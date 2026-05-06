#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace jsontitan::core {

enum class CliAction {
    ShowHelp,
    ShowVersion,
    OpenFiles,  // 1 or more files to open
    NoAction    // No arguments - launch normally
};

struct CliParseResult {
    CliAction action = CliAction::NoAction;
    std::vector<std::string> filePaths;  // Resolved absolute paths
    std::optional<std::string> error;    // Validation error message
};

// Pure function: parses arguments, resolves relative paths against cwd.
// argv[0] (program name) should be excluded from the input.
[[nodiscard]] auto parseCli(std::span<const std::string> args,
                            const std::string& cwd) -> CliParseResult;

// Generate help text (pure)
[[nodiscard]] auto helpText(const std::string& programName) -> std::string;

// Generate version text (pure)
[[nodiscard]] auto versionText(const std::string& version) -> std::string;

} // namespace jsontitan::core
