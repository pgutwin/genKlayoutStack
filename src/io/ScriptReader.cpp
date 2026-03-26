#include "gks/io/ScriptReader.hpp"
#include <stdexcept>

namespace gks {

std::expected<ScriptReadResult, GksError> readScript(const std::string&) {
    throw std::runtime_error("readScript: not yet implemented (Phase 5)");
}

} // namespace gks
