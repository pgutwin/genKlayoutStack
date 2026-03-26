#include "gks/core/Validator.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

namespace gks {

// ─── validate_identity ────────────────────────────────────────────────────────

std::vector<GksDiagnostic> validate_identity(const LayerStack& stack) {
    std::vector<GksDiagnostic> diags;

    // Duplicate (layer_num, datatype) detection
    std::map<std::pair<int,int>, std::size_t> seen; // key → first-occurrence index
    for (std::size_t i = 0; i < stack.layers.size(); ++i) {
        const auto& e = stack.layers[i];
        auto key = std::make_pair(e.layer_num, e.datatype);
        auto [it, inserted] = seen.emplace(key, i);
        if (!inserted) {
            diags.push_back({
                GksDiagnostic::Level::ERROR,
                std::format("duplicate (layer_num={}, datatype={}) at index {} "
                            "— first seen at index {}",
                            e.layer_num, e.datatype, i, it->second),
                0
            });
        }
    }

    // Blank name check [info]
    for (const auto& e : stack.layers) {
        if (e.name.empty()) {
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("layer ({}/{}) has an empty name — fill in before generating output",
                            e.layer_num, e.datatype),
                0
            });
        }
    }

    // datatype == 0 note [info]
    for (const auto& e : stack.layers) {
        if (e.datatype == 0) {
            diags.push_back({
                GksDiagnostic::Level::INFO,
                std::format("layer '{}' ({}/{}) has datatype=0 (explicit or default)",
                            e.name, e.layer_num, e.datatype),
                0
            });
        }
    }

    return diags;
}

// ─── validate_for_lyp ────────────────────────────────────────────────────────

std::vector<GksDiagnostic> validate_for_lyp(const LayerStack& stack) {
    std::vector<GksDiagnostic> diags;

    for (const auto& e : stack.layers) {
        const auto& d = e.display;

        if (d.dither_pattern < -1) {
            diags.push_back({
                GksDiagnostic::Level::ERROR,
                std::format("layer '{}' ({}/{}): dither_pattern={} is invalid (must be >= -1)",
                            e.name, e.layer_num, e.datatype, d.dither_pattern),
                0
            });
        }

        if (d.line_style < -1) {
            diags.push_back({
                GksDiagnostic::Level::ERROR,
                std::format("layer '{}' ({}/{}): line_style={} is invalid (must be >= -1)",
                            e.name, e.layer_num, e.datatype, d.line_style),
                0
            });
        }
        // fill_alpha is uint8_t — always in [0,255]; no check needed
    }

    return diags;
}

// ─── validate_for_3d ─────────────────────────────────────────────────────────

std::vector<GksDiagnostic> validate_for_3d(const LayerStack& stack) {
    std::vector<GksDiagnostic> diags;

    constexpr double kEpsilon = 1e-9;

    for (const auto& e : stack.layers) {
        if (!e.physical.has_value()) {
            diags.push_back({
                GksDiagnostic::Level::ERROR,
                std::format("layer '{}' ({}/{}): missing PhysicalProps — required for 3D output",
                            e.name, e.layer_num, e.datatype),
                0
            });
            continue;
        }
        if (e.physical->thickness_nm < 0.0) {
            diags.push_back({
                GksDiagnostic::Level::ERROR,
                std::format("layer '{}' ({}/{}): thickness_nm={:.3f} is negative",
                            e.name, e.layer_num, e.datatype, e.physical->thickness_nm),
                0
            });
        }
    }

    // Overlapping z ranges: same material, non-parallel layers.
    // Parallel = sharing the same z_start_nm (multiple layers at same z).
    std::map<double, int> z_count;
    for (const auto& e : stack.layers) {
        if (e.physical) z_count[e.physical->z_start_nm]++;
    }
    std::set<double> parallel_z;
    for (const auto& [z, cnt] : z_count) {
        if (cnt > 1) parallel_z.insert(z);
    }

    const auto& layers = stack.layers;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (!layers[i].physical) continue;
        const auto& pi = *layers[i].physical;

        for (std::size_t j = i + 1; j < layers.size(); ++j) {
            if (!layers[j].physical) continue;
            const auto& pj = *layers[j].physical;

            // Skip pairs that share z_start (parallel group — not an error)
            if (std::abs(pi.z_start_nm - pj.z_start_nm) < kEpsilon) continue;

            // Only flag same-material overlaps; ignore layers with no material
            if (pi.material != pj.material || pi.material.empty()) continue;

            double i_top = pi.z_start_nm + pi.thickness_nm;
            double j_top = pj.z_start_nm + pj.thickness_nm;
            double overlap_lo = std::max(pi.z_start_nm, pj.z_start_nm);
            double overlap_hi = std::min(i_top, j_top);

            if (overlap_hi > overlap_lo + kEpsilon) {
                diags.push_back({
                    GksDiagnostic::Level::WARN,
                    std::format("layers '{}' and '{}' overlap in z "
                                "[{:.1f},{:.1f}]nm ∩ [{:.1f},{:.1f}]nm "
                                "with same material '{}'",
                                layers[i].name, layers[j].name,
                                pi.z_start_nm, i_top,
                                pj.z_start_nm, j_top,
                                pi.material),
                    0
                });
            }
        }
    }

    return diags;
}

// ─── validate_full ────────────────────────────────────────────────────────────

std::vector<GksDiagnostic> validate_full(const LayerStack& stack) {
    auto diags = validate_identity(stack);
    auto lyp   = validate_for_lyp(stack);
    auto td    = validate_for_3d(stack);
    diags.insert(diags.end(), lyp.begin(), lyp.end());
    diags.insert(diags.end(), td.begin(),  td.end());
    return diags;
}

} // namespace gks
