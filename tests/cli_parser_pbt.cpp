// ---------------------------------------------------------------------------
// Property-Based Tests for CliParser
// Feature: cli-file-open
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include "core/cli_parser.h"

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace jsontitan::core;

namespace {

// ---------------------------------------------------------------------------
// RapidCheck Generators
// ---------------------------------------------------------------------------

// Generate a path segment: alphanumeric + underscores, length 1-10
auto genPathSegment() -> rc::Gen<std::string> {
    return rc::gen::exec([]() -> std::string {
        auto len = *rc::gen::inRange(1, 11);
        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; ++i) {
            s += *rc::gen::oneOf(
                rc::gen::inRange<char>('a', 'z' + 1),
                rc::gen::inRange<char>('0', '9' + 1),
                rc::gen::just('_')
            );
        }
        return s;
    });
}

// Generate a relative file path (e.g., "dir/subdir/file.json")
// Does NOT start with "--"
auto genRelativePath() -> rc::Gen<std::string> {
    return rc::gen::exec([]() -> std::string {
        auto segmentCount = *rc::gen::inRange(1, 4);
        std::string path;
        for (int i = 0; i < segmentCount; ++i) {
            if (i > 0) path += '/';
            path += *genPathSegment();
        }
        path += ".json";
        return path;
    });
}

// Generate a positional argument: a string that does NOT start with "--"
// Uses path-like strings to be realistic
auto genPositionalArg() -> rc::Gen<std::string> {
    return genRelativePath();
}

// Generate a valid absolute directory path for use as cwd
auto genCwd() -> rc::Gen<std::string> {
    return rc::gen::exec([]() -> std::string {
        auto segmentCount = *rc::gen::inRange(1, 4);
        std::string path = "/";
        for (int i = 0; i < segmentCount; ++i) {
            if (i > 0) path += '/';
            path += *genPathSegment();
        }
        return path;
    });
}

// Generate a list of non-flag arguments (1 to 5 items)
auto genPositionalArgs() -> rc::Gen<std::vector<std::string>> {
    return rc::gen::exec([]() -> std::vector<std::string> {
        auto count = *rc::gen::inRange(1, 6);
        std::vector<std::string> args;
        args.reserve(count);
        for (int i = 0; i < count; ++i) {
            args.push_back(*genPositionalArg());
        }
        return args;
    });
}

// Generate a random argument list that contains --help somewhere
// May also contain positional args and --version
auto genArgsWithHelp() -> rc::Gen<std::vector<std::string>> {
    return rc::gen::exec([]() -> std::vector<std::string> {
        std::vector<std::string> args;
        // Add some random positional args
        auto preCount = *rc::gen::inRange(0, 4);
        for (int i = 0; i < preCount; ++i) {
            args.push_back(*genPositionalArg());
        }
        // Insert --help
        args.push_back("--help");
        // Maybe add --version too
        if (*rc::gen::arbitrary<bool>()) {
            args.push_back("--version");
        }
        // Add some more positional args after
        auto postCount = *rc::gen::inRange(0, 3);
        for (int i = 0; i < postCount; ++i) {
            args.push_back(*genPositionalArg());
        }
        // Shuffle to randomize position of --help
        auto seed = *rc::gen::arbitrary<unsigned int>();
        std::mt19937 rng(seed);
        std::shuffle(args.begin(), args.end(), rng);
        return args;
    });
}

// Generate a random argument list that contains --version but NOT --help
auto genArgsWithVersionNoHelp() -> rc::Gen<std::vector<std::string>> {
    return rc::gen::exec([]() -> std::vector<std::string> {
        std::vector<std::string> args;
        // Add some random positional args
        auto preCount = *rc::gen::inRange(0, 4);
        for (int i = 0; i < preCount; ++i) {
            args.push_back(*genPositionalArg());
        }
        // Insert --version
        args.push_back("--version");
        // Add some more positional args after
        auto postCount = *rc::gen::inRange(0, 3);
        for (int i = 0; i < postCount; ++i) {
            args.push_back(*genPositionalArg());
        }
        // Shuffle to randomize position of --version
        auto seed = *rc::gen::arbitrary<unsigned int>();
        std::mt19937 rng(seed);
        std::shuffle(args.begin(), args.end(), rng);
        return args;
    });
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Feature: cli-file-open, Property 1: Positional arguments become file paths
// Validates: Requirements 1.1, 2.1, 3.1
// ---------------------------------------------------------------------------

TEST(CliParserProperty, PositionalArgsBecomeFilePaths) {
    rc::check("Feature: cli-file-open, Property 1: Positional arguments become file paths",
        [](void) {
            auto args = *genPositionalArgs();
            auto cwd = *genCwd();

            auto result = parseCli(args, cwd);

            // Action must be OpenFiles
            RC_ASSERT(result.action == CliAction::OpenFiles);

            // Must have same number of file paths as input args
            RC_ASSERT(result.filePaths.size() == args.size());

            // Order must be preserved: each resolved path must end with
            // the original argument's filename component
            for (std::size_t i = 0; i < args.size(); ++i) {
                std::filesystem::path inputPath(args[i]);
                std::filesystem::path resolvedPath(result.filePaths[i]);

                // The resolved path must be absolute
                RC_ASSERT(resolvedPath.is_absolute());

                // The filename component must match
                RC_ASSERT(resolvedPath.filename() == inputPath.filename());
            }

            // No error should be set
            RC_ASSERT(!result.error.has_value());
        }
    );
}

// ---------------------------------------------------------------------------
// Feature: cli-file-open, Property 2: Help flag always produces ShowHelp
// Validates: Requirements 3.2
// ---------------------------------------------------------------------------

TEST(CliParserProperty, HelpFlagAlwaysProducesShowHelp) {
    rc::check("Feature: cli-file-open, Property 2: Help flag always produces ShowHelp",
        [](void) {
            auto args = *genArgsWithHelp();
            std::string cwd = "/tmp";

            auto result = parseCli(args, cwd);

            // --help takes priority over everything
            RC_ASSERT(result.action == CliAction::ShowHelp);
        }
    );
}

// ---------------------------------------------------------------------------
// Feature: cli-file-open, Property 3: Version flag always produces ShowVersion
// Validates: Requirements 3.3
// ---------------------------------------------------------------------------

TEST(CliParserProperty, VersionFlagAlwaysProducesShowVersion) {
    rc::check("Feature: cli-file-open, Property 3: Version flag always produces ShowVersion",
        [](void) {
            auto args = *genArgsWithVersionNoHelp();
            std::string cwd = "/tmp";

            auto result = parseCli(args, cwd);

            // --version produces ShowVersion when --help is NOT present
            RC_ASSERT(result.action == CliAction::ShowVersion);
        }
    );
}

// ---------------------------------------------------------------------------
// Feature: cli-file-open, Property 4: Relative path resolution
// Validates: Requirements 3.5
// ---------------------------------------------------------------------------

TEST(CliParserProperty, RelativePathResolution) {
    rc::check("Feature: cli-file-open, Property 4: Relative path resolution",
        [](void) {
            auto relativePath = *genRelativePath();
            auto cwd = *genCwd();

            std::vector<std::string> args = {relativePath};
            auto result = parseCli(args, cwd);

            RC_ASSERT(result.action == CliAction::OpenFiles);
            RC_ASSERT(result.filePaths.size() == 1);

            // The resolved path should equal weakly_canonical(cwd / relativePath)
            std::filesystem::path expected =
                std::filesystem::weakly_canonical(
                    std::filesystem::path(cwd) / std::filesystem::path(relativePath));

            RC_ASSERT(result.filePaths[0] == expected.string());

            // Must be absolute
            RC_ASSERT(std::filesystem::path(result.filePaths[0]).is_absolute());

            // Must not contain "." or ".." segments
            for (const auto& component : std::filesystem::path(result.filePaths[0])) {
                RC_ASSERT(component != ".");
                RC_ASSERT(component != "..");
            }
        }
    );
}
