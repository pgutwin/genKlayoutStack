#include "gks/io/TomlReader.hpp"

#include <toml++/toml.hpp>

#include <format>

namespace gks {

std::expected<TomlReadResult, GksError> readToml(const std::string& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        return std::unexpected(GksError{
            std::format("TOML parse error in '{}': {}", path,
                        std::string(err.description())),
            static_cast<int>(err.source().begin.line)
        });
    }

    TomlReadResult result;

    // [stack]
    if (auto* stack_tbl = tbl["stack"].as_table()) {
        if (auto v = (*stack_tbl)["tech_name"].value<std::string>())
            result.tech_name = *v;
        if (auto v = (*stack_tbl)["version"].value<std::string>())
            result.version = *v;

        // [stack.defaults]
        if (auto* defs = (*stack_tbl)["defaults"].as_table()) {
            if (auto v = (*defs)["thickness_nm"].value<double>())
                result.defaults.thickness_nm = *v;
            if (auto v = (*defs)["fill_alpha"].value<int64_t>())
                result.defaults.fill_alpha = static_cast<uint8_t>(*v);
            if (auto v = (*defs)["material"].value<std::string>())
                result.defaults.material = *v;
            if (auto v = (*defs)["dither_pattern"].value<int64_t>())
                result.defaults.dither_pattern = static_cast<int>(*v);
            if (auto v = (*defs)["line_style"].value<int64_t>())
                result.defaults.line_style = static_cast<int>(*v);
        }
    }

    // [[layer]]
    if (auto* layers_arr = tbl["layer"].as_array()) {
        for (auto& elem : *layers_arr) {
            auto* layer_tbl = elem.as_table();
            if (!layer_tbl) continue;

            RawLayer raw;
            raw.source_line = static_cast<int>(elem.source().begin.line);

            if (auto v = (*layer_tbl)["layer_num"].value<int64_t>())
                raw.layer_num = static_cast<int>(*v);
            if (auto v = (*layer_tbl)["datatype"].value<int64_t>())
                raw.datatype = static_cast<int>(*v);
            if (auto v = (*layer_tbl)["name"].value<std::string>())
                raw.name = *v;
            if (auto v = (*layer_tbl)["purpose"].value<std::string>())
                raw.purpose = *v;

            // DisplayProps — TOML-exposed subset
            if (auto v = (*layer_tbl)["fill_color"].value<std::string>()) {
                auto c = Color::fromHex(*v);
                if (c) raw.fill_color = *c;
            }
            if (auto v = (*layer_tbl)["frame_color"].value<std::string>()) {
                auto c = Color::fromHex(*v);
                if (c) raw.frame_color = *c;
            }
            if (auto v = (*layer_tbl)["fill_alpha"].value<int64_t>())
                raw.fill_alpha = static_cast<uint8_t>(*v);
            if (auto v = (*layer_tbl)["dither_pattern"].value<int64_t>())
                raw.dither_pattern = static_cast<int>(*v);
            if (auto v = (*layer_tbl)["line_style"].value<int64_t>())
                raw.line_style = static_cast<int>(*v);
            if (auto v = (*layer_tbl)["visible"].value<bool>())
                raw.visible = *v;
            if (auto v = (*layer_tbl)["valid"].value<bool>())
                raw.valid = *v;
            if (auto v = (*layer_tbl)["transparent"].value<bool>())
                raw.transparent = *v;

            // PhysicalProps
            if (auto v = (*layer_tbl)["z_start_nm"].value<double>())
                raw.z_start_nm = *v;
            if (auto v = (*layer_tbl)["thickness_nm"].value<double>())
                raw.thickness_nm = *v;
            if (auto v = (*layer_tbl)["material"].value<std::string>())
                raw.material = *v;
            if (auto v = (*layer_tbl)["layer_expression"].value<std::string>())
                raw.layer_expression = *v;

            result.layers.push_back(std::move(raw));
        }
    }

    return result;
}

} // namespace gks
