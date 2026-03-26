#include "gks/io/TomlWriter.hpp"

#include <format>
#include <fstream>

namespace gks {

namespace {

std::string boolStr(bool b) { return b ? "true" : "false"; }

void writeOptionalDefaults(std::ofstream& out, const LayerStack::Defaults& defs) {
    out << "\n[stack.defaults]\n";
    if (defs.thickness_nm)   out << std::format("thickness_nm   = {:.1f}\n", *defs.thickness_nm);
    if (defs.fill_alpha)     out << std::format("fill_alpha     = {}\n", static_cast<int>(*defs.fill_alpha));
    if (defs.material)       out << std::format("material       = \"{}\"\n", *defs.material);
    if (defs.dither_pattern) out << std::format("dither_pattern = {}\n", *defs.dither_pattern);
    if (defs.line_style)     out << std::format("line_style     = {}\n", *defs.line_style);
}

} // anonymous namespace

std::expected<void, GksError> writeToml(const LayerStack& stack, const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        return std::unexpected(GksError{
            std::format("writeToml: cannot open '{}' for writing", path), 0
        });
    }

    // [stack]
    out << "[stack]\n";
    out << std::format("tech_name = \"{}\"\n", stack.tech_name);
    out << std::format("version   = \"{}\"\n", stack.version);

    // [stack.defaults]
    const auto& defs = stack.defaults;
    bool has_defaults = defs.thickness_nm || defs.fill_alpha || defs.material
                     || defs.dither_pattern || defs.line_style;
    if (has_defaults) {
        writeOptionalDefaults(out, defs);
    }

    // [[layer]]
    for (const auto& entry : stack.layers) {
        out << "\n[[layer]]\n";
        out << std::format("name         = \"{}\"\n", entry.name);
        out << std::format("layer_num    = {}\n", entry.layer_num);
        out << std::format("datatype     = {}\n", entry.datatype);
        out << std::format("purpose      = \"{}\"\n", entry.purpose);

        // DisplayProps — TOML-exposed subset only
        const auto& d = entry.display;
        out << std::format("fill_color   = \"{}\"\n", d.fill_color.toHex());
        out << std::format("frame_color  = \"{}\"\n", d.frame_color.toHex());
        out << std::format("fill_alpha   = {}\n", static_cast<int>(d.fill_alpha));
        out << std::format("dither_pattern = {}\n", d.dither_pattern);
        out << std::format("line_style   = {}\n", d.line_style);
        out << std::format("visible      = {}\n", boolStr(d.visible));
        out << std::format("valid        = {}\n", boolStr(d.valid));
        out << std::format("transparent  = {}\n", boolStr(d.transparent));

        // PhysicalProps — always emit z_start_nm explicitly if present
        if (entry.physical) {
            const auto& p = *entry.physical;
            out << std::format("z_start_nm   = {:.1f}\n", p.z_start_nm);
            out << std::format("thickness_nm = {:.1f}\n", p.thickness_nm);
            out << std::format("material     = \"{}\"\n", p.material);
            if (p.layer_expression)
                out << std::format("layer_expression = \"{}\"\n", *p.layer_expression);
        }
    }

    return {};
}

} // namespace gks
