#include "ide/lsp_client.h"
#include "ide/mcp_server.h"
#include "ide/oauth.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
#include <thread>

static std::string tmpDir;

static void setupTmpDir() {
    char tmpl[] = "/tmp/ide_lsp_test_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    tmpDir = dir;
}

static void cleanupTmpDir() {
    std::string cmd = "rm -rf " + tmpDir;
    std::system(cmd.c_str());
}

static bool testJsonRpcEncode() {
    std::string body = "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\"}";
    std::string encoded = synapse::ide::encodeJsonRpcMessage(body);

    std::string expectedHeader = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    assert(encoded == expectedHeader + body);

    std::cerr << "  testJsonRpcEncode passed\n";
    return true;
}

static bool testJsonRpcDecode() {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}";
    std::string raw = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;

    std::string decoded;
    size_t consumed = 0;
    bool ok = synapse::ide::decodeJsonRpcMessage(raw, decoded, consumed);
    assert(ok);
    assert(decoded == body);
    assert(consumed == raw.size());

    std::cerr << "  testJsonRpcDecode passed\n";
    return true;
}

static bool testJsonRpcDecodeIncomplete() {
    std::string raw = "Content-Length: 100\r\n\r\n{\"short\"}";
    std::string decoded;
    size_t consumed = 0;
    bool ok = synapse::ide::decodeJsonRpcMessage(raw, decoded, consumed);
    assert(!ok);

    std::cerr << "  testJsonRpcDecodeIncomplete passed\n";
    return true;
}

static bool testJsonRpcDecodeNoHeader() {
    std::string raw = "Some garbage data without a header";
    std::string decoded;
    size_t consumed = 0;
    bool ok = synapse::ide::decodeJsonRpcMessage(raw, decoded, consumed);
    assert(!ok);

    std::cerr << "  testJsonRpcDecodeNoHeader passed\n";
    return true;
}

static bool testJsonRpcRoundTrip() {
    std::string body = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{}}";
    std::string encoded = synapse::ide::encodeJsonRpcMessage(body);
    std::string decoded;
    size_t consumed = 0;
    bool ok = synapse::ide::decodeJsonRpcMessage(encoded, decoded, consumed);
    assert(ok);
    assert(decoded == body);
    assert(consumed == encoded.size());

    std::cerr << "  testJsonRpcRoundTrip passed\n";
    return true;
}

static bool testLanguageDetectionCommon() {
    assert(synapse::ide::detectLanguageId("file.go") == "go");
    assert(synapse::ide::detectLanguageId("file.rs") == "rust");
    assert(synapse::ide::detectLanguageId("file.py") == "python");
    assert(synapse::ide::detectLanguageId("file.ts") == "typescript");
    assert(synapse::ide::detectLanguageId("file.tsx") == "typescriptreact");
    assert(synapse::ide::detectLanguageId("file.js") == "javascript");
    assert(synapse::ide::detectLanguageId("file.jsx") == "javascriptreact");
    assert(synapse::ide::detectLanguageId("file.cpp") == "cpp");
    assert(synapse::ide::detectLanguageId("file.c") == "c");
    assert(synapse::ide::detectLanguageId("file.java") == "java");
    assert(synapse::ide::detectLanguageId("file.rb") == "ruby");
    assert(synapse::ide::detectLanguageId("file.php") == "php");
    assert(synapse::ide::detectLanguageId("file.cs") == "csharp");
    assert(synapse::ide::detectLanguageId("file.swift") == "swift");
    assert(synapse::ide::detectLanguageId("file.kt") == "kotlin");

    std::cerr << "  testLanguageDetectionCommon passed\n";
    return true;
}

static bool testLanguageDetectionSpecial() {
    assert(synapse::ide::detectLanguageId("/path/to/Dockerfile") == "dockerfile");
    assert(synapse::ide::detectLanguageId("/path/to/Makefile") == "makefile");
    assert(synapse::ide::detectLanguageId("/path/to/CMakeLists.txt") == "cmake");
    assert(synapse::ide::detectLanguageId("file.html") == "html");
    assert(synapse::ide::detectLanguageId("file.css") == "css");
    assert(synapse::ide::detectLanguageId("file.json") == "json");
    assert(synapse::ide::detectLanguageId("file.yaml") == "yaml");
    assert(synapse::ide::detectLanguageId("file.yml") == "yaml");
    assert(synapse::ide::detectLanguageId("file.md") == "markdown");
    assert(synapse::ide::detectLanguageId("file.sql") == "sql");
    assert(synapse::ide::detectLanguageId("file.sh") == "shellscript");
    assert(synapse::ide::detectLanguageId("file.toml") == "toml");

    std::cerr << "  testLanguageDetectionSpecial passed\n";
    return true;
}

static bool testLanguageDetectionUnknown() {
    assert(synapse::ide::detectLanguageId("file.xyz").empty());
    assert(synapse::ide::detectLanguageId("noextension").empty());

    std::cerr << "  testLanguageDetectionUnknown passed\n";
    return true;
}

static bool testLspClientRequestIdTracking() {
    synapse::ide::LspClient client;
    assert(client.state() == synapse::ide::ServerState::Disabled);

    bool ok = client.initialize("file:///tmp/project", "/usr/bin/true");
    assert(ok);
    assert(client.state() == synapse::ide::ServerState::Ready);

    bool ok2 = client.initialize("file:///tmp/other", "/usr/bin/true");
    assert(!ok2);

    client.shutdown();
    assert(client.state() == synapse::ide::ServerState::Disabled);

    std::cerr << "  testLspClientRequestIdTracking passed\n";
    return true;
}

static bool testLspClientDidOpenClose() {
    synapse::ide::LspClient client;
    client.initialize("file:///tmp/project", "/usr/bin/true");

    client.didOpen("file:///tmp/project/main.go", "go", "package main");
    auto diags = client.diagnostics("file:///tmp/project/main.go");
    assert(diags.empty());

    client.didClose("file:///tmp/project/main.go");
    diags = client.diagnostics("file:///tmp/project/main.go");
    assert(diags.empty());

    client.shutdown();

    std::cerr << "  testLspClientDidOpenClose passed\n";
    return true;
}

static bool testLspDiagnosticCounts() {
    synapse::ide::LspClient client;
    client.initialize("file:///tmp/project", "/usr/bin/true");

    auto counts = client.getDiagnosticCounts();
    assert(counts.errors == 0);
    assert(counts.warnings == 0);
    assert(counts.information == 0);
    assert(counts.hints == 0);

    client.shutdown();

    std::cerr << "  testLspDiagnosticCounts passed\n";
    return true;
}

static bool testMcpToolRegistrationAndListing() {
    synapse::ide::McpServer server;

    server.registerTool("read_file", "Read a file from disk",
                        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}",
                        [](const std::string& args) -> synapse::ide::McpToolResult {
                            (void)args;
                            synapse::ide::McpToolResult r;
                            r.success = true;
                            r.content = "file contents here";
                            return r;
                        });

    server.registerTool("write_file", "Write a file to disk",
                        "{\"type\":\"object\"}",
                        [](const std::string& args) -> synapse::ide::McpToolResult {
                            (void)args;
                            synapse::ide::McpToolResult r;
                            r.success = true;
                            r.content = "written";
                            return r;
                        });

    auto tools = server.listTools();
    assert(tools.size() == 2);

    bool foundRead = false, foundWrite = false;
    for (const auto& t : tools) {
        if (t.name == "read_file") foundRead = true;
        if (t.name == "write_file") foundWrite = true;
    }
    assert(foundRead);
    assert(foundWrite);

    std::cerr << "  testMcpToolRegistrationAndListing passed\n";
    return true;
}

static bool testMcpToolCallDispatch() {
    synapse::ide::McpServer server;

    server.registerTool("echo", "Echo tool", "{}",
                        [](const std::string& args) -> synapse::ide::McpToolResult {
                            synapse::ide::McpToolResult r;
                            r.success = true;
                            r.content = "echoed: " + args;
                            return r;
                        });

    auto result = server.callTool("echo", "{\"msg\":\"hello\"}");
    assert(result.success);
    assert(result.content == "echoed: {\"msg\":\"hello\"}");

    auto missing = server.callTool("nonexistent", "{}");
    assert(!missing.success);
    assert(!missing.errorMessage.empty());

    std::cerr << "  testMcpToolCallDispatch passed\n";
    return true;
}

static bool testMcpToolCallError() {
    synapse::ide::McpServer server;

    server.registerTool("fail_tool", "Always fails", "{}",
                        [](const std::string& args) -> synapse::ide::McpToolResult {
                            (void)args;
                            synapse::ide::McpToolResult r;
                            r.success = false;
                            r.errorMessage = "intentional failure";
                            return r;
                        });

    auto result = server.callTool("fail_tool", "{}");
    assert(!result.success);
    assert(result.errorMessage == "intentional failure");

    std::cerr << "  testMcpToolCallError passed\n";
    return true;
}

static bool testMcpResourceRegistrationAndReading() {
    synapse::ide::McpServer server;

    server.registerResource("file:///config.json", "config", "application/json",
                            []() -> synapse::ide::McpResourceContent {
                                synapse::ide::McpResourceContent c;
                                c.uri = "file:///config.json";
                                c.mimeType = "application/json";
                                c.text = "{\"key\":\"value\"}";
                                return c;
                            });

    auto resources = server.listResources();
    assert(resources.size() == 1);
    assert(resources[0].uri == "file:///config.json");
    assert(resources[0].name == "config");

    auto content = server.readResource("file:///config.json");
    assert(content.uri == "file:///config.json");
    assert(content.text == "{\"key\":\"value\"}");

    auto missing = server.readResource("file:///missing.json");
    assert(missing.text.empty());

    std::cerr << "  testMcpResourceRegistrationAndReading passed\n";
    return true;
}

static bool testMcpServerStartStop() {
    synapse::ide::McpServer server;
    assert(!server.isRunning());

    bool ok = server.start();
    assert(ok);
    assert(server.isRunning());

    bool ok2 = server.start();
    assert(!ok2);

    server.stop();
    assert(!server.isRunning());

    std::cerr << "  testMcpServerStartStop passed\n";
    return true;
}

static bool testMcpHandleRequest() {
    synapse::ide::McpServer server;

    server.registerTool("test_tool", "A test tool", "{}",
                        [](const std::string& args) -> synapse::ide::McpToolResult {
                            (void)args;
                            synapse::ide::McpToolResult r;
                            r.success = true;
                            r.content = "ok";
                            return r;
                        });

    std::string listReq = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}";
    std::string listResp = server.handleRequest(listReq);
    assert(listResp.find("test_tool") != std::string::npos);

    std::string badReq = "{\"jsonrpc\":\"2.0\",\"id\":2}";
    std::string badResp = server.handleRequest(badReq);
    assert(badResp.find("error") != std::string::npos);

    std::cerr << "  testMcpHandleRequest passed\n";
    return true;
}

static bool testOAuthTokenExpiry() {
    synapse::ide::OAuthToken tok;
    tok.accessToken = "test-access-token";
    tok.refreshToken = "test-refresh-token";
    tok.expiresIn = 3600;
    tok.setExpiresAt();

    assert(tok.expiresAt > 0);
    assert(!tok.isExpired());

    synapse::ide::OAuthToken expired;
    expired.accessToken = "expired-token";
    expired.expiresIn = 100;
    expired.expiresAt = 1000;
    assert(expired.isExpired());

    std::cerr << "  testOAuthTokenExpiry passed\n";
    return true;
}

static bool testOAuthTokenSetExpiresIn() {
    synapse::ide::OAuthToken tok;
    tok.expiresIn = 7200;
    tok.setExpiresAt();

    tok.setExpiresIn();
    assert(tok.expiresIn > 7190);
    assert(tok.expiresIn <= 7200);

    std::cerr << "  testOAuthTokenSetExpiresIn passed\n";
    return true;
}

static bool testOAuthTokenSerialization() {
    synapse::ide::OAuthToken original;
    original.accessToken = "my-access-token";
    original.refreshToken = "my-refresh-token";
    original.expiresIn = 3600;
    original.expiresAt = 1700000000;

    std::string json = original.toJson();
    assert(json.find("my-access-token") != std::string::npos);
    assert(json.find("my-refresh-token") != std::string::npos);
    assert(json.find("3600") != std::string::npos);
    assert(json.find("1700000000") != std::string::npos);

    synapse::ide::OAuthToken parsed = synapse::ide::OAuthToken::fromJson(json);
    assert(parsed.accessToken == "my-access-token");
    assert(parsed.refreshToken == "my-refresh-token");
    assert(parsed.expiresIn == 3600);
    assert(parsed.expiresAt == 1700000000);

    std::cerr << "  testOAuthTokenSerialization passed\n";
    return true;
}

static bool testOAuthTokenRoundTripFile() {
    synapse::ide::OAuthManager mgr;

    synapse::ide::OAuthToken tok;
    tok.accessToken = "file-test-token";
    tok.refreshToken = "file-refresh-token";
    tok.expiresIn = 7200;
    tok.setExpiresAt();

    std::string path = tmpDir + "/oauth_token.json";
    bool saved = mgr.saveToken("github", tok, path);
    assert(saved);

    auto loaded = mgr.loadToken("github", path);
    assert(loaded.accessToken == "file-test-token");
    assert(loaded.refreshToken == "file-refresh-token");
    assert(loaded.expiresIn == 7200);
    assert(loaded.expiresAt == tok.expiresAt);

    std::cerr << "  testOAuthTokenRoundTripFile passed\n";
    return true;
}

static bool testOAuthTokenLoadMissing() {
    synapse::ide::OAuthManager mgr;
    auto loaded = mgr.loadToken("missing", tmpDir + "/nonexistent.json");
    assert(loaded.accessToken.empty());

    std::cerr << "  testOAuthTokenLoadMissing passed\n";
    return true;
}

static bool testOAuthManagerRefresh() {
    synapse::ide::OAuthManager mgr;

    synapse::ide::OAuthToken tok;
    tok.accessToken = "old-token";
    tok.refreshToken = "refresh";
    tok.expiresIn = 100;
    tok.expiresAt = 1000;

    auto refreshed = mgr.refreshToken("test-provider", tok);
    assert(refreshed.expiresIn == 3600);
    assert(refreshed.expiresAt > 0);
    assert(!refreshed.isExpired());

    std::cerr << "  testOAuthManagerRefresh passed\n";
    return true;
}

static int passed = 0;
static int failed = 0;

static void runTest(const char* name, bool (*fn)()) {
    try {
        if (fn()) {
            ++passed;
        } else {
            std::cerr << "FAIL: " << name << "\n";
            ++failed;
        }
    } catch (const std::exception& e) {
        std::cerr << "FAIL (exception): " << name << ": " << e.what() << "\n";
        ++failed;
    } catch (...) {
        std::cerr << "FAIL (unknown exception): " << name << "\n";
        ++failed;
    }
}

int main() {
    setupTmpDir();
    std::cerr << "Running IDE LSP/MCP/OAuth tests...\n";

    runTest("testJsonRpcEncode", testJsonRpcEncode);
    runTest("testJsonRpcDecode", testJsonRpcDecode);
    runTest("testJsonRpcDecodeIncomplete", testJsonRpcDecodeIncomplete);
    runTest("testJsonRpcDecodeNoHeader", testJsonRpcDecodeNoHeader);
    runTest("testJsonRpcRoundTrip", testJsonRpcRoundTrip);
    runTest("testLanguageDetectionCommon", testLanguageDetectionCommon);
    runTest("testLanguageDetectionSpecial", testLanguageDetectionSpecial);
    runTest("testLanguageDetectionUnknown", testLanguageDetectionUnknown);
    runTest("testLspClientRequestIdTracking", testLspClientRequestIdTracking);
    runTest("testLspClientDidOpenClose", testLspClientDidOpenClose);
    runTest("testLspDiagnosticCounts", testLspDiagnosticCounts);
    runTest("testMcpToolRegistrationAndListing", testMcpToolRegistrationAndListing);
    runTest("testMcpToolCallDispatch", testMcpToolCallDispatch);
    runTest("testMcpToolCallError", testMcpToolCallError);
    runTest("testMcpResourceRegistrationAndReading", testMcpResourceRegistrationAndReading);
    runTest("testMcpServerStartStop", testMcpServerStartStop);
    runTest("testMcpHandleRequest", testMcpHandleRequest);
    runTest("testOAuthTokenExpiry", testOAuthTokenExpiry);
    runTest("testOAuthTokenSetExpiresIn", testOAuthTokenSetExpiresIn);
    runTest("testOAuthTokenSerialization", testOAuthTokenSerialization);
    runTest("testOAuthTokenRoundTripFile", testOAuthTokenRoundTripFile);
    runTest("testOAuthTokenLoadMissing", testOAuthTokenLoadMissing);
    runTest("testOAuthManagerRefresh", testOAuthManagerRefresh);

    cleanupTmpDir();

    std::cerr << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
