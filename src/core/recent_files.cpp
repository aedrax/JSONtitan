#include "core/recent_files.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace jsontitan::core {

namespace {

bool isBlank(const std::string& s) {
    return s.empty() ||
           std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isspace(c); });
}

} // namespace

RecentFilesList addRecentFile(const RecentFilesList& list,
                              const std::string& filePath) {
    if (isBlank(filePath)) {
        return list;
    }

    RecentFilesList result;
    result.reserve(std::min(list.size() + 1, kMaxRecentFiles));

    // Place the new path at the front.
    result.push_back(filePath);

    // Copy existing entries, skipping duplicates of filePath.
    for (const auto& entry : list) {
        if (entry != filePath) {
            result.push_back(entry);
        }
        if (result.size() == kMaxRecentFiles) {
            break;
        }
    }

    return result;
}

RecentFilesList removeRecentFile(const RecentFilesList& list,
                                 const std::string& filePath) {
    RecentFilesList result;
    result.reserve(list.size());

    for (const auto& entry : list) {
        if (entry != filePath) {
            result.push_back(entry);
        }
    }

    return result;
}

RecentFilesList clearRecentFiles() {
    return {};
}

std::string formatRecentEntry(const std::string& filePath) {
    namespace fs = std::filesystem;

    fs::path p(filePath);
    std::string filename = p.filename().string();
    std::string directory = p.parent_path().string();

    if (directory.empty()) {
        // No parent directory — just return the filename alone.
        return filename;
    }

    return filename + " [" + directory + "]";
}

} // namespace jsontitan::core
