#pragma once

#include "gks/core/LayerStack.hpp"
#include <expected>
#include <string>

namespace gks {

// Implemented in Phase 3
struct TomlReadResult {
    std::string              tech_name;
    std::string              version;
    std::vector<RawLayer>    layers;
    LayerStack::Defaults     defaults;
};

std::expected<TomlReadResult, GksError> readToml(const std::string& path);

} // namespace gks
