#include "gks/core/LayerStack.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace gks {
namespace {

constexpr double kEpsilon  = 1e-9;
constexpr double kThickEps = 0.001; // nm; threshold for thickness mismatch warning

// Apply document-level defaults to produce a DisplayProps.
// Layer-level values take precedence over defaults.
DisplayProps resolveDisplay(const RawLayer& row, const LayerStack::Defaults& defaults) {
    DisplayProps d;
    if (row.fill_color)               d.fill_color     = *row.fill_color;
    if (row.frame_color)              d.frame_color    = *row.frame_color;
    if (row.fill_alpha)               d.fill_alpha     = *row.fill_alpha;
    else if (defaults.fill_alpha)     d.fill_alpha     = *defaults.fill_alpha;
    if (row.dither_pattern)           d.dither_pattern = *row.dither_pattern;
    else if (defaults.dither_pattern) d.dither_pattern = *defaults.dither_pattern;
    if (row.line_style)               d.line_style     = *row.line_style;
    else if (defaults.line_style)     d.line_style     = *defaults.line_style;
    if (row.visible)                  d.visible        = *row.visible;
    if (row.valid)                    d.valid          = *row.valid;
    if (row.transparent)              d.transparent    = *row.transparent;
    return d;
}

// Parsed alignment reference: "layer_name:edge" → {name, edge}
struct AlignRef { std::string name; std::string edge; };

std::optional<AlignRef> parseAlignRef(const std::string& s) {
    auto pos = s.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= s.size())
        return std::nullopt;
    std::string edge = s.substr(pos + 1);
    if (edge != "top" && edge != "bottom") return std::nullopt;
    return AlignRef{s.substr(0, pos), edge};
}

// Compute the z coordinate of a named edge given the layer's z_start and thickness.
double edgeZ(double z_start, double thickness, const std::string& edge) {
    return (edge == "top") ? z_start + thickness : z_start;
}

} // anonymous namespace

BuildResult buildStack(std::string              tech_name,
                       std::string              version,
                       const std::vector<RawLayer>& raw,
                       const LayerStack::Defaults&  defaults) {
    BuildResult result;
    LayerStack& stack = result.stack;
    auto&       diags = result.diagnostics;

    stack.tech_name = std::move(tech_name);
    stack.version   = std::move(version);
    stack.defaults  = defaults;

    if (raw.empty()) return result;

    const size_t N = raw.size();

    // Per-layer intermediate state used throughout this function.
    struct LayerState {
        double z_start      = 0.0;
        double thickness    = 0.0;
        bool   z_resolved   = false;  // z_start is known (explicit or alignment)
        bool   has_physical = false;
    };
    std::vector<LayerState> state(N);

    // Build name → index map for alignment resolution.
    std::unordered_map<std::string, size_t> name_to_idx;
    for (size_t i = 0; i < N; ++i) {
        if (raw[i].name.has_value() && !raw[i].name->empty())
            name_to_idx[*raw[i].name] = i;
    }

    // Pre-compute per-layer state: thickness, has_physical, and explicit z_start.
    for (size_t i = 0; i < N; ++i) {
        const auto& row = raw[i];
        state[i].thickness = row.thickness_nm.value_or(defaults.thickness_nm.value_or(0.0));
        state[i].has_physical =
            row.z_start_nm.has_value()
            || row.thickness_nm.has_value()
            || row.material.has_value()
            || row.align_bottom_to.has_value()
            || row.align_top_to.has_value();
        if (row.z_start_nm.has_value()) {
            state[i].z_start    = *row.z_start_nm;
            state[i].z_resolved = true;
        }
    }

    // ── Conflict check: align_* and z_start_nm are mutually exclusive ─────────
    {
        bool conflict = false;
        for (size_t i = 0; i < N; ++i) {
            if ((raw[i].align_bottom_to.has_value() || raw[i].align_top_to.has_value())
                    && raw[i].z_start_nm.has_value()) {
                diags.push_back({
                    GksDiagnostic::Level::ERROR,
                    std::format("align_* and z_start_nm are mutually exclusive on layer '{}'",
                                raw[i].name.value_or("?")),
                    raw[i].source_line
                });
                conflict = true;
            }
        }
        if (conflict) return result;
    }

    // ── Alignment resolution (spec Section 5.2a) ──────────────────────────────
    bool has_any_alignment = false;
    for (size_t i = 0; i < N; ++i) {
        if (raw[i].align_bottom_to.has_value() || raw[i].align_top_to.has_value()) {
            has_any_alignment = true;
            break;
        }
    }

    if (has_any_alignment) {
        // Build dependency graph: deps[i] = indices of layers that i depends on.
        std::vector<std::vector<size_t>> deps(N);
        bool bad_ref = false;

        auto addDep = [&](size_t i, const std::optional<std::string>& align_opt) {
            if (!align_opt) return;
            auto ref = parseAlignRef(*align_opt);
            if (!ref) {
                diags.push_back({
                    GksDiagnostic::Level::ERROR,
                    std::format("invalid alignment '{}' on layer '{}' (expected 'name:top' or 'name:bottom')",
                                *align_opt, raw[i].name.value_or("?")),
                    raw[i].source_line
                });
                bad_ref = true;
                return;
            }
            auto it = name_to_idx.find(ref->name);
            if (it == name_to_idx.end()) {
                diags.push_back({
                    GksDiagnostic::Level::ERROR,
                    std::format("align target '{}' not found in stack", ref->name),
                    raw[i].source_line
                });
                bad_ref = true;
                return;
            }
            deps[i].push_back(it->second);
        };

        for (size_t i = 0; i < N; ++i) {
            addDep(i, raw[i].align_bottom_to);
            addDep(i, raw[i].align_top_to);
        }
        if (bad_ref) return result;

        // DFS for cycle detection and post-order traversal (dependencies first).
        // Edges: aligned layer → its referenced layers.
        std::vector<int>    color(N, 0); // 0=white, 1=gray, 2=black
        std::vector<size_t> topo_order;  // post-order; deps appear before dependents
        std::vector<size_t> dfs_path;
        bool cycle = false;

        std::function<void(size_t)> dfs = [&](size_t u) {
            if (cycle) return;
            color[u] = 1;
            dfs_path.push_back(u);
            for (size_t v : deps[u]) {
                if (color[v] == 1) {
                    // Cycle: emit error with path
                    cycle = true;
                    std::string path;
                    bool in_cycle = false;
                    for (size_t node : dfs_path) {
                        if (node == v) in_cycle = true;
                        if (in_cycle) {
                            if (!path.empty()) path += " \u2192 ";
                            path += raw[node].name.value_or("?");
                        }
                    }
                    path += " \u2192 " + raw[v].name.value_or("?");
                    diags.push_back({
                        GksDiagnostic::Level::ERROR,
                        std::format("circular alignment reference: {}", path),
                        raw[u].source_line
                    });
                    return;
                }
                if (color[v] == 0) {
                    dfs(v);
                    if (cycle) return;
                }
            }
            dfs_path.pop_back();
            color[u] = 2;
            topo_order.push_back(u);
        };

        // Start DFS only from aligned nodes; non-aligned nodes are visited as deps.
        for (size_t i = 0; i < N && !cycle; ++i) {
            if (color[i] == 0 &&
                    (raw[i].align_bottom_to.has_value() || raw[i].align_top_to.has_value())) {
                dfs(i);
            }
        }
        if (cycle) return result;

        // Process topo_order in order (post-order = deps come before dependents).
        bool align_err = false;

        // Returns the absolute z of the named edge, or nullopt if the reference
        // layer is not yet resolved (accumulated layer — error).
        auto refEdge = [&](size_t layer_i, const std::string& align_str) -> std::optional<double> {
            auto ref = parseAlignRef(align_str); // already validated above
            size_t ri = name_to_idx.at(ref->name);
            if (!state[ri].z_resolved) {
                diags.push_back({
                    GksDiagnostic::Level::ERROR,
                    std::format("align target '{}' is an accumulated layer with no explicit "
                                "z_start; alignment requires an explicit z_start on the reference",
                                ref->name),
                    raw[layer_i].source_line
                });
                align_err = true;
                return std::nullopt;
            }
            return edgeZ(state[ri].z_start, state[ri].thickness, ref->edge);
        };

        for (size_t i : topo_order) {
            if (!raw[i].align_bottom_to.has_value() && !raw[i].align_top_to.has_value())
                continue; // non-aligned node visited as a dep; nothing to resolve here

            const std::string lname = raw[i].name.value_or("?");
            bool both = raw[i].align_bottom_to.has_value() && raw[i].align_top_to.has_value();

            if (both) {
                auto bz = refEdge(i, *raw[i].align_bottom_to);
                auto tz = refEdge(i, *raw[i].align_top_to);
                if (!bz || !tz) { align_err = true; continue; }
                double derived_t = *tz - *bz;
                if (raw[i].thickness_nm.has_value() &&
                        std::abs(*raw[i].thickness_nm - derived_t) > kThickEps) {
                    diags.push_back({
                        GksDiagnostic::Level::WARN,
                        std::format("'{}': thickness_nm={:.3f}nm ignored; "
                                    "derived height from alignment is {:.3f}nm",
                                    lname, *raw[i].thickness_nm, derived_t),
                        raw[i].source_line
                    });
                }
                state[i].z_start    = *bz;
                state[i].thickness  = derived_t;
                state[i].z_resolved = true;
            } else if (raw[i].align_bottom_to.has_value()) {
                auto z = refEdge(i, *raw[i].align_bottom_to);
                if (!z) { align_err = true; continue; }
                state[i].z_start    = *z;
                state[i].z_resolved = true;
                // thickness stays as pre-computed from thickness_nm or default
            } else {
                // align_top_to only: z_start = ref_edge - thickness
                if (!raw[i].thickness_nm.has_value() && !defaults.thickness_nm.has_value()) {
                    diags.push_back({
                        GksDiagnostic::Level::ERROR,
                        std::format("'{}': align_top_to requires thickness_nm or a document "
                                    "default to derive z_start", lname),
                        raw[i].source_line
                    });
                    align_err = true;
                    continue;
                }
                auto z = refEdge(i, *raw[i].align_top_to);
                if (!z) { align_err = true; continue; }
                state[i].z_start    = *z - state[i].thickness;
                state[i].z_resolved = true;
            }
        }

        if (align_err) return result;
    }

    // ── Find anchor ────────────────────────────────────────────────────────────
    // The anchor is the first layer with z_start_nm == 0.0 explicitly set.
    // If none found: backward-compat mode — use last layer as anchor.
    size_t anchor_idx   = N - 1;
    bool   anchor_found = false;
    for (size_t i = 0; i < N; ++i) {
        if (raw[i].z_start_nm.has_value() && std::abs(*raw[i].z_start_nm) < kEpsilon) {
            anchor_idx   = i;
            anchor_found = true;
            break;
        }
    }
    // Ensure anchor's z_start is set (may already be resolved by explicit value).
    if (!state[anchor_idx].z_resolved) {
        state[anchor_idx].z_start    = 0.0;
        state[anchor_idx].z_resolved = true;
    }
    const double anchor_z = state[anchor_idx].z_start;
    const double anchor_t = state[anchor_idx].thickness;

    // ── Parallel-group tracking ───────────────────────────────────────────────
    // Map from z_start value → list of layer names that share it (explicit or aligned).
    // Used to detect co-planar parallel groups and build the INFO strings.
    std::map<double, std::vector<std::string>> z_groups;

    // Record layer i at z_start in z_groups; return a z_kind string augmented with
    // parallel-group annotation when two or more layers share the same z.
    auto recordZ = [&](size_t i, double z_start, std::string base_kind) -> std::string {
        auto& group = z_groups[z_start];
        group.push_back(raw[i].name.value_or(""));
        if (group.size() > 1) {
            std::string members;
            for (size_t j = 0; j + 1 < group.size(); ++j) {
                if (j) members += ", ";
                members += group[j];
            }
            return std::format("{} \u2014 parallel group {{{}, {}}}",
                               base_kind, members, raw[i].name.value_or(""));
        }
        return base_kind;
    };

    // ── Pass 1: upward accumulation ───────────────────────────────────────────
    // Process layers above the anchor in the file (indices 0..anchor_idx-1),
    // in reverse file order (from anchor_idx-1 down to 0).
    // hw_pos = high-water mark; starts at the anchor's top edge.
    {
        double hw_pos = anchor_z + anchor_t;

        for (size_t k = 0; k < anchor_idx; ++k) {
            const size_t i = anchor_idx - 1 - k;
            const auto&  row = raw[i];
            if (!state[i].has_physical) continue;

            const bool is_align = row.align_bottom_to.has_value() || row.align_top_to.has_value();

            double      z_start;
            std::string z_kind;

            if (is_align && state[i].z_resolved) {
                // Alignment-resolved — position is fixed; skip burial/gap check.
                z_start = state[i].z_start;
                z_kind  = recordZ(i, z_start, "alignment-resolved");
            } else if (row.z_start_nm.has_value() &&
                       (!anchor_found || *row.z_start_nm > kEpsilon)) {
                // Explicit z_start: use if anchor was found and z > 0 (upward-pass rule),
                // OR always in backward-compat mode (no anchor) to match v0.4.1 behaviour.
                z_start = *row.z_start_nm;
                state[i].z_start = z_start;
                // Burial / gap diagnostics.
                if (z_start < hw_pos - kEpsilon) {
                    diags.push_back({
                        GksDiagnostic::Level::WARN,
                        std::format("[line {}] '{}': z_start={:.1f}nm below high_water={:.1f}nm"
                                    " \u2014 possible burial",
                                    row.source_line, row.name.value_or("?"), z_start, hw_pos),
                        row.source_line
                    });
                } else if (z_start > hw_pos + kEpsilon) {
                    diags.push_back({
                        GksDiagnostic::Level::WARN,
                        std::format("[line {}] '{}': {:.1f}nm gap above high_water={:.1f}nm",
                                    row.source_line, row.name.value_or("?"),
                                    z_start - hw_pos, hw_pos),
                        row.source_line
                    });
                }
                z_kind = recordZ(i, z_start, "explicit");
            } else {
                // Accumulated upward from hw_pos.
                z_start = hw_pos;
                state[i].z_start    = z_start;
                state[i].z_resolved = true;
                z_kind = std::format("accumulated upward from hw={:.1f}", hw_pos);
            }

            hw_pos = std::max(hw_pos, z_start + state[i].thickness);

            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("'{}': z=[{:.1f}, {:.1f}] nm  ({})",
                            row.name.value_or("?"), z_start, z_start + state[i].thickness,
                            z_kind),
                row.source_line
            });
        }
    }

    // ── Pass 2: downward accumulation ─────────────────────────────────────────
    // Process layers below the anchor in the file (indices anchor_idx+1..N-1),
    // in forward file order.
    // hw_neg = low-water mark; starts at the anchor's bottom edge.
    {
        double hw_neg = anchor_z;

        for (size_t i = anchor_idx + 1; i < N; ++i) {
            const auto& row = raw[i];
            if (!state[i].has_physical) continue;

            const bool is_align = row.align_bottom_to.has_value() || row.align_top_to.has_value();

            double      z_start;
            std::string z_kind;

            if (is_align && state[i].z_resolved) {
                // Alignment-resolved.
                z_start = state[i].z_start;
                z_kind  = recordZ(i, z_start, "alignment-resolved");
            } else if (row.z_start_nm.has_value() && *row.z_start_nm < -kEpsilon) {
                // Explicit negative z_start (downward-pass rule: use only if < 0).
                z_start = *row.z_start_nm;
                state[i].z_start = z_start;
                z_kind = recordZ(i, z_start, "explicit");
            } else {
                // Accumulated downward from hw_neg.
                z_start = hw_neg - state[i].thickness;
                state[i].z_start    = z_start;
                state[i].z_resolved = true;
                z_kind = std::format("accumulated downward from hw={:.1f}", hw_neg);
            }

            hw_neg = std::min(hw_neg, z_start);

            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("'{}': z=[{:.1f}, {:.1f}] nm  ({})",
                            row.name.value_or("?"), z_start, z_start + state[i].thickness,
                            z_kind),
                row.source_line
            });
        }
    }

    // ── Anchor INFO diagnostic ────────────────────────────────────────────────
    if (state[anchor_idx].has_physical) {
        const std::string kind = anchor_found
            ? "anchor \u2014 z=0 reference plane"
            : "anchor (backward-compat)";
        diags.push_back({
            GksDiagnostic::Level::INFO,
            std::format("'{}': z=[{:.1f}, {:.1f}] nm  ({})",
                        raw[anchor_idx].name.value_or("?"),
                        anchor_z, anchor_z + anchor_t, kind),
            raw[anchor_idx].source_line
        });
        z_groups[anchor_z].push_back(raw[anchor_idx].name.value_or(""));
    }

    if (anchor_found) {
        diags.push_back({
            GksDiagnostic::Level::INFO,
            std::format("SUMMARY  Anchor layer: '{}' at z={:.1f}",
                        raw[anchor_idx].name.value_or("?"), anchor_z),
            0
        });
    }

    // ── Parallel-group summary diagnostics ────────────────────────────────────
    for (const auto& [z_val, names] : z_groups) {
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

    // ── Build LayerEntry objects ───────────────────────────────────────────────
    // Build in reverse file order (N-1 downto 0) so that after the z_start sort,
    // equal-z ties are broken in the same way as the original v0.4.1 code
    // (which reversed the raw array before accumulation).  This also makes the
    // ordering stable under a TOML write→read round-trip.
    for (size_t k = 0; k < N; ++k) {
        const size_t i = N - 1 - k;
        const auto& row = raw[i];
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

        if (state[i].has_physical) {
            entry.physical = PhysicalProps{
                .z_start_nm       = state[i].z_start,
                .thickness_nm     = state[i].thickness,
                .material         = row.material.value_or(defaults.material.value_or("")),
                .layer_expression = row.layer_expression
            };
        } else {
            entry.physical = std::nullopt;
        }
        stack.layers.push_back(std::move(entry));
    }

    // Sort physical layers ascending by z_start.  Display-only layers (no physical
    // props) are treated as equivalent to all other elements — stable_sort
    // preserves their relative position among the physical layers unchanged.
    // This matches v0.4.1 behaviour where layers were accumulated in-order
    // with display-only entries interspersed in their natural file position.
    std::stable_sort(stack.layers.begin(), stack.layers.end(),
        [](const LayerEntry& a, const LayerEntry& b) {
            if (a.physical.has_value() && b.physical.has_value())
                return a.physical->z_start_nm < b.physical->z_start_nm;
            return false; // keep relative order for display-only vs anything
        });

    return result;
}

} // namespace gks
