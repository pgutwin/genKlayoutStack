#include <iostream>
#include <stdexcept>

// CLI implemented in Phase 6
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "gks: no subcommand given\n";
        std::cerr << "Usage: gks <import|generate|validate> [options]\n";
        return 1;
    }
    std::cerr << "gks CLI: not yet implemented (Phase 6)\n";
    return 1;
}
