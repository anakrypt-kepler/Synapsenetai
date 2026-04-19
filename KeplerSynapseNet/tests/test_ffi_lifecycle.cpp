#include "synapsed_ffi.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

static std::string tmpDir;

static void setupTmpDir() {
    char tmpl[] = "/tmp/ffi_lifecycle_test_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    tmpDir = dir;
}

static void cleanupTmpDir() {
    std::string cmd = "rm -rf " + tmpDir;
    std::system(cmd.c_str());
}

static bool testInitSuccess() {
    std::string configPath = tmpDir + "/config.json";
    FILE* f = fopen(configPath.c_str(), "w");
    assert(f);
    fprintf(f, "{\"data_dir\":\"%s\"}", tmpDir.c_str());
    fclose(f);

    int result = synapsed_init(configPath.c_str());
    assert(result == 0);

    synapsed_shutdown();

    std::cerr << "  testInitSuccess passed\n";
    return true;
}

static bool testGetStatusReturnsValidJson() {
    std::string configPath = tmpDir + "/config_status.json";
    FILE* f = fopen(configPath.c_str(), "w");
    assert(f);
    fprintf(f, "{}");
    fclose(f);

    int result = synapsed_init(configPath.c_str());
    assert(result == 0);

    const char* status = synapsed_get_status();
    assert(status != nullptr);

    std::string statusStr(status);
    assert(statusStr.find("node_id") != std::string::npos);
    assert(statusStr.find("peer_count") != std::string::npos);
    assert(statusStr.find("uptime") != std::string::npos);
    assert(statusStr.find("version") != std::string::npos);
    assert(statusStr.find("model_status") != std::string::npos);

    synapsed_free_string(status);
    synapsed_shutdown();

    std::cerr << "  testGetStatusReturnsValidJson passed\n";
    return true;
}

static bool testRpcCallNodeStatus() {
    std::string configPath = tmpDir + "/config_rpc.json";
    FILE* f = fopen(configPath.c_str(), "w");
    assert(f);
    fprintf(f, "{}");
    fclose(f);

    int result = synapsed_init(configPath.c_str());
    assert(result == 0);

    const char* resp = synapsed_rpc_call("node.status", "{}");
    assert(resp != nullptr);

    std::string respStr(resp);
    assert(respStr.find("node_id") != std::string::npos);
    assert(respStr.find("version") != std::string::npos);

    synapsed_free_string(resp);
    synapsed_shutdown();

    std::cerr << "  testRpcCallNodeStatus passed\n";
    return true;
}

static bool testShutdownCleansUp() {
    std::string configPath = tmpDir + "/config_shutdown.json";
    FILE* f = fopen(configPath.c_str(), "w");
    assert(f);
    fprintf(f, "{}");
    fclose(f);

    int result = synapsed_init(configPath.c_str());
    assert(result == 0);

    synapsed_shutdown();

    const char* status = synapsed_get_status();
    assert(status != nullptr);
    std::string statusStr(status);
    assert(statusStr.find("error") != std::string::npos);
    synapsed_free_string(status);

    std::cerr << "  testShutdownCleansUp passed\n";
    return true;
}

static bool testFreeStringNoCrash() {
    synapsed_free_string(nullptr);

    char* test = static_cast<char*>(std::malloc(16));
    std::strcpy(test, "test");
    synapsed_free_string(test);

    std::cerr << "  testFreeStringNoCrash passed\n";
    return true;
}

static bool testDoubleInitReturnsError() {
    std::string configPath = tmpDir + "/config_double.json";
    FILE* f = fopen(configPath.c_str(), "w");
    assert(f);
    fprintf(f, "{}");
    fclose(f);

    int result1 = synapsed_init(configPath.c_str());
    assert(result1 == 0);

    int result2 = synapsed_init(configPath.c_str());
    assert(result2 != 0);

    synapsed_shutdown();

    std::cerr << "  testDoubleInitReturnsError passed\n";
    return true;
}

static bool testRpcBeforeInitReturnsError() {
    synapsed_shutdown();

    const char* resp = synapsed_rpc_call("node.status", "{}");
    assert(resp != nullptr);
    std::string respStr(resp);
    assert(respStr.find("error") != std::string::npos);
    assert(respStr.find("not initialized") != std::string::npos);
    synapsed_free_string(resp);

    std::cerr << "  testRpcBeforeInitReturnsError passed\n";
    return true;
}

static bool callbackFired = false;

static void testCallback(const char* event_type, const char* payload_json) {
    (void)event_type;
    (void)payload_json;
    callbackFired = true;
}

static bool testSubscribeRegistersCallback() {
    std::string configPath = tmpDir + "/config_sub.json";
    FILE* f = fopen(configPath.c_str(), "w");
    assert(f);
    fprintf(f, "{}");
    fclose(f);

    int result = synapsed_init(configPath.c_str());
    assert(result == 0);

    int subResult = synapsed_subscribe("node.event", testCallback);
    assert(subResult == 0);

    int badResult = synapsed_subscribe(nullptr, testCallback);
    assert(badResult != 0);

    int badResult2 = synapsed_subscribe("node.event", nullptr);
    assert(badResult2 != 0);

    synapsed_shutdown();

    std::cerr << "  testSubscribeRegistersCallback passed\n";
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
    std::cerr << "Running FFI lifecycle tests...\n";

    runTest("testInitSuccess", testInitSuccess);
    runTest("testGetStatusReturnsValidJson", testGetStatusReturnsValidJson);
    runTest("testRpcCallNodeStatus", testRpcCallNodeStatus);
    runTest("testShutdownCleansUp", testShutdownCleansUp);
    runTest("testFreeStringNoCrash", testFreeStringNoCrash);
    runTest("testDoubleInitReturnsError", testDoubleInitReturnsError);
    runTest("testRpcBeforeInitReturnsError", testRpcBeforeInitReturnsError);
    runTest("testSubscribeRegistersCallback", testSubscribeRegistersCallback);

    cleanupTmpDir();

    std::cerr << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
