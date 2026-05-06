#include "core/cli_parser.h"

#include <filesystem>

namespace jsontitan::core {

auto parseCli(std::span<const std::string> args,
              const std::string& cwd) -> CliParseResult {
    CliParseResult result;

    bool hasHelp = false;
    bool hasVersion = false;
    std::vector<std::string> positionalArgs;

    for (const auto& arg : args) {
        if (arg == "--help") {
            hasHelp = true;
        } else if (arg == "--version") {
            hasVersion = true;
        } else if (arg.starts_with("--")) {
            // Unknown flag — produce error immediately
            result.action = CliAction::NoAction;
            result.error = "Unknown option: " + arg;
            return result;
        } else {
            positionalArgs.push_back(arg);
        }
    }

    // --help takes priority over --version
    if (hasHelp) {
        result.action = CliAction::ShowHelp;
        return result;
    }

    if (hasVersion) {
        result.action = CliAction::ShowVersion;
        return result;
    }

    if (positionalArgs.empty()) {
        result.action = CliAction::NoAction;
        return result;
    }

    // Resolve relative paths against cwd
    result.action = CliAction::OpenFiles;
    std::filesystem::path cwdPath(cwd);
    for (const auto& arg : positionalArgs) {
        std::filesystem::path p(arg);
        if (p.is_absolute()) {
            result.filePaths.push_back(
                std::filesystem::weakly_canonical(p).string());
        } else {
            result.filePaths.push_back(
                std::filesystem::weakly_canonical(cwdPath / p).string());
        }
    }

    return result;
}

auto helpText(const std::string& programName) -> std::string {
    return "Usage: " + programName + " [options] [file ...]\n"
           "\n"
           "Options:\n"
           "  --help       Show this help message and exit\n"
           "  --version    Show version information and exit\n"
           "\n"
           "Arguments:\n"
           "  file         One or more JSON file paths to open\n"
           "\n"
           "If a single file is provided, it is opened normally.\n"
           "If multiple files are provided, they are combined in union mode.\n";
}

auto versionText(const std::string& version) -> std::string {
    return "JSONTitan " + version + "\n";
}

} // namespace jsontitan::core
