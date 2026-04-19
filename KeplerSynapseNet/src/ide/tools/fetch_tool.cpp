#include "ide/tools/fetch_tool.h"

#include "web/curl_fetch.h"

#include <sstream>

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

FetchTool::FetchTool() = default;

std::string FetchTool::name() const { return "fetch"; }

std::string FetchTool::description() const {
    return "Fetches content from a URL using curlFetch.";
}

ToolResult FetchTool::execute(const std::string& paramsJson) {
    std::string url = extractJsonString(paramsJson, "url");
    if (url.empty()) {
        return ToolResult{"url parameter is required", false, ""};
    }

    synapse::web::CurlFetchOptions opts;
    opts.timeoutSeconds = 30;
    opts.maxBytes = 1024 * 1024;

    auto result = synapse::web::curlFetch(url, opts);

    if (result.exitCode != 0) {
        std::string errMsg = "fetch failed";
        if (!result.error.empty()) {
            errMsg += ": " + result.error;
        }
        return ToolResult{errMsg, false, ""};
    }

    return ToolResult{result.body, true, ""};
}

}
}
}
