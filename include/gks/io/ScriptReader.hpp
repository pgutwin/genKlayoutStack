#pragma once

#include "gks/core/LayerStack.hpp"
#include <expected>
#include <string>

namespace gks {

// Implemented in Phase 5
struct ScriptReadResult {
    std::vector<RawLayer>      layers;
    std::vector<GksDiagnostic> diagnostics;
};

std::expected<ScriptReadResult, GksError> readScript(const std::string& path);

} // namespace gks
