#pragma once

#include <string>

namespace synapse {
namespace utils {

// Restrict a regular file so only the current user can read and write it.
// Returns false when the file is missing, is not a regular file, or the
// resulting mode or DACL is not limited to the current user.
bool lockFileToCurrentUser(const std::string& path);

// Restrict a directory so only the current user can access it. On Windows the
// DACL is inheritable by new children. Returns false when the directory cannot
// be limited to the current user.
bool lockDirectoryToCurrentUser(const std::string& path);

// Publish contents at path only after the new file is limited to the current
// user. Returns false without leaving a new world-readable file.
bool writeFileForCurrentUser(const std::string& path, const std::string& contents);

} // namespace utils
} // namespace synapse
