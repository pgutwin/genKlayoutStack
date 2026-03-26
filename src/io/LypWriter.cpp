#include "gks/io/LypWriter.hpp"
#include <stdexcept>

namespace gks {

std::expected<void, GksError> writeLyp(const LayerStack&, const std::string&) {
    throw std::runtime_error("writeLyp: not yet implemented (Phase 4)");
}

} // namespace gks
