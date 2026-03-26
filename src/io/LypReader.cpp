#include "gks/io/LypReader.hpp"
#include <stdexcept>

namespace gks {

std::expected<LayerStack, GksError> readLyp(const std::string&) {
    throw std::runtime_error("readLyp: not yet implemented (Phase 4)");
}

} // namespace gks
