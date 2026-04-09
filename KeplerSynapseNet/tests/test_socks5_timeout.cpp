#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "privacy/privacy.h"

using namespace synapse::privacy;

static constexpr int SOCKS5_TIMEOUT_MS = 5000;
static constexpr int PORT_HANGING = 12345;

// Simple hanging server that accepts but never responds
void hangingServerThread() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) return;
    
    int reuseaddr = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT_HANGING);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (bind(serverSock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        listen(serverSock, 1);
        
        // Accept and hold connections indefinitely
        for (int i = 0; i < 10; ++i) {
            struct sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientSock >= 0) {
                // Hold connection without responding
                std::this_thread::sleep_for(std::chrono::seconds(10));
                close(clientSock);
            }
        }
    }
    close(serverSock);
}

int main() {
    std::cout << "Running SOCKS5 timeout tests...\n";

    // Test 1: Hanging server should timeout within SOCKS5_TIMEOUT_MS + 1s
    std::cout << "  Test 1: Hanging TCP server timeout... ";
    {
        std::thread server(hangingServerThread);
        server.detach();
        
        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Socks5Proxy proxy;
        auto start = std::chrono::steady_clock::now();
        bool ok = proxy.connect("127.0.0.1", PORT_HANGING);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        
        assert(!ok);  // Should fail/timeout
        assert(elapsed < SOCKS5_TIMEOUT_MS + 1000);  // Should timeout within budget
        assert(!proxy.isConnected());  // State should be disconnected
    }
    std::cout << "PASSED\n";

    // Test 2: Closed port refusal returns false within ~2s
    std::cout << "  Test 2: Closed port refusal... ";
    {
        Socks5Proxy proxy;
        auto start = std::chrono::steady_clock::now();
        bool ok = proxy.connect("127.0.0.1", 65432);  // unused port
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        
        assert(!ok);  // Should fail (connection refused)
        assert(elapsed < 2000);  // Should fail quickly
        assert(!proxy.isConnected());  // State should be disconnected
    }
    std::cout << "PASSED\n";

    // Test 3: isConnected() returns false after failed connect
    std::cout << "  Test 3: isConnected() after failure... ";
    {
        Socks5Proxy proxy;
        bool ok = proxy.connect("10.255.255.1", 9999);  // non-routable
        assert(!ok);
        assert(!proxy.isConnected());  // State should be disconnected
    }
    std::cout << "PASSED\n";


    std::cout << "All SOCKS5 timeout tests PASSED\n";
    return 0;
}
