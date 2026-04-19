#include "ide/tools/download_tool.h"

#include "web/curl_fetch.h"

#include <fstream>
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

DownloadTool::DownloadTool(const std::string& workingDir)
    : workingDir_(workingDir) {}

std::string DownloadTool::name() const { return "download"; }

std::string DownloadTool::description() const {
    return "Downloads a file from a URL and saves it locally.";
}

ToolResult DownloadTool::execute(const std::string& paramsJson) {
    std::string url = extractJsonString(paramsJson, "url");
    std::string outputPath = extractJsonString(paramsJson, "output_path");

    if (url.empty()) {
        return ToolResult{"url parameter is required", false, ""};
    }
    if (outputPath.empty()) {
        return ToolResult{"output_path parameter is required", false, ""};
    }

    std::string fullPath = resolvePath(workingDir_, outputPath);

    synapse::web::CurlFetchOptions opts;
    opts.timeoutSeconds = 60;
    opts.maxBytes = 50 * 1024 * 1024;

    auto result = synapse::web::curlFetch(url, opts);

    if (result.exitCode != 0) {
        std::string errMsg = "download failed";
        if (!result.error.empty()) {
            errMsg += ": " + result.error;
        }
        return ToolResult{errMsg, false, ""};
    }

    mkdirRecursive(parentDir(fullPath));

    std::ofstream file(fullPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return ToolResult{"failed to open file for writing: " + fullPath, false, ""};
    }
    file << result.body;
    if (!file.good()) {
        return ToolResult{"failed to write file: " + fullPath, false, ""};
    }

    return ToolResult{
        "downloaded " + url + " to " + fullPath + " (" +
            std::to_string(result.body.size()) + " bytes)",
        true, ""};
}

}
}
}
