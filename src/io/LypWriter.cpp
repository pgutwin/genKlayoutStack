#include "gks/io/LypWriter.hpp"
#include <pugixml.hpp>
#include <format>

namespace gks {

namespace {

static const char* boolStr(bool v) { return v ? "true" : "false"; }

} // anonymous namespace

std::expected<void, GksError> writeLyp(const LayerStack& stack, const std::string& path) {
    pugi::xml_document doc;

    // XML declaration
    auto decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "utf-8";

    pugi::xml_node root = doc.append_child("layer-properties");

    for (auto it = stack.layers.rbegin(); it != stack.layers.rend(); ++it) { const LayerEntry& entry = *it;
        pugi::xml_node prop = root.append_child("properties");
        const DisplayProps& d = entry.display;

        // Emit in spec Table 4.4 order:
        // source, name, fill-color, frame-color, fill-brightness, frame-brightness,
        // dither-pattern, line-style, valid, visible, transparent,
        // width, marked, xfill, animation, expanded

        // <source>
        {
            std::string src = std::format("{}/{}@1", entry.layer_num, entry.datatype);
            prop.append_child("source").append_child(pugi::node_pcdata).set_value(src.c_str());
        }

        // <name>
        prop.append_child("name").append_child(pugi::node_pcdata).set_value(entry.name.c_str());

        // <fill-color>
        prop.append_child("fill-color").append_child(pugi::node_pcdata)
            .set_value(d.fill_color.toHex().c_str());

        // <frame-color>
        prop.append_child("frame-color").append_child(pugi::node_pcdata)
            .set_value(d.frame_color.toHex().c_str());

        // <fill-brightness>
        prop.append_child("fill-brightness").append_child(pugi::node_pcdata)
            .set_value(std::to_string(d.fill_brightness).c_str());

        // <frame-brightness>
        prop.append_child("frame-brightness").append_child(pugi::node_pcdata)
            .set_value(std::to_string(d.frame_brightness).c_str());

        // <dither-pattern>: "I{n}" or empty element if -1
        {
            auto dp = prop.append_child("dither-pattern");
            if (d.dither_pattern >= 0) {
                std::string val = std::format("I{}", d.dither_pattern);
                dp.append_child(pugi::node_pcdata).set_value(val.c_str());
            }
            // else: emit self-closing empty element
        }

        // <line-style>: "L{n}" or empty element if -1
        {
            auto ls = prop.append_child("line-style");
            if (d.line_style >= 0) {
                std::string val = std::format("L{}", d.line_style);
                ls.append_child(pugi::node_pcdata).set_value(val.c_str());
            }
        }

        // <valid>
        prop.append_child("valid").append_child(pugi::node_pcdata).set_value(boolStr(d.valid));

        // <visible>
        prop.append_child("visible").append_child(pugi::node_pcdata).set_value(boolStr(d.visible));

        // <transparent>
        prop.append_child("transparent").append_child(pugi::node_pcdata)
            .set_value(boolStr(d.transparent));

        // <width>: int or empty element
        {
            auto w = prop.append_child("width");
            if (d.width.has_value()) {
                w.append_child(pugi::node_pcdata)
                    .set_value(std::to_string(*d.width).c_str());
            }
        }

        // <marked>
        prop.append_child("marked").append_child(pugi::node_pcdata).set_value(boolStr(d.marked));

        // <xfill>
        prop.append_child("xfill").append_child(pugi::node_pcdata).set_value(boolStr(d.xfill));

        // <animation>
        prop.append_child("animation").append_child(pugi::node_pcdata)
            .set_value(std::to_string(d.animation).c_str());

        // <expanded>
        prop.append_child("expanded").append_child(pugi::node_pcdata)
            .set_value(boolStr(d.expanded));
    }

    if (!doc.save_file(path.c_str(), " ")) {
        return std::unexpected(GksError{
            std::format("writeLyp: failed to write '{}'", path), 0
        });
    }

    return {};
}

} // namespace gks
