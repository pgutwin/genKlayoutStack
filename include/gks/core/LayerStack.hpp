#pragma once

#include <cstdint>
// std::expected is C++23; provide it from tl::expected on C++20
#if defined(__cpp_lib_expected)
#  include <expected>
#else
#  include <tl/expected.hpp>
   namespace std {
       template<class T, class E> using expected   = tl::expected<T, E>;
       template<class E>          using unexpected = tl::unexpected<E>;
   }
#endif
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gks {

// ─── GksError ─────────────────────────────────────────────────────────────────

struct GksError {
    std::string message;
    int         source_line = 0;
};

// ─── GksDiagnostic ────────────────────────────────────────────────────────────

struct GksDiagnostic {
    enum class Level { INFO, WARN, ERROR };
    Level       level;
    std::string message;
    int         source_line = 0;
};

// ─── Color ────────────────────────────────────────────────────────────────────

struct Color {
    uint8_t r = 0x80;
    uint8_t g = 0x80;
    uint8_t b = 0x80;

    // Accepts "#rrggbb" (lyp/TOML) or "0xrrggbb" (script)
    static std::expected<Color, std::string> fromHex(std::string_view s);

    std::string toHex() const;   // returns "#rrggbb"
    uint32_t    toInt() const;   // returns 0x00rrggbb

    bool operator==(const Color&) const = default;
};

// ─── DisplayProps ─────────────────────────────────────────────────────────────

struct DisplayProps {
    // TOML-exposed fields
    Color   fill_color    = {0x80, 0x80, 0x80};
    Color   frame_color   = {0x80, 0x80, 0x80};
    uint8_t fill_alpha    = 128;
    int     dither_pattern = -1;
    int     line_style     = -1;
    bool    visible       = true;
    bool    valid         = true;
    bool    transparent   = false;

    // Passthrough fields (not in TOML; round-tripped for .lyp fidelity)
    int                fill_brightness  = 0;
    int                frame_brightness = 0;
    std::optional<int> width;
    bool               marked    = false;
    bool               xfill     = false;
    int                animation = 0;
    bool               expanded  = false;

    bool operator==(const DisplayProps&) const = default;
};

// ─── PhysicalProps ────────────────────────────────────────────────────────────

struct PhysicalProps {
    double      z_start_nm;
    double      thickness_nm;
    std::string material;
    std::optional<std::string> layer_expression;

    bool operator==(const PhysicalProps&) const = default;
};

// ─── LayerEntry ───────────────────────────────────────────────────────────────

struct LayerEntry {
    int         layer_num;
    int         datatype;
    std::string name;
    std::string purpose;

    DisplayProps                 display;
    std::optional<PhysicalProps> physical;

    bool operator==(const LayerEntry&) const = default;
};

// ─── LayerStack ───────────────────────────────────────────────────────────────

struct LayerStack {
    std::string             tech_name;
    std::string             version;
    std::vector<LayerEntry> layers;

    struct Defaults {
        std::optional<double>      thickness_nm;
        std::optional<uint8_t>     fill_alpha;
        std::optional<std::string> material;
        std::optional<int>         dither_pattern;
        std::optional<int>         line_style;
    } defaults;
};

// ─── RawLayer ─────────────────────────────────────────────────────────────────

struct RawLayer {
    std::optional<int>         layer_num;
    std::optional<int>         datatype;
    std::optional<std::string> name;
    std::optional<std::string> purpose;

    // DisplayProps — TOML-exposed subset
    std::optional<Color>   fill_color;
    std::optional<Color>   frame_color;
    std::optional<uint8_t> fill_alpha;
    std::optional<int>     dither_pattern;
    std::optional<int>     line_style;
    std::optional<bool>    visible;
    std::optional<bool>    valid;
    std::optional<bool>    transparent;

    // PhysicalProps
    std::optional<double>      z_start_nm;       // absent → accumulate; 0.0 → z anchor
    std::optional<double>      thickness_nm;
    std::optional<std::string> material;
    std::optional<std::string> layer_expression;

    // Alignment (v0.4.2) — format: "layer_name:top" or "layer_name:bottom"
    // align_bottom_to: snaps the BOTTOM of this layer to the named edge
    // align_top_to:    snaps the TOP    of this layer to the named edge
    // Mutually exclusive with explicit z_start_nm (ERROR if both present).
    std::optional<std::string> align_bottom_to;
    std::optional<std::string> align_top_to;

    int source_line = 0;
};

// ─── BuildResult ──────────────────────────────────────────────────────────────

struct BuildResult {
    LayerStack                 stack;
    std::vector<GksDiagnostic> diagnostics;
};

// ─── buildStack ───────────────────────────────────────────────────────────────

/// Build a LayerStack IR from raw parsed layer data.
/// Resolves z_start_nm via hybrid accumulation (see spec Section 5.2).
/// Emits INFO for each physical layer, WARN for burial/gap anomalies,
/// INFO for parallel group detection.
BuildResult buildStack(std::string             tech_name,
                       std::string             version,
                       const std::vector<RawLayer>& raw,
                       const LayerStack::Defaults&  defaults = {});

} // namespace gks
