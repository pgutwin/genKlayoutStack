#pragma once

#include "gks/core/LayerStack.hpp"
#include <vector>

namespace gks {

// Implemented in Phase 2
std::vector<GksDiagnostic> validate_identity(const LayerStack& stack);
std::vector<GksDiagnostic> validate_for_lyp(const LayerStack& stack);
std::vector<GksDiagnostic> validate_for_3d(const LayerStack& stack);
std::vector<GksDiagnostic> validate_full(const LayerStack& stack);

} // namespace gks
