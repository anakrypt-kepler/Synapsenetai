#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace synapse {
namespace ide {

enum class ServerState {
    Starting,
    Ready,
    Error,
    Disabled
};

struct Diagnostic {
    std::string uri;
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
    int severity = 0;
    std::string message;
    std::string source;
};

struct DiagnosticCounts {
    int errors = 0;
    int warnings = 0;
    int information = 0;
    int hints = 0;
};

struct CompletionItem {
    std::string label;
    int kind = 0;
    std::string detail;
    std::string insertText;
};

struct HoverResult {
    std::string contents;
    bool found = false;
};

struct Location {
    std::string uri;
    int line = 0;
    int character = 0;
};

struct JsonRpcMessage {
    int64_t id = -1;
    std::string method;
    std::string params;
    std::string result;
    std::string error;
    bool isNotification = false;
};

std::string detectLanguageId(const std::string& uri);

std::string encodeJsonRpcMessage(const std::string& body);
bool decodeJsonRpcMessage(const std::string& raw, std::string& body, size_t& consumed);

class LspClient {
public:
    LspClient();
    ~LspClient();

    bool initialize(const std::string& rootUri, const std::string& command,
                    const std::vector<std::string>& args = {});
    void shutdown();

    void didOpen(const std::string& uri, const std::string& languageId,
                 const std::string& text);
    void didClose(const std::string& uri);

    std::vector<CompletionItem> completion(const std::string& uri, int line,
                                           int character);
    std::vector<Diagnostic> diagnostics(const std::string& uri);
    HoverResult hover(const std::string& uri, int line, int character);
    std::vector<Location> references(const std::string& uri, int line,
                                     int character);

    DiagnosticCounts getDiagnosticCounts() const;
    ServerState state() const;

    using DiagnosticsCallback = std::function<void(const std::string& uri,
                                                   const std::vector<Diagnostic>& diags)>;
    void setDiagnosticsCallback(DiagnosticsCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    int64_t nextRequestId();
    std::string buildRequest(int64_t id, const std::string& method,
                             const std::string& paramsJson);
    std::string buildNotification(const std::string& method,
                                  const std::string& paramsJson);
};

}
}
