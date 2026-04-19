#include "ide/tools/edit_tool.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace synapse {
namespace ide {
namespace tools {

namespace {

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

std::string resolvePath(const std::string& workingDir, const std::string& filePath) {
    if (!filePath.empty() && filePath[0] == '/') return filePath;
    return workingDir + "/" + filePath;
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

}

EditTool::EditTool(const std::string& workingDir)
    : workingDir_(workingDir) {}

std::string EditTool::name() const { return "edit"; }

std::string EditTool::description() const {
    return "Performs exact string replacements in files.";
}

ToolResult EditTool::execute(const std::string& paramsJson) {
    std::string filePath = extractJsonString(paramsJson, "file_path");
    std::string oldString = extractJsonString(paramsJson, "old_string");
    std::string newString = extractJsonString(paramsJson, "new_string");
    bool replaceAll = extractJsonBool(paramsJson, "replace_all");

    if (filePath.empty()) {
        return ToolResult{"file_path is required", false, ""};
    }

    std::string fullPath = resolvePath(workingDir_, filePath);

    if (oldString.empty() && !newString.empty()) {
        if (!writeFileContents(fullPath, newString)) {
            return ToolResult{"failed to create file: " + fullPath, false, ""};
        }
        return ToolResult{"created file: " + fullPath, true, ""};
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

    if (!writeFileContents(fullPath, content)) {
        return ToolResult{"failed to write file: " + fullPath, false, ""};
    }

    return ToolResult{"edited file: " + fullPath, true, ""};
}

}
}
}
