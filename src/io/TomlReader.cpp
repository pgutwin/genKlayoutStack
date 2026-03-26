#include "gks/io/TomlReader.hpp"
#include <stdexcept>

namespace gks {

std::expected<TomlReadResult, GksError> readToml(const std::string&) {
    throw std::runtime_error("readToml: not yet implemented (Phase 3)");
}

} // namespace gks
