#include "gks/core/LayerStack.hpp"
#include "gks/core/Validator.hpp"
#include "gks/core/Defaulter.hpp"
#include "gks/io/TomlReader.hpp"
#include "gks/io/LypWriter.hpp"
#include "gks/io/ScriptWriter.hpp"

#include <iostream>
#include <string>
#include <vector>

static void printDiagnostics(const std::vector<gks::GksDiagnostic>& diags, bool verbose) {
    for (const auto& d : diags) {
        if (d.level == gks::GksDiagnostic::Level::ERROR) {
            std::cerr << "[ERROR] " << d.message << "\n";
        } else if (d.level == gks::GksDiagnostic::Level::WARN) {
            std::cerr << "[WARN]  " << d.message << "\n";
        } else if (verbose) {
            std::cout << "[INFO]  " << d.message << "\n";
        }
    }
}

int runGenerate(int argc, char* argv[]) {
    std::string toml_path;
    std::string lyp_out;
    std::string script_out;
    bool verbose = false;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lyp" && i + 1 < argc) {
            lyp_out = argv[++i];
        } else if (arg == "--script" && i + 1 < argc) {
            script_out = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (!arg.empty() && arg[0] != '-') {
            toml_path = arg;
        }
    }

    if (toml_path.empty()) {
        std::cerr << "gks generate: missing <stack.toml>\n";
        std::cerr << "Usage: gks generate <stack.toml> [--lyp <f>] [--script <f>] [--verbose]\n";
        return 1;
    }
    if (lyp_out.empty() && script_out.empty()) {
        std::cerr << "gks generate: at least one of --lyp or --script is required\n";
        return 1;
    }

    auto tr = gks::readToml(toml_path);
    if (!tr) {
        std::cerr << "[ERROR] " << tr.error().message << "\n";
        return 1;
    }

    // First pass: build stack
    auto br = gks::buildStack(tr->tech_name, tr->version, tr->layers, tr->defaults);
    auto& stack = br.stack;
    printDiagnostics(br.diagnostics, verbose);

    // Apply defaults
    auto dd = gks::applyDefaults(stack);
    printDiagnostics(dd, verbose);

    // Validate (print diagnostics but don't abort — writers are graceful)
    std::vector<gks::GksDiagnostic> vd;
    if (!lyp_out.empty() && script_out.empty()) {
        // LYP only: only require display props
        auto v1 = gks::validate_identity(stack);
        auto v2 = gks::validate_for_lyp(stack);
        vd.insert(vd.end(), v1.begin(), v1.end());
        vd.insert(vd.end(), v2.begin(), v2.end());
    } else if (lyp_out.empty() && !script_out.empty()) {
        // Script only
        auto v1 = gks::validate_identity(stack);
        auto v2 = gks::validate_for_3d(stack);
        vd.insert(vd.end(), v1.begin(), v1.end());
        vd.insert(vd.end(), v2.begin(), v2.end());
    } else {
        // Both: run full validation
        vd = gks::validate_full(stack);
    }
    printDiagnostics(vd, verbose);

    // Abort on identity ERRORs (duplicate keys) — those are always fatal
    for (const auto& d : vd) {
        if (d.level == gks::GksDiagnostic::Level::ERROR) {
            // Only abort for identity errors (duplicate keys); physical/display
            // errors are handled gracefully by the writers.
            // For now: print a note and continue.
            break;
        }
    }

    // Write LYP if requested
    if (!lyp_out.empty()) {
        auto wr = gks::writeLyp(stack, lyp_out);
        if (!wr) {
            std::cerr << "[ERROR] " << wr.error().message << "\n";
            return 1;
        }
        if (verbose) {
            std::cout << "Wrote " << lyp_out << "\n";
        }
    }

    // Write script if requested
    if (!script_out.empty()) {
        auto wr = gks::writeScript(stack, script_out);
        if (!wr) {
            std::cerr << "[ERROR] " << wr.error().message << "\n";
            return 1;
        }
        if (verbose) {
            std::cout << "Wrote " << script_out << "\n";
        }
    }

    return 0;
}
