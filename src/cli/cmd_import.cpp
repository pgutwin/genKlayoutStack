#include "gks/core/LayerStack.hpp"
#include "gks/core/Validator.hpp"
#include "gks/io/LypReader.hpp"
#include "gks/io/ScriptReader.hpp"
#include "gks/io/TomlWriter.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
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

int runImport(int argc, char* argv[]) {
    std::string lyp_path;
    std::string script_path;
    std::string out_path;
    bool prefer_script_colors = false;
    bool verbose = false;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lyp" && i + 1 < argc) {
            lyp_path = argv[++i];
        } else if (arg == "--script" && i + 1 < argc) {
            script_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--prefer-script-colors") {
            prefer_script_colors = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        }
    }

    if (lyp_path.empty() && script_path.empty()) {
        std::cerr << "gks import: at least one of --lyp or --script is required\n";
        std::cerr << "Usage: gks import [--lyp <f>] [--script <f>] -o <out.toml>"
                     " [--prefer-script-colors] [--verbose]\n";
        return 1;
    }
    if (out_path.empty()) {
        std::cerr << "gks import: -o <output.toml> is required\n";
        return 1;
    }

    gks::LayerStack stack;

    // Step 1: read .lyp if provided
    if (!lyp_path.empty()) {
        auto lr = gks::readLyp(lyp_path);
        if (!lr) {
            std::cerr << "[ERROR] " << lr.error().message << "\n";
            return 1;
        }
        stack = std::move(*lr);
    }

    // Step 2: read script if provided and merge into stack
    if (!script_path.empty()) {
        auto sr = gks::readScript(script_path);
        if (!sr) {
            std::cerr << "[ERROR] " << sr.error().message << "\n";
            return 1;
        }
        printDiagnostics(sr->diagnostics, verbose);

        if (!lyp_path.empty()) {
            // Merge script physical/name data into lyp-derived stack
            std::set<std::pair<int, int>> script_keys;
            for (const auto& raw : sr->layers) {
                int ln = raw.layer_num.value_or(-1);
                int dt = raw.datatype.value_or(0);
                script_keys.insert({ln, dt});

                auto it = std::find_if(stack.layers.begin(), stack.layers.end(),
                    [&](const gks::LayerEntry& e) {
                        return e.layer_num == ln && e.datatype == dt;
                    });

                if (it == stack.layers.end()) {
                    std::cerr << "[WARN]  Script layer " << ln << "/" << dt
                              << " not found in .lyp — skipping\n";
                    continue;
                }

                // Populate name from script if present and non-empty
                if (raw.name.has_value() && !raw.name->empty()) {
                    it->name = *raw.name;
                }

                // Optionally override display colors from script
                if (prefer_script_colors) {
                    if (raw.fill_color)  it->display.fill_color  = *raw.fill_color;
                    if (raw.frame_color) it->display.frame_color = *raw.frame_color;
                }

                // Set physical props from script
                if (raw.z_start_nm.has_value() || raw.thickness_nm.has_value()
                        || raw.material.has_value()) {
                    gks::PhysicalProps phys;
                    phys.z_start_nm    = raw.z_start_nm.value_or(0.0);
                    phys.thickness_nm  = raw.thickness_nm.value_or(0.0);
                    phys.material      = raw.material.value_or("");
                    phys.layer_expression = raw.layer_expression;
                    it->physical = phys;
                }
            }

            // Warn about .lyp layers not present in script (verbose / info)
            for (const auto& entry : stack.layers) {
                if (script_keys.find({entry.layer_num, entry.datatype}) == script_keys.end()) {
                    if (verbose) {
                        std::cout << "[INFO]  .lyp layer " << entry.layer_num
                                  << "/" << entry.datatype << " has no matching script entry\n";
                    }
                }
            }
        } else {
            // Script-only import: build stack from script layers
            for (const auto& raw : sr->layers) {
                gks::LayerEntry entry;
                entry.layer_num = raw.layer_num.value_or(0);
                entry.datatype  = raw.datatype.value_or(0);
                entry.name      = raw.name.value_or("");
                entry.purpose   = raw.purpose.value_or("drawing");
                if (raw.fill_color)  entry.display.fill_color  = *raw.fill_color;
                if (raw.frame_color) entry.display.frame_color = *raw.frame_color;
                if (raw.z_start_nm.has_value() || raw.thickness_nm.has_value()
                        || raw.material.has_value()) {
                    gks::PhysicalProps phys;
                    phys.z_start_nm    = raw.z_start_nm.value_or(0.0);
                    phys.thickness_nm  = raw.thickness_nm.value_or(0.0);
                    phys.material      = raw.material.value_or("");
                    phys.layer_expression = raw.layer_expression;
                    entry.physical = phys;
                }
                stack.layers.push_back(std::move(entry));
            }
        }
    }

    // Step 3: validate identity (duplicate keys, blank names)
    auto vi = gks::validate_identity(stack);
    printDiagnostics(vi, verbose);

    // Warn on blank names
    for (const auto& entry : stack.layers) {
        if (entry.name.empty()) {
            std::cerr << "[WARN]  Layer " << entry.layer_num << "/" << entry.datatype
                      << " has blank name — fill in before generating output\n";
        }
    }

    // Step 4: write TOML
    auto wr = gks::writeToml(stack, out_path);
    if (!wr) {
        std::cerr << "[ERROR] " << wr.error().message << "\n";
        return 1;
    }

    std::cout << "Wrote " << out_path << "\n";
    return 0;
}
