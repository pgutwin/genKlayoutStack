#pragma once

#include "gks/core/LayerStack.hpp"
#include <expected>
#include <string>

namespace gks {

// Implemented in Phase 4
std::expected<LayerStack, GksError> readLyp(const std::string& path);

} // namespace gks
