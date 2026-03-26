#include "gks/io/TomlWriter.hpp"
#include <stdexcept>

namespace gks {

std::expected<void, GksError> writeToml(const LayerStack&, const std::string&) {
    throw std::runtime_error("writeToml: not yet implemented (Phase 3)");
}

} // namespace gks
