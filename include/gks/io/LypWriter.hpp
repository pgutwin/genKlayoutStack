#pragma once

#include "gks/core/LayerStack.hpp"
#include <expected>
#include <string>

namespace gks {

// Implemented in Phase 4
std::expected<void, GksError> writeLyp(const LayerStack& stack, const std::string& path);

} // namespace gks
