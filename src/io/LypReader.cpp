#include "gks/io/LypReader.hpp"
#include <pugixml.hpp>
#include <format>
#include <string_view>
#include <cctype>

namespace gks {

namespace {

// Parse "#rrggbb" hex color from lyp element text
static Color parseColor(std::string_view s) {
    auto result = Color::fromHex(s);
    if (result) return *result;
    return Color{0x80, 0x80, 0x80};
}

// Parse bool string "true"/"false" case-insensitively
static bool parseBool(std::string_view s) {
    if (s.size() == 4 &&
        std::tolower((unsigned char)s[0]) == 't' &&
        std::tolower((unsigned char)s[1]) == 'r' &&
        std::tolower((unsigned char)s[2]) == 'u' &&
        std::tolower((unsigned char)s[3]) == 'e')
        return true;
    return false;
}

// Parse dither-pattern: "I{n}" → n; empty → -1
static int parseDitherPattern(std::string_view s) {
    if (s.empty()) return -1;
    if (!s.empty() && (s[0] == 'I' || s[0] == 'i'))
        return std::stoi(std::string(s.substr(1)));
    return -1;
}

// Parse line-style: "L{n}" → n; empty → -1
static int parseLineStyle(std::string_view s) {
    if (s.empty()) return -1;
    if (!s.empty() && (s[0] == 'L' || s[0] == 'l'))
        return std::stoi(std::string(s.substr(1)));
    return -1;
}

// Parse <source> "layer/datatype@idx" → (layer_num, datatype)
static bool parseSource(std::string_view s, int& layer_num, int& datatype) {
    // format: "L/DT@idx"
    auto slash = s.find('/');
    if (slash == std::string_view::npos) return false;
    auto at    = s.find('@', slash);
    if (at == std::string_view::npos) return false;

    try {
        layer_num = std::stoi(std::string(s.substr(0, slash)));
        datatype  = std::stoi(std::string(s.substr(slash + 1, at - slash - 1)));
    } catch (...) {
        return false;
    }
    return true;
}

} // anonymous namespace

std::expected<LayerStack, GksError> readLyp(const std::string& path) {
    pugi::xml_document doc;
    pugi::xml_parse_result pr = doc.load_file(path.c_str());
    if (!pr) {
        return std::unexpected(GksError{
            std::format("readLyp: failed to parse '{}': {}", path, pr.description()),
            0
        });
    }

    pugi::xml_node root = doc.child("layer-properties");
    if (!root) {
        return std::unexpected(GksError{
            std::format("readLyp: no <layer-properties> root in '{}'", path),
            0
        });
    }

    LayerStack stack;
    // tech_name and version are unknown from .lyp
    stack.tech_name = "";
    stack.version   = "";

    for (pugi::xml_node prop : root.children("properties")) {
        LayerEntry entry;
        entry.physical = std::nullopt;

        // <source> → layer_num, datatype
        if (auto src = prop.child("source")) {
            int ln = 0, dt = 0;
            if (parseSource(src.child_value(), ln, dt)) {
                entry.layer_num = ln;
                entry.datatype  = dt;
            } else {
                return std::unexpected(GksError{
                    std::format("readLyp: malformed <source> '{}'", src.child_value()),
                    0
                });
            }
        } else {
            return std::unexpected(GksError{
                "readLyp: <properties> missing <source> element", 0
            });
        }

        // <name> — may be empty or absent
        if (auto n = prop.child("name")) {
            entry.name = n.child_value();
        } else {
            entry.name = "";
        }
        entry.purpose = "";

        // DisplayProps
        DisplayProps& d = entry.display;

        if (auto e = prop.child("fill-color"))
            d.fill_color = parseColor(e.child_value());
        if (auto e = prop.child("frame-color"))
            d.frame_color = parseColor(e.child_value());
        if (auto e = prop.child("fill-brightness"))
            d.fill_brightness = std::stoi(e.child_value());
        if (auto e = prop.child("frame-brightness"))
            d.frame_brightness = std::stoi(e.child_value());

        if (auto e = prop.child("dither-pattern"))
            d.dither_pattern = parseDitherPattern(e.child_value());
        if (auto e = prop.child("line-style"))
            d.line_style = parseLineStyle(e.child_value());

        if (auto e = prop.child("valid"))
            d.valid = parseBool(e.child_value());
        if (auto e = prop.child("visible"))
            d.visible = parseBool(e.child_value());
        if (auto e = prop.child("transparent"))
            d.transparent = parseBool(e.child_value());

        if (auto e = prop.child("width")) {
            std::string_view wv = e.child_value();
            if (!wv.empty())
                d.width = std::stoi(std::string(wv));
            else
                d.width = std::nullopt;
        }

        if (auto e = prop.child("marked"))
            d.marked = parseBool(e.child_value());
        if (auto e = prop.child("xfill"))
            d.xfill = parseBool(e.child_value());
        if (auto e = prop.child("animation"))
            d.animation = std::stoi(e.child_value());
        if (auto e = prop.child("expanded"))
            d.expanded = parseBool(e.child_value());

        // fill_alpha: not directly in lyp; derive from fill_brightness
        // fill_brightness is a passthrough; fill_alpha stays at default 128
        // (the mapping is lossless for lyp fidelity; fill_alpha is TOML-exposed only)
        d.fill_alpha = 128;

        stack.layers.push_back(std::move(entry));
    }

    return stack;
}

} // namespace gks
