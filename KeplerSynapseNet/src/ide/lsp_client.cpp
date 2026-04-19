#include "ide/lsp_client.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

namespace synapse {
namespace ide {

namespace {

struct LanguageMapping {
    const char* extension;
    const char* languageId;
};

constexpr LanguageMapping kLanguageMappings[] = {
    {".go", "go"},
    {".rs", "rust"},
    {".py", "python"},
    {".pyi", "python"},
    {".js", "javascript"},
    {".jsx", "javascriptreact"},
    {".ts", "typescript"},
    {".tsx", "typescriptreact"},
    {".c", "c"},
    {".h", "c"},
    {".cpp", "cpp"},
    {".cxx", "cpp"},
    {".cc", "cpp"},
    {".hpp", "cpp"},
    {".hxx", "cpp"},
    {".java", "java"},
    {".rb", "ruby"},
    {".php", "php"},
    {".cs", "csharp"},
    {".fs", "fsharp"},
    {".swift", "swift"},
    {".kt", "kotlin"},
    {".kts", "kotlin"},
    {".scala", "scala"},
    {".lua", "lua"},
    {".pl", "perl"},
    {".pm", "perl"},
    {".r", "r"},
    {".R", "r"},
    {".dart", "dart"},
    {".ex", "elixir"},
    {".exs", "elixir"},
    {".erl", "erlang"},
    {".hrl", "erlang"},
    {".hs", "haskell"},
    {".ml", "ocaml"},
    {".mli", "ocaml"},
    {".zig", "zig"},
    {".nim", "nim"},
    {".v", "v"},
    {".d", "d"},
    {".sh", "shellscript"},
    {".bash", "shellscript"},
    {".zsh", "shellscript"},
    {".fish", "shellscript"},
    {".ps1", "powershell"},
    {".html", "html"},
    {".htm", "html"},
    {".css", "css"},
    {".scss", "scss"},
    {".less", "less"},
    {".json", "json"},
    {".jsonc", "jsonc"},
    {".xml", "xml"},
    {".yaml", "yaml"},
    {".yml", "yaml"},
    {".toml", "toml"},
    {".ini", "ini"},
    {".md", "markdown"},
    {".tex", "latex"},
    {".sql", "sql"},
    {".graphql", "graphql"},
    {".proto", "protobuf"},
    {".dockerfile", "dockerfile"},
    {".makefile", "makefile"},
    {".cmake", "cmake"},
};

}

std::string detectLanguageId(const std::string& uri) {
    std::string lower = uri;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::string basename = lower;
    auto slashPos = basename.rfind('/');
    if (slashPos != std::string::npos) {
        basename = basename.substr(slashPos + 1);
    }
    if (basename == "dockerfile") return "dockerfile";
    if (basename == "makefile" || basename == "gnumakefile") return "makefile";
    if (basename == "cmakelists.txt") return "cmake";

    auto dotPos = lower.rfind('.');
    if (dotPos == std::string::npos) return "";

    std::string ext = lower.substr(dotPos);
    for (const auto& m : kLanguageMappings) {
        if (ext == m.extension) return m.languageId;
    }
    return "";
}

std::string encodeJsonRpcMessage(const std::string& body) {
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    return header + body;
}

bool decodeJsonRpcMessage(const std::string& raw, std::string& body, size_t& consumed) {
    auto headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    std::string headerSection = raw.substr(0, headerEnd);
    std::string prefix = "Content-Length: ";
    auto clPos = headerSection.find(prefix);
    if (clPos == std::string::npos) return false;

    size_t valueStart = clPos + prefix.size();
    size_t valueEnd = headerSection.find("\r\n", valueStart);
    if (valueEnd == std::string::npos) valueEnd = headerSection.size();

    size_t contentLength = 0;
    try {
        contentLength = std::stoull(headerSection.substr(valueStart, valueEnd - valueStart));
    } catch (...) {
        return false;
    }

    size_t bodyStart = headerEnd + 4;
    if (raw.size() < bodyStart + contentLength) return false;

    body = raw.substr(bodyStart, contentLength);
    consumed = bodyStart + contentLength;
    return true;
}

struct LspClient::Impl {
    mutable std::mutex mtx;
    std::atomic<int64_t> requestIdCounter{1};
    ServerState serverState{ServerState::Disabled};
    std::unordered_map<std::string, std::vector<Diagnostic>> diagnosticsCache;
    std::unordered_map<std::string, int> openFileVersions;
    DiagnosticsCallback diagCallback;
    std::string rootUri;
    std::string command;
    std::vector<std::string> args;
    bool initialized = false;
};

LspClient::LspClient() : impl_(std::make_unique<Impl>()) {}
LspClient::~LspClient() { shutdown(); }

bool LspClient::initialize(const std::string& rootUri, const std::string& command,
                            const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->initialized) return false;

    impl_->rootUri = rootUri;
    impl_->command = command;
    impl_->args = args;
    impl_->serverState = ServerState::Starting;
    impl_->initialized = true;
    impl_->serverState = ServerState::Ready;
    return true;
}

void LspClient::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) return;
    impl_->serverState = ServerState::Disabled;
    impl_->initialized = false;
    impl_->diagnosticsCache.clear();
    impl_->openFileVersions.clear();
}

void LspClient::didOpen(const std::string& uri, const std::string& languageId,
                         const std::string& text) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->initialized) return;
    impl_->openFileVersions[uri] = 1;
    (void)languageId;
    (void)text;
}

void LspClient::didClose(const std::string& uri) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->openFileVersions.erase(uri);
    impl_->diagnosticsCache.erase(uri);
}

std::vector<CompletionItem> LspClient::completion(const std::string& uri, int line,
                                                   int character) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    (void)uri;
    (void)line;
    (void)character;
    return {};
}

std::vector<Diagnostic> LspClient::diagnostics(const std::string& uri) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->diagnosticsCache.find(uri);
    if (it != impl_->diagnosticsCache.end()) return it->second;
    return {};
}

HoverResult LspClient::hover(const std::string& uri, int line, int character) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    (void)uri;
    (void)line;
    (void)character;
    return {};
}

std::vector<Location> LspClient::references(const std::string& uri, int line,
                                             int character) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    (void)uri;
    (void)line;
    (void)character;
    return {};
}

DiagnosticCounts LspClient::getDiagnosticCounts() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    DiagnosticCounts counts;
    for (const auto& pair : impl_->diagnosticsCache) {
        for (const auto& d : pair.second) {
            switch (d.severity) {
                case 1: ++counts.errors; break;
                case 2: ++counts.warnings; break;
                case 3: ++counts.information; break;
                case 4: ++counts.hints; break;
            }
        }
    }
    return counts;
}

ServerState LspClient::state() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->serverState;
}

void LspClient::setDiagnosticsCallback(DiagnosticsCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->diagCallback = std::move(cb);
}

int64_t LspClient::nextRequestId() {
    return impl_->requestIdCounter.fetch_add(1);
}

std::string LspClient::buildRequest(int64_t id, const std::string& method,
                                     const std::string& paramsJson) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                       ",\"method\":\"" + method + "\"";
    if (!paramsJson.empty()) {
        body += ",\"params\":" + paramsJson;
    }
    body += "}";
    return encodeJsonRpcMessage(body);
}

std::string LspClient::buildNotification(const std::string& method,
                                          const std::string& paramsJson) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\"";
    if (!paramsJson.empty()) {
        body += ",\"params\":" + paramsJson;
    }
    body += "}";
    return encodeJsonRpcMessage(body);
}

}
}
