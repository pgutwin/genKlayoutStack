#include "gks/io/ScriptReader.hpp"

#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <format>

namespace gks {

namespace {

// Trim leading and trailing whitespace
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Apply unit multiplier: .nm=1, .um=1000, .mm=1e6, none=1
double applyUnit(double val, const std::string& suffix,
                 int line_no, std::vector<GksDiagnostic>& diags) {
    if (suffix == ".nm" || suffix.empty()) {
        if (suffix.empty()) {
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("line {}: bare number without unit suffix; assuming nm", line_no),
                line_no
            });
        }
        return val;
    } else if (suffix == ".um") {
        return val * 1000.0;
    } else if (suffix == ".mm") {
        return val * 1'000'000.0;
    }
    return val; // fallback
}

// Check if a string looks like a Ruby variable reference (not a plain number/hex)
bool looksLikeVariable(const std::string& s) {
    if (s.empty()) return false;
    // Ruby variables start with a letter or underscore
    return std::isalpha((unsigned char)s[0]) || s[0] == '_';
}

// Parse a z() call line. Returns a RawLayer or nullopt on parse failure.
// line should be the full z(...) call text (trimmed).
// line_no is used for diagnostics.
std::optional<RawLayer> parseZCall(const std::string& line, int line_no,
                                   std::vector<GksDiagnostic>& diags) {
    // Check for Ruby variable usage
    // A naive heuristic: if we see a key: value where value starts with a letter (not 0x)
    static const std::regex var_re(R"((?:zstart|height|fill|frame):\s*([a-zA-Z_]\w*))");
    std::smatch vm;
    if (std::regex_search(line, vm, var_re)) {
        diags.push_back({
            GksDiagnostic::Level::WARN,
            std::format("line {}: possible Ruby variable '{}' in argument position; parsing best-effort",
                        line_no, vm[1].str()),
            line_no
        });
    }

    RawLayer rl;

    // input(L, DT)
    static const std::regex input_re(R"(input\(\s*(\d+)\s*,\s*(\d+)\s*\))");
    std::smatch m;
    if (std::regex_search(line, m, input_re)) {
        rl.layer_num = std::stoi(m[1].str());
        rl.datatype  = std::stoi(m[2].str());
    } else {
        diags.push_back({
            GksDiagnostic::Level::WARN,
            std::format("line {}: no input(L,DT) found; skipping", line_no),
            line_no
        });
        return std::nullopt;
    }

    // zstart: V[.unit]
    // Use [-\d]+ then optional fractional part, to avoid consuming the unit dot
    static const std::regex zstart_re(R"(zstart:\s*([-\d]+(?:\.\d+)?)(\.nm|\.um|\.mm)?)");
    if (std::regex_search(line, m, zstart_re)) {
        double val = std::stod(m[1].str());
        std::string suffix = m[2].str();
        rl.z_start_nm = applyUnit(val, suffix, line_no, diags);
    }

    // height: V[.unit]
    static const std::regex height_re(R"(height:\s*([-\d]+(?:\.\d+)?)(\.nm|\.um|\.mm)?)");
    if (std::regex_search(line, m, height_re)) {
        double val = std::stod(m[1].str());
        std::string suffix = m[2].str();
        rl.thickness_nm = applyUnit(val, suffix, line_no, diags);
    }

    // fill: 0xRRGGBB
    static const std::regex fill_re(R"(fill:\s*(0x[0-9a-fA-F]+))");
    if (std::regex_search(line, m, fill_re)) {
        auto c = Color::fromHex(m[1].str());
        if (c.has_value()) rl.fill_color = *c;
    }

    // frame: 0xRRGGBB
    static const std::regex frame_re(R"(frame:\s*(0x[0-9a-fA-F]+))");
    if (std::regex_search(line, m, frame_re)) {
        auto c = Color::fromHex(m[1].str());
        if (c.has_value()) rl.frame_color = *c;
    }

    // name: "..."
    static const std::regex name_re("name:\\s*\"([^\"]*)\"");
    if (std::regex_search(line, m, name_re)) {
        rl.name = m[1].str();
    }

    rl.source_line = line_no;
    return rl;
}

// Parse the header of a zz() call.
// Returns name, fill_color, frame_color.
struct ZzHeader {
    std::string         name;
    std::optional<Color> fill_color;
    std::optional<Color> frame_color;
};

ZzHeader parseZzHeader(const std::string& line, int line_no,
                       std::vector<GksDiagnostic>& diags) {
    ZzHeader hdr;

    static const std::regex name_re("name:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(line, m, name_re)) {
        hdr.name = m[1].str();
    }

    static const std::regex fill_re(R"(fill:\s*(0x[0-9a-fA-F]+))");
    if (std::regex_search(line, m, fill_re)) {
        auto c = Color::fromHex(m[1].str());
        if (c.has_value()) hdr.fill_color = *c;
    }

    static const std::regex frame_re(R"(frame:\s*(0x[0-9a-fA-F]+))");
    if (std::regex_search(line, m, frame_re)) {
        auto c = Color::fromHex(m[1].str());
        if (c.has_value()) hdr.frame_color = *c;
    }

    return hdr;
}

} // anonymous namespace

std::expected<ScriptReadResult, GksError> readScript(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return std::unexpected(GksError{
            std::format("readScript: cannot open '{}'", path), 0
        });
    }

    ScriptReadResult result;

    std::string line;
    int line_no = 0;

    bool in_zz_block = false;
    ZzHeader zz_hdr;
    std::vector<RawLayer> zz_inner_layers;

    while (std::getline(in, line)) {
        ++line_no;
        std::string t = trim(line);

        // Skip blank lines and comment lines
        if (t.empty() || t[0] == '#') continue;

        if (in_zz_block) {
            if (t == "end") {
                // Close zz block: finalize inner layers
                if (!zz_inner_layers.empty()) {
                    // Build layer_expression for the first inner layer
                    std::string expr;
                    for (size_t i = 0; i < zz_inner_layers.size(); ++i) {
                        if (i > 0) expr += " + ";
                        expr += std::format("input({},{})",
                            *zz_inner_layers[i].layer_num,
                            *zz_inner_layers[i].datatype);
                    }
                    zz_inner_layers[0].layer_expression = expr;

                    // Apply outer zz() colors to all inner layers
                    for (auto& rl : zz_inner_layers) {
                        if (zz_hdr.fill_color.has_value())
                            rl.fill_color = zz_hdr.fill_color;
                        if (zz_hdr.frame_color.has_value())
                            rl.frame_color = zz_hdr.frame_color;
                        result.layers.push_back(std::move(rl));
                    }
                }
                zz_inner_layers.clear();
                in_zz_block = false;
            } else if (t.size() >= 2 && t.substr(0, 2) == "z(") {
                // Inner z() call
                auto rl = parseZCall(t, line_no, result.diagnostics);
                if (rl.has_value()) {
                    zz_inner_layers.push_back(std::move(*rl));
                }
            } else {
                result.diagnostics.push_back({
                    GksDiagnostic::Level::WARN,
                    std::format("line {}: unexpected line inside zz() block: {}", line_no, t),
                    line_no
                });
            }
        } else {
            if (t.substr(0, 3) == "zz(") {
                // Start of a zz() block
                in_zz_block = true;
                zz_inner_layers.clear();
                zz_hdr = parseZzHeader(t, line_no, result.diagnostics);
            } else if (t.substr(0, 2) == "z(") {
                // Standalone z() call
                auto rl = parseZCall(t, line_no, result.diagnostics);
                if (rl.has_value()) {
                    result.layers.push_back(std::move(*rl));
                }
            } else {
                // Unknown line — warn and skip
                result.diagnostics.push_back({
                    GksDiagnostic::Level::WARN,
                    std::format("line {}: unrecognized line: {}", line_no, t),
                    line_no
                });
            }
        }
    }

    if (in_zz_block) {
        result.diagnostics.push_back({
            GksDiagnostic::Level::WARN,
            "Unclosed zz() block at end of file",
            line_no
        });
        // Flush what we have
        if (!zz_inner_layers.empty()) {
            std::string expr;
            for (size_t i = 0; i < zz_inner_layers.size(); ++i) {
                if (i > 0) expr += " + ";
                expr += std::format("input({},{})",
                    *zz_inner_layers[i].layer_num,
                    *zz_inner_layers[i].datatype);
            }
            zz_inner_layers[0].layer_expression = expr;
            for (auto& rl : zz_inner_layers) {
                if (zz_hdr.fill_color.has_value())  rl.fill_color  = zz_hdr.fill_color;
                if (zz_hdr.frame_color.has_value()) rl.frame_color = zz_hdr.frame_color;
                result.layers.push_back(std::move(rl));
            }
        }
    }

    return result;
}

} // namespace gks
