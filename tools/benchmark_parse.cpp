// tools/benchmark_parse.cpp
// Standalone benchmark harness for comparing JSON parsing backends.
// Measures wall-clock time, peak RSS, and computes statistics.
//
// Validates: Requirements 4.1, 4.3, 4.4, 4.5

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <span>
#include <string>
#include <vector>

#ifdef __linux__
// /proc/self/status is read via std::ifstream (included above)
#endif

#ifdef __APPLE__
#include <sys/resource.h>
#endif

#include "core/parse_orchestrator.h"

namespace {

// ============================================================================
// Statistics
// ============================================================================

auto computeMean(std::span<const double> values) -> double {
    if (values.empty()) return 0.0;
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

auto computeMedian(std::span<const double> values) -> double {
    if (values.empty()) return 0.0;
    std::vector<double> sorted(values.begin(), values.end());
    std::ranges::sort(sorted);
    auto n = sorted.size();
    if (n % 2 == 0) {
        return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
    }
    return sorted[n / 2];
}

auto computeStddev(std::span<const double> values) -> double {
    if (values.size() < 2) return 0.0;
    double mean = computeMean(values);
    double sumSqDiff = 0.0;
    for (double v : values) {
        double diff = v - mean;
        sumSqDiff += diff * diff;
    }
    return std::sqrt(sumSqDiff / static_cast<double>(values.size()));
}

// ============================================================================
// Peak RSS measurement (platform-specific)
// ============================================================================

auto getPeakRssBytes() -> std::size_t {
#ifdef __linux__
    // Read VmHWM from /proc/self/status
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("VmHWM:")) {
            // Format: "VmHWM:    <value> kB"
            std::size_t kb = 0;
            auto pos = line.find_first_of("0123456789");
            if (pos != std::string::npos) {
                kb = std::stoull(line.substr(pos));
            }
            return kb * 1024; // Convert kB to bytes
        }
    }
    return 0;
#elif defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // On macOS, ru_maxrss is in bytes
        return static_cast<std::size_t>(usage.ru_maxrss);
    }
    return 0;
#else
    return 0; // Unsupported platform
#endif
}

// ============================================================================
// Benchmark result and configuration
// ============================================================================

struct BenchmarkResult {
    double medianMs;
    double meanMs;
    double stddevMs;
    double throughputMBps;
    std::size_t peakRssBytes;
};

struct BenchmarkConfig {
    std::filesystem::path inputFile;
    jsontitan::core::ParserBackend backend = jsontitan::core::ParserBackend::Simdjson;
    unsigned iterations = 10;
};

// ============================================================================
// Benchmark execution
// ============================================================================

auto runBenchmark(const BenchmarkConfig& config) -> BenchmarkResult {
    // Read input file into memory
    std::ifstream file(config.inputFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << config.inputFile << "\n";
        std::exit(1);
    }

    auto fileSize = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::string content(fileSize, '\0');
    if (!file.read(content.data(), static_cast<std::streamsize>(fileSize))) {
        std::cerr << "Error: Failed to read file: " << config.inputFile << "\n";
        std::exit(1);
    }
    file.close();

    double fileSizeMB = static_cast<double>(fileSize) / (1024.0 * 1024.0);

    // Run iterations and collect timings
    std::vector<double> timingsMs;
    timingsMs.reserve(config.iterations);

    for (unsigned i = 0; i < config.iterations; ++i) {
        // Make a copy of the input for each iteration (parseBuffer takes ownership)
        std::string inputCopy = content;

        jsontitan::core::ParseBufferOptions opts;
        opts.backend = config.backend;

        auto start = std::chrono::steady_clock::now();
        auto result = jsontitan::core::parseBuffer(std::move(inputCopy), opts);
        auto end = std::chrono::steady_clock::now();

        if (!result.ok()) {
            std::cerr << "Error: Parse failed on iteration " << (i + 1);
            if (result.error) {
                std::cerr << ": " << result.error->description
                          << " at byte " << result.error->byteOffset;
            }
            std::cerr << "\n";
            std::exit(1);
        }

        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        timingsMs.push_back(ms);
    }

    // Measure peak RSS after all iterations
    std::size_t peakRss = getPeakRssBytes();

    // Compute statistics
    double median = computeMedian(timingsMs);
    double mean = computeMean(timingsMs);
    double stddev = computeStddev(timingsMs);
    double throughput = fileSizeMB / (median / 1000.0); // MB/s based on median time

    return BenchmarkResult{
        .medianMs = median,
        .meanMs = mean,
        .stddevMs = stddev,
        .throughputMBps = throughput,
        .peakRssBytes = peakRss,
    };
}

// ============================================================================
// Output formatting
// ============================================================================

auto backendToString(jsontitan::core::ParserBackend backend) -> std::string {
    switch (backend) {
        case jsontitan::core::ParserBackend::Simdjson: return "simdjson";
        case jsontitan::core::ParserBackend::Custom:   return "custom";
    }
    return "unknown";
}

auto formatResultsTable(std::span<const BenchmarkResult> results,
                        std::span<const std::string> labels) -> std::string {
    std::string output;
    output += "┌────────────┬────────────┬────────────┬────────────┬──────────────┬──────────────┐\n";
    output += "│ Backend    │ Median(ms) │ Mean(ms)   │ Stddev(ms) │ Throughput   │ Peak RSS     │\n";
    output += "├────────────┼────────────┼────────────┼────────────┼──────────────┼──────────────┤\n";

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        const auto& label = (i < labels.size()) ? labels[i] : "unknown";

        char row[256];
        double rssMB = static_cast<double>(r.peakRssBytes) / (1024.0 * 1024.0);
        std::snprintf(row, sizeof(row),
            "│ %-10s │ %10.2f │ %10.2f │ %10.2f │ %8.2f MB/s │ %8.1f MB  │\n",
            label.c_str(), r.medianMs, r.meanMs, r.stddevMs,
            r.throughputMBps, rssMB);
        output += row;
    }

    output += "└────────────┴────────────┴────────────┴────────────┴──────────────┴──────────────┘\n";
    return output;
}

// ============================================================================
// CLI argument parsing
// ============================================================================

void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " <input-file> [options]\n"
              << "\n"
              << "Options:\n"
              << "  --backend simdjson|custom   Select parsing backend (default: simdjson)\n"
              << "  --iterations N             Number of iterations (default: 10, minimum: 5)\n"
              << "  --help                     Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << progName << " large.json\n"
              << "  " << progName << " large.json --backend custom --iterations 20\n";
}

struct CliArgs {
    std::filesystem::path inputFile;
    jsontitan::core::ParserBackend backend = jsontitan::core::ParserBackend::Simdjson;
    unsigned iterations = 10;
    bool help = false;
};

auto parseCliArgs(int argc, char* argv[]) -> CliArgs {
    CliArgs args;

    if (argc < 2) {
        printUsage(argv[0]);
        std::exit(1);
    }

    // Check for --help first
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            args.help = true;
            return args;
        }
    }

    // First positional argument is the input file
    args.inputFile = argv[1];

    // Parse remaining options
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--backend") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --backend requires an argument\n";
                std::exit(1);
            }
            std::string backendStr = argv[++i];
            if (backendStr == "simdjson") {
                args.backend = jsontitan::core::ParserBackend::Simdjson;
            } else if (backendStr == "custom") {
                args.backend = jsontitan::core::ParserBackend::Custom;
            } else {
                std::cerr << "Error: Unknown backend '" << backendStr
                          << "'. Use 'simdjson' or 'custom'.\n";
                std::exit(1);
            }
        } else if (arg == "--iterations") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --iterations requires an argument\n";
                std::exit(1);
            }
            int n = std::stoi(argv[++i]);
            if (n < 5) {
                std::cerr << "Error: Minimum iterations is 5, got " << n << "\n";
                std::exit(1);
            }
            args.iterations = static_cast<unsigned>(n);
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            printUsage(argv[0]);
            std::exit(1);
        }
    }

    return args;
}

} // anonymous namespace

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    auto args = parseCliArgs(argc, argv);

    if (args.help) {
        printUsage(argv[0]);
        return 0;
    }

    // Validate input file exists
    if (!std::filesystem::exists(args.inputFile)) {
        std::cerr << "Error: File not found: " << args.inputFile << "\n";
        return 1;
    }

    auto fileSize = std::filesystem::file_size(args.inputFile);
    double fileSizeMB = static_cast<double>(fileSize) / (1024.0 * 1024.0);

    std::cout << "Benchmark: " << args.inputFile.filename().string() << "\n"
              << "  File size: " << fileSizeMB << " MB\n"
              << "  Backend:   " << backendToString(args.backend) << "\n"
              << "  Iterations: " << args.iterations << "\n"
              << "\n"
              << "Running benchmark...\n\n";

    BenchmarkConfig config{
        .inputFile = args.inputFile,
        .backend = args.backend,
        .iterations = args.iterations,
    };

    auto result = runBenchmark(config);

    // Format and print results
    std::vector<BenchmarkResult> results{result};
    std::vector<std::string> labels{backendToString(args.backend)};

    std::cout << formatResultsTable(results, labels);

    return 0;
}
