#include "gks/version.hpp"

#include <iostream>
#include <string>

// Forward declarations of subcommand entry points
int runImport(int argc, char* argv[]);
int runGenerate(int argc, char* argv[]);
int runValidate(int argc, char* argv[]);

static void printUsage() {
    std::cout << "Usage: gks <subcommand> [options]\n\n";
    std::cout << "Subcommands:\n";
    std::cout << "  import    Create a TOML stack definition from .lyp and/or 2.5D script\n";
    std::cout << "  generate  Generate .lyp and/or 2.5D script from a TOML stack definition\n";
    std::cout << "  validate  Validate a TOML stack definition\n\n";
    std::cout << "Options by subcommand:\n";
    std::cout << "  import:   [--lyp <f>] [--script <f>] -o <out.toml>\n";
    std::cout << "            [--prefer-script-colors] [--verbose]\n";
    std::cout << "  generate: <stack.toml> [--lyp <f>] [--script <f>] [--verbose]\n";
    std::cout << "  validate: <stack.toml> [--for lyp|3d|full] [--verbose]\n\n";
    std::cout << "Global options:\n";
    std::cout << "  --version, -V  Print version and exit\n";
    std::cout << "  --help, -h     Print this help and exit\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "gks: no subcommand given\n";
        printUsage();
        return 1;
    }

    std::string subcmd = argv[1];

    if (subcmd == "--version" || subcmd == "-V") {
        std::cout << "gks v" << gks::VERSION_STRING << "\n";
        return 0;
    }

    // Pass remaining args (skip argv[0]=program, argv[1]=subcommand)
    int sub_argc = argc - 2;
    char** sub_argv = argv + 2;

    if (subcmd == "import") {
        return runImport(sub_argc, sub_argv);
    } else if (subcmd == "generate") {
        return runGenerate(sub_argc, sub_argv);
    } else if (subcmd == "validate") {
        return runValidate(sub_argc, sub_argv);
    } else if (subcmd == "--help" || subcmd == "-h" || subcmd == "help") {
        printUsage();
        return 0;
    } else {
        std::cerr << "gks: unknown subcommand '" << subcmd << "'\n";
        printUsage();
        return 1;
    }
}
