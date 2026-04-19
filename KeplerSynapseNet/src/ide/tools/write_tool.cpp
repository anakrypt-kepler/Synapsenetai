#include "ide/tools/write_tool.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace synapse {
namespace ide {
namespace tools {

namespace {

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
            ++i;
            if (json[i] == '"') result += '"';
            else if (json[i] == '\\') result += '\\';
            else if (json[i] == 'n') result += '\n';
            else if (json[i] == 't') result += '\t';
            else result += json[i];
            continue;
        }
        if (json[i] == '"') break;
        result += json[i];
    }
    return result;
}

std::string resolvePath(const std::string& workingDir, const std::string& filePath) {
    if (!filePath.empty() && filePath[0] == '/') return filePath;
    return workingDir + "/" + filePath;
}

std::string parentDir(const std::string& path) {
    auto pos = path.rfind('/');
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

bool mkdirRecursive(const std::string& path) {
    if (path.empty()) return true;
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) return true;
    mkdirRecursive(parentDir(path));
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

}

WriteTool::WriteTool(const std::string& workingDir)
    : workingDir_(workingDir) {}

std::string WriteTool::name() const { return "write"; }

std::string WriteTool::description() const {
    return "Writes content to a file, creating directories as needed.";
}

ToolResult WriteTool::execute(const std::string& paramsJson) {
    std::string filePath = extractJsonString(paramsJson, "file_path");
    std::string content = extractJsonString(paramsJson, "content");

    if (filePath.empty()) {
        return ToolResult{"file_path is required", false, ""};
    }

    std::string fullPath = resolvePath(workingDir_, filePath);

    mkdirRecursive(parentDir(fullPath));

    std::ofstream file(fullPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return ToolResult{"failed to open file for writing: " + fullPath, false, ""};
    }
    file << content;
    if (!file.good()) {
        return ToolResult{"failed to write to file: " + fullPath, false, ""};
    }

    return ToolResult{"wrote to file: " + fullPath, true, ""};
}

}
}
}
