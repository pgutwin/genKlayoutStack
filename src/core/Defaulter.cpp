#include "gks/core/Defaulter.hpp"

#include <format>

namespace gks {

// Apply LayerStack::Defaults to entries that are still at their struct-default
// (unset) values.  Each substitution is logged as an INFO diagnostic.
//
// "Unset" sentinels used for detection:
//   fill_alpha      == 128  (struct default)
//   dither_pattern  == -1   (struct default; means solid fill)
//   line_style      == -1   (struct default; means no style)
//   material        == ""   (empty string — never a valid material name)
//
// Limitation: a user who explicitly writes fill_alpha = 128 is indistinguishable
// from a layer that got the struct default.  This is a known v1 limitation.

std::vector<GksDiagnostic> applyDefaults(LayerStack& stack) {
    std::vector<GksDiagnostic> diags;
    const auto& defs = stack.defaults;

    for (auto& entry : stack.layers) {
        // ── Display defaults ──────────────────────────────────────────────────

        if (defs.fill_alpha.has_value()
            && *defs.fill_alpha != 128          // document default differs from struct default
            && entry.display.fill_alpha == 128) // still at struct default → treat as unset
        {
            entry.display.fill_alpha = *defs.fill_alpha;
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("layer '{}' ({}/{}): fill_alpha defaulted to {}",
                            entry.name, entry.layer_num, entry.datatype,
                            static_cast<int>(*defs.fill_alpha)),
                0
            });
        }

        if (defs.dither_pattern.has_value()
            && *defs.dither_pattern != -1
            && entry.display.dither_pattern == -1)
        {
            entry.display.dither_pattern = *defs.dither_pattern;
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("layer '{}' ({}/{}): dither_pattern defaulted to {}",
                            entry.name, entry.layer_num, entry.datatype,
                            *defs.dither_pattern),
                0
            });
        }

        if (defs.line_style.has_value()
            && *defs.line_style != -1
            && entry.display.line_style == -1)
        {
            entry.display.line_style = *defs.line_style;
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("layer '{}' ({}/{}): line_style defaulted to {}",
                            entry.name, entry.layer_num, entry.datatype,
                            *defs.line_style),
                0
            });
        }

        // ── Physical defaults ─────────────────────────────────────────────────

        if (entry.physical.has_value()
            && defs.material.has_value()
            && entry.physical->material.empty())
        {
            entry.physical->material = *defs.material;
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("layer '{}' ({}/{}): material defaulted to '{}'",
                            entry.name, entry.layer_num, entry.datatype,
                            *defs.material),
                0
            });
        }
    }

    return diags;
}

} // namespace gks
