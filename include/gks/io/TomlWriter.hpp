#pragma once

#include "gks/core/LayerStack.hpp"
#include <expected>
#include <string>

namespace gks {

// Implemented in Phase 3
std::expected<void, GksError> writeToml(const LayerStack& stack, const std::string& path);

} // namespace gks
