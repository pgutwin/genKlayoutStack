#pragma once

#include "gks/core/LayerStack.hpp"
#include <expected>
#include <string>

namespace gks {

// Implemented in Phase 5
std::expected<void, GksError> writeScript(const LayerStack& stack, const std::string& path);

} // namespace gks
