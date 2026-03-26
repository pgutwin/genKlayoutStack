#include "gks/io/ScriptWriter.hpp"
#include <stdexcept>

namespace gks {

std::expected<void, GksError> writeScript(const LayerStack&, const std::string&) {
    throw std::runtime_error("writeScript: not yet implemented (Phase 5)");
}

} // namespace gks
