#include "gks/core/LayerStack.hpp"
#include "gks/core/Validator.hpp"
#include "gks/core/Defaulter.hpp"
#include "gks/io/TomlReader.hpp"

#include <iostream>
#include <string>
#include <vector>

// Print diagnostics; returns true if any ERROR-level diagnostic was present.
static bool printDiagnostics(const std::vector<gks::GksDiagnostic>& diags, bool verbose) {
    bool has_error = false;
    for (const auto& d : diags) {
        if (d.level == gks::GksDiagnostic::Level::ERROR) {
            has_error = true;
            std::cerr << "[ERROR] " << d.message << "\n";
        } else if (d.level == gks::GksDiagnostic::Level::WARN) {
            std::cerr << "[WARN]  " << d.message << "\n";
        } else if (verbose) {
            std::cout << "[INFO]  " << d.message << "\n";
        }
    }
    return has_error;
}

int runValidate(int argc, char* argv[]) {
    std::string toml_path;
    std::string for_mode = "full";
    bool verbose = false;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--for" && i + 1 < argc) {
            for_mode = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (!arg.empty() && arg[0] != '-') {
            toml_path = arg;
        }
    }

    if (toml_path.empty()) {
        std::cerr << "gks validate: missing <stack.toml>\n";
        std::cerr << "Usage: gks validate <stack.toml> [--for lyp|3d|full] [--verbose]\n";
        return 1;
    }
    if (for_mode != "lyp" && for_mode != "3d" && for_mode != "full") {
        std::cerr << "gks validate: --for must be lyp, 3d, or full (got '" << for_mode << "')\n";
        return 1;
    }

    auto tr = gks::readToml(toml_path);
    if (!tr) {
        std::cerr << "[ERROR] " << tr.error().message << "\n";
        return 1;
    }

    auto br = gks::buildStack(tr->tech_name, tr->version, tr->layers, tr->defaults);
    auto& stack = br.stack;
    bool has_error = printDiagnostics(br.diagnostics, verbose);

    auto dd = gks::applyDefaults(stack);
    has_error |= printDiagnostics(dd, verbose);

    std::vector<gks::GksDiagnostic> vd;
    if (for_mode == "lyp") {
        auto v1 = gks::validate_identity(stack);
        auto v2 = gks::validate_for_lyp(stack);
        vd.insert(vd.end(), v1.begin(), v1.end());
        vd.insert(vd.end(), v2.begin(), v2.end());
    } else if (for_mode == "3d") {
        auto v1 = gks::validate_identity(stack);
        auto v2 = gks::validate_for_3d(stack);
        vd.insert(vd.end(), v1.begin(), v1.end());
        vd.insert(vd.end(), v2.begin(), v2.end());
    } else {
        vd = gks::validate_full(stack);
    }
    has_error |= printDiagnostics(vd, verbose);

    if (!has_error) {
        std::cout << "OK  " << toml_path << "\n";
    }

    return has_error ? 1 : 0;
}
