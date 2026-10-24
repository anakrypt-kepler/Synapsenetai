#include "ide/tools/edit_tool.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace synapse {
namespace ide {
namespace tools {

namespace {

enum class PathError {
    Ok,
    BadRoot,
    Absolute,
    Traversal,
    Outside,
    Invalid
};

struct ConfinedPath {
    PathError error = PathError::Invalid;
    std::filesystem::path root;
    std::filesystem::path path;
};

std::string readFileContents(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool writeFileContents(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << content;
    return file.good();
}

std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    auto valStart = json.find(':', pos);
    if (valStart == std::string::npos) return "";
    auto qStart = json.find('"', valStart + 1);
    if (qStart == std::string::npos) return "";
    std::string result;
    for (size_t i = qStart + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            char next = json[i + 1];
            if (next == '"') { result += '"'; ++i; }
            else if (next == '\\') { result += '\\'; ++i; }
            else if (next == 'n') { result += '\n'; ++i; }
            else if (next == 't') { result += '\t'; ++i; }
            else { result += next; ++i; }
            continue;
        }
        if (json[i] == '"') break;
        result += json[i];
    }
    return result;
}

bool extractJsonBool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;
    auto valStart = json.find(':', pos);
    if (valStart == std::string::npos) return false;
    auto rest = json.substr(valStart + 1);
    auto nonSpace = rest.find_first_not_of(" \t\n\r");
    if (nonSpace == std::string::npos) return false;
    return rest.substr(nonSpace, 4) == "true";
}

bool isControlText(const std::string& text) {
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

bool isDotDotSegment(std::string segment) {
#ifdef _WIN32
    const auto colon = segment.find(':');
    if (colon != std::string::npos) segment.resize(colon);
#endif
    while (!segment.empty()) {
        const unsigned char c = static_cast<unsigned char>(segment.back());
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') segment.pop_back();
        else break;
    }
#ifdef _WIN32
    while (segment.size() > 2 && segment.back() == '.') segment.pop_back();
#endif
    return segment == "..";
}

bool hasDotDotSegment(const std::string& pathText) {
    std::string segment;
    auto flush = [&]() {
        bool bad = isDotDotSegment(segment);
        segment.clear();
        return bad;
    };
    for (char c : pathText) {
        if (c == '/' || c == '\\') {
            if (flush()) return true;
        } else {
            segment.push_back(c);
        }
    }
    if (flush()) return true;

    const std::filesystem::path parsed(pathText);
    for (const auto& part : parsed) {
        if (isDotDotSegment(part.generic_string())) return true;
    }
    return false;
}

bool isAbsoluteInput(const std::string& pathText) {
    if (pathText.empty()) return false;
    const unsigned char lead = static_cast<unsigned char>(pathText[0]);
    if (lead == '/' || lead == '\\') return true;
    const std::filesystem::path parsed(pathText);
    return parsed.is_absolute() || parsed.has_root_name() || parsed.has_root_directory() ||
           parsed.has_root_path();
}

#ifdef _WIN32
bool isReservedDeviceSegment(const std::string& segment) {
    std::string name;
    for (unsigned char c : segment) {
        if (c == '.' || c == ':') break;
        if (c == ' ' || c == '\t') continue;
        name.push_back(static_cast<char>(std::tolower(c)));
    }
    if (name == "con" || name == "prn" || name == "aux" || name == "nul") return true;
    if (name.size() == 4 && (name.compare(0, 3, "com") == 0 || name.compare(0, 3, "lpt") == 0) &&
        name[3] >= '1' && name[3] <= '9') {
        return true;
    }
    return false;
}
#endif

PathError inspectSegments(const std::string& pathText) {
    std::string segment;
    auto flush = [&]() {
        if (isDotDotSegment(segment)) return PathError::Traversal;
#ifdef _WIN32
        if (isReservedDeviceSegment(segment)) return PathError::Invalid;
#endif
        return PathError::Ok;
    };
    for (char c : pathText) {
        if (c == '/' || c == '\\') {
            const PathError issue = flush();
            segment.clear();
            if (issue != PathError::Ok) return issue;
        } else {
            segment.push_back(c);
        }
    }
    const PathError tail = flush();
    if (tail != PathError::Ok) return tail;
    if (hasDotDotSegment(pathText)) return PathError::Traversal;
    return PathError::Ok;
}

std::filesystem::path canonicalOrLexical(const std::filesystem::path& path, std::error_code& ec) {
    namespace fs = std::filesystem;
    ec.clear();
    fs::path canon = fs::weakly_canonical(path, ec);
    if (!ec && !canon.empty()) return canon;

    ec.clear();
    fs::path cursor = path;
    std::vector<fs::path> tail;
    while (!cursor.empty()) {
        std::error_code existsEc;
        if (fs::exists(cursor, existsEc)) break;
        if (!cursor.has_filename() || cursor.filename().empty()) break;
        tail.push_back(cursor.filename());
        const fs::path parent = cursor.parent_path();
        if (parent.empty() || parent == cursor) return {};
        cursor = parent;
    }
    if (cursor.empty()) return {};
    std::error_code existsEc;
    if (!fs::exists(cursor, existsEc)) return {};
    canon = fs::weakly_canonical(cursor, ec);
    if (ec || canon.empty()) return {};
    for (auto it = tail.rbegin(); it != tail.rend(); ++it) {
        if (isDotDotSegment(it->generic_string())) {
            ec = std::make_error_code(std::errc::permission_denied);
            return {};
        }
        canon /= *it;
    }
    ec.clear();
    return canon.lexically_normal();
}

bool lockInside(const std::filesystem::path& root, std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path canon = canonicalOrLexical(path, ec);
    if (ec || canon.empty()) return false;
    if (canon == root) {
        path = canon;
        return true;
    }

    const fs::path relative = canon.lexically_relative(root);
    if (relative.empty()) return false;
    bool child = false;
    for (const auto& part : relative) {
        if (part == "..") return false;
        if (part != ".") child = true;
    }
    if (!child) {
        path = root;
        return true;
    }

    std::string rootText = root.generic_string();
    std::string canonText = canon.generic_string();
    if (rootText.empty()) return false;
    if (rootText.back() != '/') rootText.push_back('/');
#ifdef _WIN32
    for (char& ch : rootText) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (char& ch : canonText) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
#endif
    if (canonText.compare(0, rootText.size(), rootText) != 0) return false;
    path = canon;
    return true;
}

ConfinedPath confineUserPath(const std::string& workingDir,
                              const std::string& userPath,
                              bool allowRoot) {
    namespace fs = std::filesystem;
    ConfinedPath result;
    if (workingDir.empty() || userPath.empty() || isControlText(workingDir) ||
        isControlText(userPath)) {
        result.error = PathError::Invalid;
        return result;
    }
    const bool absolute = isAbsoluteInput(userPath);
    const PathError segments = inspectSegments(userPath);
    if (segments != PathError::Ok) {
        result.error = segments;
        return result;
    }

    try {
        std::error_code ec;
        const fs::path absoluteRoot = fs::absolute(fs::path(workingDir), ec);
        if (ec) {
            result.error = PathError::BadRoot;
            return result;
        }
        fs::path root = fs::weakly_canonical(absoluteRoot, ec);
        if (ec || root.empty() || !fs::is_directory(root, ec)) {
            result.error = PathError::BadRoot;
            return result;
        }

        fs::path joined = absolute ? fs::path(userPath) : root / fs::path(userPath);
        if (!lockInside(root, joined)) {
            result.error = absolute ? PathError::Absolute : PathError::Outside;
            return result;
        }
        if (!allowRoot && joined == root) {
            result.error = PathError::Invalid;
            return result;
        }
        result.error = PathError::Ok;
        result.root = root;
        result.path = joined;
        return result;
    } catch (const fs::filesystem_error&) {
        result.error = PathError::Invalid;
        return result;
    }
}

ToolResult pathError(PathError error) {
    const char* message = "invalid path";
    if (error == PathError::BadRoot) message = "invalid working directory";
    else if (error == PathError::Absolute) message = "absolute paths are not allowed";
    else if (error == PathError::Traversal) message = "path traversal is not allowed";
    else if (error == PathError::Outside) message = "path is outside the working directory";
    return ToolResult{message, false, ""};
}

}

EditTool::EditTool(const std::string& workingDir)
    : workingDir_(workingDir) {}

std::string EditTool::name() const { return "edit"; }

std::string EditTool::description() const {
    return "Performs exact string replacements in files inside the working directory.";
}

ToolResult EditTool::execute(const std::string& paramsJson) {
    std::string filePath = extractJsonString(paramsJson, "file_path");
    std::string oldString = extractJsonString(paramsJson, "old_string");
    std::string newString = extractJsonString(paramsJson, "new_string");
    bool replaceAll = extractJsonBool(paramsJson, "replace_all");

    if (filePath.empty()) {
        return ToolResult{"file_path is required", false, ""};
    }

    ConfinedPath confined = confineUserPath(workingDir_, filePath, false);
    if (confined.error != PathError::Ok) return pathError(confined.error);
    if (!lockInside(confined.root, confined.path)) {
        return ToolResult{"path is outside the working directory", false, ""};
    }
    const std::string fullPath = confined.path.string();

    if (oldString.empty() && !newString.empty()) {
        if (!lockInside(confined.root, confined.path)) {
            return ToolResult{"path is outside the working directory", false, ""};
        }
        if (!writeFileContents(confined.path.string(), newString)) {
            return ToolResult{"failed to create file: " + confined.path.string(), false, ""};
        }
        return ToolResult{"created file: " + confined.path.string(), true, ""};
    }

    std::string content = readFileContents(fullPath);
    if (content.empty() && oldString.empty()) {
        return ToolResult{"file is empty and no old_string specified", false, ""};
    }

    auto pos = content.find(oldString);
    if (pos == std::string::npos) {
        return ToolResult{
            "old_string not found in file. Make sure it matches exactly.", false, ""};
    }

    if (!replaceAll) {
        auto second = content.find(oldString, pos + oldString.size());
        if (second != std::string::npos) {
            return ToolResult{
                "old_string appears multiple times. Use replace_all or provide more context.",
                false, ""};
        }
        content.replace(pos, oldString.size(), newString);
    } else {
        std::string result;
        size_t searchStart = 0;
        while (true) {
            auto found = content.find(oldString, searchStart);
            if (found == std::string::npos) {
                result += content.substr(searchStart);
                break;
            }
            result += content.substr(searchStart, found - searchStart);
            result += newString;
            searchStart = found + oldString.size();
        }
        content = result;
    }

    if (!lockInside(confined.root, confined.path)) {
        return ToolResult{"path is outside the working directory", false, ""};
    }
    if (!writeFileContents(confined.path.string(), content)) {
        return ToolResult{"failed to write file: " + confined.path.string(), false, ""};
    }

    return ToolResult{"edited file: " + confined.path.string(), true, ""};
}

}
}
}
