#include "gks/core/LayerStack.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <stdexcept>
#include <string>

namespace gks {

std::expected<Color, std::string> Color::fromHex(std::string_view s) {
    // Accept "#rrggbb" or "0xrrggbb" (case-insensitive)
    std::string_view hex = s;
    if (hex.starts_with('#')) {
        hex.remove_prefix(1);
    } else if (hex.size() >= 2 &&
               hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex.remove_prefix(2);
    } else {
        return std::unexpected(std::string("Color::fromHex: expected '#rrggbb' or '0xrrggbb', got '") +
                               std::string(s) + "'");
    }

    if (hex.size() != 6) {
        return std::unexpected(std::string("Color::fromHex: expected 6 hex digits, got '") +
                               std::string(s) + "'");
    }

    auto parseTwo = [](std::string_view two) -> std::expected<uint8_t, std::string> {
        // Use a mutable buffer to lowercase for parsing
        char buf[2] = { static_cast<char>(std::tolower(static_cast<unsigned char>(two[0]))),
                        static_cast<char>(std::tolower(static_cast<unsigned char>(two[1]))) };
        uint8_t val = 0;
        auto [ptr, ec] = std::from_chars(buf, buf + 2, val, 16);
        if (ec != std::errc{} || ptr != buf + 2) {
            return std::unexpected(std::string("Color::fromHex: invalid hex digits '") +
                                   std::string(two) + "'");
        }
        return val;
    };

    auto r = parseTwo(hex.substr(0, 2));
    if (!r) return std::unexpected(r.error());
    auto g = parseTwo(hex.substr(2, 2));
    if (!g) return std::unexpected(g.error());
    auto b = parseTwo(hex.substr(4, 2));
    if (!b) return std::unexpected(b.error());

    return Color{*r, *g, *b};
}

std::string Color::toHex() const {
    return std::format("#{:02x}{:02x}{:02x}", r, g, b);
}

uint32_t Color::toInt() const {
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) <<  8) |
           static_cast<uint32_t>(b);
}

} // namespace gks
