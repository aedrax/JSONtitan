#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace jsontitan::core {

// Maximum number of entries retained in the recent files list.
inline constexpr std::size_t kMaxRecentFiles = 10;

// A recent files list is simply a vector of absolute file path strings,
// ordered from most-recently-opened (front) to least-recently-opened (back).
using RecentFilesList = std::vector<std::string>;

// Adds `filePath` to the front of `list`. If `filePath` already exists in
// the list, it is moved to the front (no duplicates). The returned list
// is capped at kMaxRecentFiles entries.
// If `filePath` is empty or whitespace-only, returns the list unchanged.
[[nodiscard]] RecentFilesList addRecentFile(
    const RecentFilesList& list, const std::string& filePath);

// Removes `filePath` from `list`. Returns the list without that entry.
// If `filePath` is not present, returns the list unchanged.
[[nodiscard]] RecentFilesList removeRecentFile(
    const RecentFilesList& list, const std::string& filePath);

// Returns an empty list.
[[nodiscard]] RecentFilesList clearRecentFiles();

// Formats a file path for display in the menu.
// Returns "filename.json [/path/to/directory]".
[[nodiscard]] std::string formatRecentEntry(const std::string& filePath);

} // namespace jsontitan::core
