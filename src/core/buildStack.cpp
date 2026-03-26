#include "gks/core/LayerStack.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace gks {

namespace {

constexpr double kEpsilon = 1e-9;

// Apply document-level defaults and layer-level values to produce a DisplayProps.
// Layer-level values take precedence over defaults.
DisplayProps resolveDisplay(const RawLayer& row, const LayerStack::Defaults& defaults) {
    DisplayProps d; // starts from struct defaults

    if (row.fill_color)     d.fill_color      = *row.fill_color;
    if (row.frame_color)    d.frame_color     = *row.frame_color;

    if (row.fill_alpha)             d.fill_alpha      = *row.fill_alpha;
    else if (defaults.fill_alpha)   d.fill_alpha      = *defaults.fill_alpha;

    if (row.dither_pattern)             d.dither_pattern  = *row.dither_pattern;
    else if (defaults.dither_pattern)   d.dither_pattern  = *defaults.dither_pattern;

    if (row.line_style)             d.line_style      = *row.line_style;
    else if (defaults.line_style)   d.line_style      = *defaults.line_style;

    if (row.visible)     d.visible      = *row.visible;
    if (row.valid)       d.valid        = *row.valid;
    if (row.transparent) d.transparent  = *row.transparent;

    return d;
}

} // anonymous namespace

BuildResult buildStack(std::string              tech_name,
                       std::string              version,
                       const std::vector<RawLayer>& raw,
                       const LayerStack::Defaults&  defaults) {
    BuildResult result;
    LayerStack& stack = result.stack;
    auto& diags = result.diagnostics;

    stack.tech_name = std::move(tech_name);
    stack.version   = std::move(version);
    stack.defaults  = defaults;

    double high_water    = 0.0;
    bool   hw_initialized = false;

    // Track which z_start values are explicit (for parallel group detection)
    // map: z_start_nm -> list of layer names that share it (explicit only)
    std::map<double, std::vector<std::string>> explicit_z_groups;

    for (const auto& row : raw) {
        // layer_num is required
        if (!row.layer_num) {
            throw std::runtime_error(
                std::format("buildStack: layer at source_line {} has no layer_num (required)",
                            row.source_line));
        }

        LayerEntry entry;
        entry.layer_num = *row.layer_num;
        entry.datatype  = row.datatype.value_or(0);
        entry.name      = row.name.value_or("");
        entry.purpose   = row.purpose.value_or("drawing");
        entry.display   = resolveDisplay(row, defaults);

        // Determine if this entry has any physical data
        bool has_physical = row.z_start_nm.has_value()  ||
                            row.thickness_nm.has_value() ||
                            row.material.has_value();

        if (has_physical) {
            double thickness = row.thickness_nm.value_or(
                                   defaults.thickness_nm.value_or(0.0));
            std::string material = row.material.value_or(
                                       defaults.material.value_or(""));

            double z_start;
            bool   z_explicit = row.z_start_nm.has_value();

            if (z_explicit) {
                z_start = *row.z_start_nm;

                if (hw_initialized) {
                    if (z_start < high_water - kEpsilon) {
                        diags.push_back({
                            GksDiagnostic::Level::WARN,
                            std::format("[line {}] '{}': z_start={:.1f}nm below high_water={:.1f}nm — possible burial",
                                        row.source_line, entry.name, z_start, high_water),
                            row.source_line
                        });
                    } else if (z_start > high_water + kEpsilon) {
                        diags.push_back({
                            GksDiagnostic::Level::WARN,
                            std::format("[line {}] '{}': {:.1f}nm gap above high_water={:.1f}nm",
                                        row.source_line, entry.name,
                                        z_start - high_water, high_water),
                            row.source_line
                        });
                    }
                }

                // Track for parallel group detection
                explicit_z_groups[z_start].push_back(entry.name);

            } else {
                z_start = hw_initialized ? high_water : 0.0;
            }

            hw_initialized = true;
            high_water = std::max(high_water, z_start + thickness);

            // Emit INFO diagnostic
            std::string z_kind;
            if (z_explicit) {
                // Check if this is part of a parallel group (same z as a previous layer)
                const auto& group = explicit_z_groups[z_start];
                if (group.size() > 1) {
                    // Build group member list (excluding current)
                    std::string members;
                    for (size_t i = 0; i + 1 < group.size(); ++i) {
                        if (i) members += ", ";
                        members += group[i];
                    }
                    z_kind = std::format("explicit — parallel group {{{}, {}}}", members, entry.name);
                } else {
                    z_kind = "explicit";
                }
            } else {
                z_kind = std::format("accumulated from high_water={:.1f}", z_start);
            }

            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("'{}': z=[{:.1f}, {:.1f}] nm  ({})",
                            entry.name, z_start, z_start + thickness, z_kind),
                row.source_line
            });

            entry.physical = PhysicalProps{
                .z_start_nm       = z_start,
                .thickness_nm     = thickness,
                .material         = std::move(material),
                .layer_expression = row.layer_expression
            };
        } else {
            entry.physical = std::nullopt;
        }

        stack.layers.push_back(std::move(entry));
    }

    // Emit parallel group summary diagnostics
    for (const auto& [z_val, names] : explicit_z_groups) {
        if (names.size() > 1) {
            std::string members;
            for (size_t i = 0; i < names.size(); ++i) {
                if (i) members += ", ";
                members += names[i];
            }
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("SUMMARY  Parallel group @ z={:.1f}nm: {{{}}}", z_val, members),
                0
            });
        }
    }

    return result;
}

} // namespace gks
