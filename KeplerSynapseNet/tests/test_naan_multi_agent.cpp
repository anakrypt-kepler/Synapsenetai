#include "synapsed_ffi.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static void writeFile(const std::string& path, const std::string& body) {
    std::ofstream out(path, std::ios::trunc);
    out << body;
}

static std::string rpc(const char* method, const std::string& params) {
    const char* raw = synapsed_rpc_call(method, params.c_str());
    assert(raw != nullptr);
    std::string out(raw);
    synapsed_free_string(raw);
    return out;
}

static bool hasDesktopApp() {
    FILE* p = popen("pgrep -f '[s]ynapsenet-app' 2>/dev/null", "r");
    if (!p) return false;
    char buf[32];
    const bool found = fgets(buf, sizeof(buf), p) != nullptr;
    pclose(p);
    return found;
}

static void restoreAtExit() {
    synapsed_shutdown();
}

int main() {
    std::cerr << std::unitbuf;
    if (hasDesktopApp()) {
        std::cerr << "skip: synapsenet-app is running (would fight session Tor)\n";
        return 0;
    }

    std::atexit(restoreAtExit);
    const std::string tmp = "/tmp/naan_multi_agent_test";
    (void)std::system(("rm -rf " + tmp + " && mkdir -p " + tmp).c_str());

    const std::string badGguf = tmp + "/not-a-model.bin";
    writeFile(badGguf, "not gguf");
    const std::string fatGguf = tmp + "/fat.gguf";
    {
        int fd = open(fatGguf.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
        assert(fd >= 0);
        const char magic[] = {'G', 'G', 'U', 'F'};
        assert(write(fd, magic, 4) == 4);
        assert(ftruncate(fd, 800LL * 1024 * 1024) == 0);
        close(fd);
    }

    std::string configPath = tmp + "/config.json";
    writeFile(configPath, "{\"data_dir\":\"" + tmp + "\"}");
    writeFile(tmp + "/settings.json",
        "{\"connection_type\":\"tor\",\"naan_enabled\":false,\"ram_limit_mb\":256}");

    int initRc = synapsed_init(configPath.c_str());
    std::cerr << "init rc=" << initRc << "\n";
    if (initRc != 0) {
        std::cerr << "synapsed_init failed: " << initRc << "\n";
        return 1;
    }

    std::string patch = std::string("{")
        + "\"naan_enabled\":false,"
        + "\"naan_topics\":\"AI, cryptography\","
        + "\"ram_limit_mb\":256,"
        + "\"naan_crew\":[{\"id\":\"crew-test-1\",\"skin\":\"station_minion\","
        + "\"rooms\":[\"tor\"],\"model_mode\":\"primary\",\"model_path\":\"\"}]"
        + "}";
    std::string up = rpc("settings.update", patch);
    assert(up.find("\"ok\"") != std::string::npos);

    std::string cfg = rpc("naan.config",
        "{\"topics\":\"AI, cryptography\",\"tick_interval\":10,\"budget_limit\":\"20\"}");
    assert(cfg.find("error") == std::string::npos);

    std::string startP = rpc("naan.control", "{\"action\":\"start\",\"agent_id\":\"primary\"}");
    std::cerr << "start primary: " << startP << "\n";
    assert(startP.find("\"ok\"") != std::string::npos);

    std::string startC = rpc("naan.control", "{\"action\":\"start\",\"agent_id\":\"crew-test-1\"}");
    std::cerr << "start crew: " << startC << "\n";
    assert(startC.find("\"ok\"") != std::string::npos);
    assert(startC.find("crew-test-1") != std::string::npos);

    std::string again = rpc("naan.control", "{\"action\":\"start\",\"agent_id\":\"crew-test-1\"}");
    assert(again.find("already running") != std::string::npos);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::string st = rpc("naan.status", "{}");
    std::cerr << "status len=" << st.size() << "\n";
    assert(st.find("\"id\":\"primary\"") != std::string::npos);
    assert(st.find("\"id\":\"crew-test-1\"") != std::string::npos);
    assert(st.find("crew-test-1") != std::string::npos);
    const bool primaryActive = st.find("\"state\":\"active\"") != std::string::npos;
    assert(primaryActive);

    std::string stopC = rpc("naan.control", "{\"action\":\"stop\",\"agent_id\":\"crew-test-1\"}");
    assert(stopC.find("\"ok\"") != std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    st = rpc("naan.status", "{}");
    assert(st.find("\"id\":\"primary\"") != std::string::npos);
    assert(st.find("\"state\":\"active\"") != std::string::npos);

    rpc("naan.control", "{\"action\":\"stop\",\"agent_id\":\"primary\"}");

    auto startWhenIdle = [&](const std::string& params) {
        std::string last;
        for (int i = 0; i < 120; i++) {
            last = rpc("naan.control", params);
            if (last.find("still stopping") == std::string::npos) return last;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return last;
    };

    std::string badPatch = std::string("{")
        + "\"naan_crew\":[{\"id\":\"crew-test-1\",\"skin\":\"station_minion\","
        + "\"rooms\":[\"tor\"],\"model_mode\":\"own\",\"model_path\":\"" + badGguf + "\"}]"
        + "}";
    rpc("settings.update", badPatch);
    std::string badStart = startWhenIdle("{\"action\":\"start\",\"agent_id\":\"crew-test-1\"}");
    std::cerr << "bad gguf: " << badStart << "\n";
    assert(badStart.find("invalid GGUF") != std::string::npos);

    std::string fatPatch = std::string("{")
        + "\"ram_limit_mb\":256,"
        + "\"naan_crew\":[{\"id\":\"crew-test-1\",\"skin\":\"station_minion\","
        + "\"rooms\":[\"tor\"],\"model_mode\":\"own\",\"model_path\":\"" + fatGguf + "\"}]"
        + "}";
    rpc("settings.update", fatPatch);
    std::string ramStart = startWhenIdle("{\"action\":\"start\",\"agent_id\":\"crew-test-1\"}");
    std::cerr << "fat gguf: " << ramStart << "\n";
    assert(ramStart.find("not enough RAM") != std::string::npos);

    std::cerr << "NaanMultiAgentTests ok\n";
    return 0;
}
