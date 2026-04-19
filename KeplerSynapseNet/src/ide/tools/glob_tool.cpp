#include "ide/tools/glob_tool.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <vector>

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
            else result += json[i];
            continue;
        }
        if (json[i] == '"') break;
        result += json[i];
    }
    return result;
}

}

GlobTool::GlobTool(const std::string& workingDir)
    : workingDir_(workingDir) {}

std::string GlobTool::name() const { return "glob"; }

std::string GlobTool::description() const {
    return "Fast file pattern matching using glob patterns.";
}

ToolResult GlobTool::execute(const std::string& paramsJson) {
    std::string pattern = extractJsonString(paramsJson, "pattern");
    std::string path = extractJsonString(paramsJson, "path");

    if (pattern.empty()) {
        return ToolResult{"pattern is required", false, ""};
    }

    if (path.empty()) {
        path = workingDir_;
    }

    std::string cmd = "find " + path + " -name '" + pattern + "' -type f 2>/dev/null | head -100";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return ToolResult{"failed to execute glob", false, ""};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
    pclose(pipe);

    if (output.empty()) {
        return ToolResult{"No files found", true, ""};
    }

    return ToolResult{output, true, ""};
}

}
}
}
