#include "ide/tools/web_search_tool.h"

#include "web/web.h"

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

WebSearchTool::WebSearchTool() = default;

std::string WebSearchTool::name() const { return "web_search"; }

std::string WebSearchTool::description() const {
    return "Searches the web using the configured search engines.";
}

ToolResult WebSearchTool::execute(const std::string& paramsJson) {
    std::string query = extractJsonString(paramsJson, "query");
    if (query.empty()) {
        return ToolResult{"query parameter is required", false, ""};
    }

    synapse::web::WebSearch searcher;
    auto config = synapse::web::defaultSearchConfig();
    if (!searcher.init(config)) {
        return ToolResult{"failed to initialize web search", false, ""};
    }

    auto results = searcher.searchClearnet(query);
    if (results.empty()) {
        return ToolResult{"no results found for: " + query, true, ""};
    }

    std::ostringstream out;
    size_t count = 0;
    for (const auto& r : results) {
        if (count >= 10) break;
        out << r.title << "\n" << r.url << "\n";
        if (!r.snippet.empty()) {
            out << r.snippet << "\n";
        }
        out << "\n";
        ++count;
    }

    return ToolResult{out.str(), true, ""};
}

}
}
}
