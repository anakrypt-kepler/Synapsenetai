#include <cassert>
#include <iostream>
#include "network/network.h"

using namespace synapse::network;

int main() {
    std::cout << "Running Tor no-clearnet-leak tests...\n";

    // Scenario 1: unreachable SOCKS5 proxy with torRequired=true should return false
    std::cout << "  Scenario 1: Unreachable SOCKS5 proxy with torRequired=true... ";
    {
        Network net;
        NetworkConfig cfg;
        cfg.maxPeers = 125;
        cfg.maxOutbound = 25;
        cfg.maxInbound = 100;
        cfg.torRequired = true;
        cfg.useSocksProxy = true;
        cfg.socksProxyHost = "10.255.255.1";  // non-routable
        cfg.socksProxyPort = 9999;
        net.setConfig(cfg);
        
        bool ok = net.connect("example.com", 80);
        assert(!ok);  // Must fail
    }
    std::cout << "PASSED\n";

    // Scenario 2: torRequired=true with useSocksProxy=false should immediately return false
    std::cout << "  Scenario 2: torRequired=true, no proxy configured... ";
    {
        Network net;
        NetworkConfig cfg;
        cfg.maxPeers = 125;
        cfg.maxOutbound = 25;
        cfg.maxInbound = 100;
        cfg.torRequired = true;
        cfg.useSocksProxy = false;
        net.setConfig(cfg);
        
        bool ok = net.connect("93.184.216.34", 80);  // example.com
        assert(!ok);  // Must fail (fail-closed)
    }
    std::cout << "PASSED\n";

    // Scenario 3: torRequired=false with no proxy should attempt direct connection without crashing
    std::cout << "  Scenario 3: torRequired=false, no proxy, direct attempt... ";
    {
        Network net;
        NetworkConfig cfg;
        cfg.maxPeers = 125;
        cfg.maxOutbound = 25;
        cfg.maxInbound = 100;
        cfg.torRequired = false;
        cfg.useSocksProxy = false;
        net.setConfig(cfg);
        
        // Should not crash; may succeed or fail depending on connectivity
        bool ok = net.connect("127.0.0.1", 65432);  // localhost on unused port
        (void)ok;  // Accept either result
    }
    std::cout << "PASSED (no crash)\n";

    std::cout << "All Tor no-clearnet-leak tests PASSED\n";
    return 0;
}
