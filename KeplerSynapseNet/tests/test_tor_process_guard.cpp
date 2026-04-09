#include "core/tor_process_guard.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

static void testOwnedManagedTorCommandLineMatch() {
    const std::string torDir = "/tmp/synapsenet_x/tor";
    const std::string cmd =
        "tor --quiet --SocksPort 127.0.0.1:9050 --ControlPort 127.0.0.1:9051 "
        "--DataDirectory /tmp/synapsenet_x/tor --PidFile /tmp/synapsenet_x/tor/tor.pid "
        "--RunAsDaemon 1";
    assert(synapse::core::isOwnedManagedTorCommandLine(cmd, torDir));
}

static void testOwnedManagedTorCommandLineRejectsUnrelatedTor() {
    const std::string torDir = "/tmp/synapsenet_x/tor";

    const std::string externalTor =
        "tor --SocksPort 127.0.0.1:9150 --ControlPort 127.0.0.1:9151";
    assert(!synapse::core::isOwnedManagedTorCommandLine(externalTor, torDir));

    const std::string otherDataDir =
        "tor --DataDirectory /tmp/other/tor --RunAsDaemon 1";
    assert(!synapse::core::isOwnedManagedTorCommandLine(otherDataDir, torDir));

    const std::string noDaemon =
        "tor --DataDirectory /tmp/synapsenet_x/tor";
    assert(!synapse::core::isOwnedManagedTorCommandLine(noDaemon, torDir));
}

static void testParseOwnedManagedTorPidsFromPsOutputFiltersAndDedups() {
    const std::string torDir = "/tmp/synapsenet_x/tor";
    const std::string psOut =
        "  58154 tor --quiet --SocksPort 127.0.0.1:9050 --ControlPort 127.0.0.1:9051 "
        "--DataDirectory /tmp/synapsenet_x/tor --PidFile /tmp/synapsenet_x/tor/tor.pid --RunAsDaemon 1\n"
        "  58154 tor --quiet --SocksPort 127.0.0.1:9050 --ControlPort 127.0.0.1:9051 "
        "--DataDirectory /tmp/synapsenet_x/tor --PidFile /tmp/synapsenet_x/tor/tor.pid --RunAsDaemon 1\n"
        "  61000 tor --SocksPort 127.0.0.1:9150 --ControlPort 127.0.0.1:9151 --RunAsDaemon 1\n"
        "  62000 tor --DataDirectory /tmp/other/tor --RunAsDaemon 1\n"
        "  63000 /Applications/Tor Browser.app/Contents/MacOS/firefox\n"
        "  64000 python3 some_script.py\n"
        "  invalid tor --DataDirectory /tmp/synapsenet_x/tor --RunAsDaemon 1\n"
        "  59000 tor --quiet --DataDirectory /tmp/synapsenet_x/tor --RunAsDaemon 1\n";

    const std::vector<int64_t> pids =
        synapse::core::parseOwnedManagedTorPidsFromPsOutput(psOut, torDir);
    assert((pids == std::vector<int64_t>{58154, 59000}));
}

static void testParseOwnedManagedTorPidsFromPsOutputEmptyInputs() {
    assert(synapse::core::parseOwnedManagedTorPidsFromPsOutput("", "/tmp/x").empty());
    assert(synapse::core::parseOwnedManagedTorPidsFromPsOutput("123 tor", "").empty());
}

int main() {
    testOwnedManagedTorCommandLineMatch();
    testOwnedManagedTorCommandLineRejectsUnrelatedTor();
    testParseOwnedManagedTorPidsFromPsOutputFiltersAndDedups();
    testParseOwnedManagedTorPidsFromPsOutputEmptyInputs();
    return 0;
}
