#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>
#include "network/network.h"

using namespace synapse::network;

void testNetworkSoak60s() {
    Network a, b;
    a.start(19100);
    b.start(19101);

    // connect b -> a
    bool ok = b.connect("127.0.0.1", 19100);
    assert(ok);
    for (int i = 0; i < 50; ++i) {
        auto statsA = a.getStats();
        auto statsB = b.getStats();
        if (statsA.totalPeers != 0 || statsB.totalPeers != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto start = std::chrono::steady_clock::now();
    uint64_t initialBuffered = a.getStats().bufferedRxBytes;
    uint64_t lastBuffered = initialBuffered;

    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() < 60) {
        Message ping;
        ping.type = MessageType::PING;
        ping.payload = {0x00};
        a.send("127.0.0.1:19101", ping);
        b.send("127.0.0.1:19100", ping);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        static int tick = 0;
        if (++tick % 50 == 0) {
            auto statsA = a.getStats();
            if (statsA.totalPeers == 0) {
                continue;
            }
            if (statsA.bufferedRxBytes > lastBuffered * 2 + 1) {
                assert(false && "buffered bytes grew too much");
            }
            lastBuffered = statsA.bufferedRxBytes;
        }
    }

    a.stop();
    b.stop();
}

int main() {
    std::cout << "Running network soak test...\n";
    testNetworkSoak60s();
    std::cout << "Network soak OK\n";
    return 0;
}
