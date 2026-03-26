#pragma once

#include "gks/core/LayerStack.hpp"
#include <vector>

namespace gks {

// Implemented in Phase 2
// Apply LayerStack::Defaults to entries missing fields; log each substitution.
std::vector<GksDiagnostic> applyDefaults(LayerStack& stack);

} // namespace gks
