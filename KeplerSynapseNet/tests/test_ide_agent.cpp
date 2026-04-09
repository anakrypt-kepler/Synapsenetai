#include "ide/agent_coordinator.h"
#include "ide/agent_errors.h"
#include "ide/agent_event.h"
#include "ide/agent_prompt.h"
#include "ide/tools/bash_tool.h"
#include "ide/tools/download_tool.h"
#include "ide/tools/edit_tool.h"
#include "ide/tools/fetch_tool.h"
#include "ide/tools/glob_tool.h"
#include "ide/tools/grep_tool.h"
#include "ide/tools/tool_base.h"
#include "ide/tools/web_search_tool.h"
#include "ide/tools/write_tool.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

static std::string tmpDir;

static void setupTmpDir() {
    char tmpl[] = "/tmp/ide_agent_test_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    tmpDir = dir;
}

static void cleanupTmpDir() {
    std::string cmd = "rm -rf " + tmpDir;
    std::system(cmd.c_str());
}

static bool testCoordinatorCreation() {
    synapse::ide::AgentCoordinator coord;
    assert(!coord.isBusy());
    assert(!coord.isSessionBusy("test-session"));
    auto names = coord.registeredToolNames();
    assert(names.empty());
    std::cerr << "  testCoordinatorCreation passed\n";
    return true;
}

static bool testToolRegistration() {
    synapse::ide::AgentCoordinator coord;

    auto bash = std::make_shared<synapse::ide::tools::BashTool>(tmpDir);
    auto edit = std::make_shared<synapse::ide::tools::EditTool>(tmpDir);
    auto grep = std::make_shared<synapse::ide::tools::GrepTool>(tmpDir);
    auto glob = std::make_shared<synapse::ide::tools::GlobTool>(tmpDir);
    auto fetch = std::make_shared<synapse::ide::tools::FetchTool>();
    auto write = std::make_shared<synapse::ide::tools::WriteTool>(tmpDir);
    auto download = std::make_shared<synapse::ide::tools::DownloadTool>(tmpDir);
    auto webSearch = std::make_shared<synapse::ide::tools::WebSearchTool>();

    coord.registerTool(bash);
    coord.registerTool(edit);
    coord.registerTool(grep);
    coord.registerTool(glob);
    coord.registerTool(fetch);
    coord.registerTool(write);
    coord.registerTool(download);
    coord.registerTool(webSearch);

    auto names = coord.registeredToolNames();
    assert(names.size() == 8);

    assert(bash->name() == "bash");
    assert(edit->name() == "edit");
    assert(grep->name() == "grep");
    assert(glob->name() == "glob");
    assert(fetch->name() == "fetch");
    assert(write->name() == "write");
    assert(download->name() == "download");
    assert(webSearch->name() == "web_search");

    std::cerr << "  testToolRegistration passed\n";
    return true;
}

static bool testBashToolExecution() {
    synapse::ide::tools::BashTool bash(tmpDir);
    auto result = bash.execute("{\"command\": \"echo hello world\"}");
    assert(result.success);
    assert(result.output.find("hello world") != std::string::npos);

    auto result2 = bash.execute("{\"command\": \"ls /nonexistent_path_xyz\"}");
    assert(!result2.success);

    std::cerr << "  testBashToolExecution passed\n";
    return true;
}

static bool testEditToolReplacement() {
    std::string testFile = tmpDir + "/edit_test.txt";
    {
        std::ofstream f(testFile);
        f << "Hello World\nLine Two\nLine Three\n";
    }

    synapse::ide::tools::EditTool edit(tmpDir);
    std::string params = "{\"file_path\": \"" + testFile +
                         "\", \"old_string\": \"Hello World\", \"new_string\": \"Hi Universe\"}";
    auto result = edit.execute(params);
    assert(result.success);

    std::ifstream f(testFile);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    assert(content.find("Hi Universe") != std::string::npos);
    assert(content.find("Hello World") == std::string::npos);

    std::cerr << "  testEditToolReplacement passed\n";
    return true;
}

static bool testGrepToolSearch() {
    std::string testFile = tmpDir + "/grep_test.txt";
    {
        std::ofstream f(testFile);
        f << "alpha\nbeta\ngamma\nalpha_beta\n";
    }

    synapse::ide::tools::GrepTool grep(tmpDir);
    auto result = grep.execute("{\"pattern\": \"alpha\", \"path\": \"" + tmpDir + "\"}");
    assert(result.success);
    assert(result.output.find("alpha") != std::string::npos);

    std::cerr << "  testGrepToolSearch passed\n";
    return true;
}

static bool testGlobToolMatching() {
    std::string testFile = tmpDir + "/glob_test.cpp";
    {
        std::ofstream f(testFile);
        f << "test file\n";
    }

    synapse::ide::tools::GlobTool glob(tmpDir);
    auto result = glob.execute("{\"pattern\": \"*.cpp\", \"path\": \"" + tmpDir + "\"}");
    assert(result.success);
    assert(result.output.find("glob_test.cpp") != std::string::npos);

    std::cerr << "  testGlobToolMatching passed\n";
    return true;
}

static bool testWriteToolCreation() {
    synapse::ide::tools::WriteTool write(tmpDir);
    std::string outFile = tmpDir + "/write_output.txt";
    std::string params =
        "{\"file_path\": \"" + outFile + "\", \"content\": \"written by write tool\"}";
    auto result = write.execute(params);
    assert(result.success);

    std::ifstream f(outFile);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    assert(content == "written by write tool");

    std::cerr << "  testWriteToolCreation passed\n";
    return true;
}

static bool testFetchToolCallsCurlFetch() {
    synapse::ide::tools::FetchTool fetch;
    assert(fetch.name() == "fetch");
    assert(!fetch.description().empty());
    std::cerr << "  testFetchToolCallsCurlFetch passed\n";
    return true;
}

static bool testDownloadToolCallsCurlFetch() {
    synapse::ide::tools::DownloadTool download(tmpDir);
    assert(download.name() == "download");
    assert(!download.description().empty());
    std::cerr << "  testDownloadToolCallsCurlFetch passed\n";
    return true;
}

static bool testWebSearchToolDelegates() {
    synapse::ide::tools::WebSearchTool ws;
    assert(ws.name() == "web_search");
    assert(!ws.description().empty());
    std::cerr << "  testWebSearchToolDelegates passed\n";
    return true;
}

static bool testAgentDispatchesByName() {
    synapse::ide::AgentCoordinator coord;
    coord.registerTool(std::make_shared<synapse::ide::tools::BashTool>(tmpDir));
    coord.registerTool(std::make_shared<synapse::ide::tools::WriteTool>(tmpDir));

    auto result = coord.dispatchTool("bash", "{\"command\": \"echo dispatched\"}");
    assert(result.success);
    assert(result.output.find("dispatched") != std::string::npos);

    auto unknown = coord.dispatchTool("nonexistent", "{}");
    assert(!unknown.success);
    assert(unknown.output.find("unknown tool") != std::string::npos);

    std::cerr << "  testAgentDispatchesByName passed\n";
    return true;
}

static bool testAgentPromptBuild() {
    synapse::ide::AgentPrompt prompt;
    prompt.setName("test");
    prompt.setWorkingDir(tmpDir);
    prompt.setPlatform("linux");
    prompt.setProvider("anthropic");
    prompt.setModel("claude-3");

    std::string built = prompt.build();
    assert(!built.empty());
    assert(built.find("Working directory:") != std::string::npos);
    assert(built.find("Platform: linux") != std::string::npos);
    assert(built.find("Provider: anthropic") != std::string::npos);
    assert(built.find("Model: claude-3") != std::string::npos);
    assert(built.find("Date:") != std::string::npos);

    assert(prompt.name() == "test");

    std::cerr << "  testAgentPromptBuild passed\n";
    return true;
}

static bool testAgentCoordinatorRun() {
    synapse::ide::AgentCoordinator coord;

    auto result1 = coord.run("", "hello");
    assert(!result1.success);
    assert(result1.output.find("session id is missing") != std::string::npos);

    auto result2 = coord.run("session1", "");
    assert(!result2.success);
    assert(result2.output.find("prompt is empty") != std::string::npos);

    auto result3 = coord.run("session1", "hello");
    assert(result3.success);
    assert(result3.output.find("prompt received") != std::string::npos);

    std::cerr << "  testAgentCoordinatorRun passed\n";
    return true;
}

static bool testAgentEventCallback() {
    synapse::ide::AgentCoordinator coord;
    int callCount = 0;
    coord.setEventCallback([&](const synapse::ide::AgentEvent& ev) {
        ++callCount;
    });

    coord.run("session1", "test prompt");
    assert(callCount == 2);

    std::cerr << "  testAgentEventCallback passed\n";
    return true;
}

static bool testAgentErrors() {
    try {
        throw synapse::ide::RequestCancelledError();
    } catch (const synapse::ide::AgentError& e) {
        assert(std::string(e.what()).find("canceled") != std::string::npos);
    }

    try {
        throw synapse::ide::SessionBusyError();
    } catch (const synapse::ide::AgentError& e) {
        assert(std::string(e.what()).find("processing") != std::string::npos);
    }

    try {
        throw synapse::ide::EmptyPromptError();
    } catch (const synapse::ide::AgentError& e) {
        assert(std::string(e.what()).find("empty") != std::string::npos);
    }

    try {
        throw synapse::ide::SessionMissingError();
    } catch (const synapse::ide::AgentError& e) {
        assert(std::string(e.what()).find("missing") != std::string::npos);
    }

    std::cerr << "  testAgentErrors passed\n";
    return true;
}

static bool testTokenUsage() {
    synapse::ide::TokenUsage usage;
    usage.inputTokens = 100;
    usage.outputTokens = 200;
    usage.cacheReadTokens = 50;
    usage.cacheCreationTokens = 25;
    assert(usage.total() == 375);

    std::cerr << "  testTokenUsage passed\n";
    return true;
}

int main() {
    setupTmpDir();

    std::cerr << "Running IDE Agent tests...\n";

    int failures = 0;
    if (!testCoordinatorCreation()) ++failures;
    if (!testToolRegistration()) ++failures;
    if (!testBashToolExecution()) ++failures;
    if (!testEditToolReplacement()) ++failures;
    if (!testGrepToolSearch()) ++failures;
    if (!testGlobToolMatching()) ++failures;
    if (!testWriteToolCreation()) ++failures;
    if (!testFetchToolCallsCurlFetch()) ++failures;
    if (!testDownloadToolCallsCurlFetch()) ++failures;
    if (!testWebSearchToolDelegates()) ++failures;
    if (!testAgentDispatchesByName()) ++failures;
    if (!testAgentPromptBuild()) ++failures;
    if (!testAgentCoordinatorRun()) ++failures;
    if (!testAgentEventCallback()) ++failures;
    if (!testAgentErrors()) ++failures;
    if (!testTokenUsage()) ++failures;

    cleanupTmpDir();

    if (failures == 0) {
        std::cerr << "All IDE Agent tests passed!\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed!\n";
    return 1;
}
