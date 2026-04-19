#include "network/network.h"
#include "network/scale_policy.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <deque>
#include <array>
#include <cstring>
#include <sys/time.h>
#include <optional>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <fstream>
#include <random>
#include <cerrno>
#include "utils/logger.h"

namespace synapse {
namespace network {

namespace {

struct PeerRxState {
    std::vector<uint8_t> buffer;
    scale::TokenBucketState byteBucket;
    scale::TokenBucketState messageBucket;
    std::unordered_set<uint64_t> recentNonces;
    static constexpr size_t MAX_RECENT_NONCES = 10000;

    bool checkAndRecordNonce(uint64_t nonce) {
        if (recentNonces.count(nonce)) return false; // duplicate
        if (recentNonces.size() >= MAX_RECENT_NONCES) {
            recentNonces.clear(); // simple eviction: clear when full
        }
        recentNonces.insert(nonce);
        return true;
    }
};

static constexpr size_t RX_BUFFER_LIMIT = MAX_MESSAGE_SIZE * 8;

static uint64_t monotonicMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
    return true;
}

static bool isValidCommand(const char command[12]) {
    for (size_t i = 0; i < 12; ++i) {
        unsigned char c = static_cast<unsigned char>(command[i]);
        if (c == 0) break;
        if (c < 32 || c > 126) return false;
    }
    return true;
}

static bool resolveIpv4Host(const std::string& host, in_addr* outAddr) {
    if (!outAddr) return false;
    if (inet_pton(AF_INET, host.c_str(), outAddr) == 1) return true;

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        return false;
    }
    struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    *outAddr = ipv4->sin_addr;
    freeaddrinfo(res);
    return true;
}

static bool resolveHost(const std::string& host, uint16_t port, struct sockaddr_storage* outAddr, socklen_t* outLen) {
    if (!outAddr || !outLen) return false;
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    // copy first result
    std::memset(outAddr, 0, sizeof(*outAddr));
    std::memcpy(outAddr, res->ai_addr, res->ai_addrlen);
    if (res->ai_family == AF_INET) {
        struct sockaddr_in* a = reinterpret_cast<struct sockaddr_in*>(outAddr);
        a->sin_port = htons(port);
        *outLen = sizeof(struct sockaddr_in);
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6* a6 = reinterpret_cast<struct sockaddr_in6*>(outAddr);
        a6->sin6_port = htons(port);
        *outLen = sizeof(struct sockaddr_in6);
    } else {
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);
    return true;
}

static bool sendAllBlocking(int sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(sock, data + sent, len - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) return false;
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

static bool recvAllBlocking(int sock, uint8_t* data, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t n = ::recv(sock, data + received, len - received, 0);
        if (n > 0) {
            received += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) return false;
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

static bool socks5ConnectTarget(int sock, const std::string& targetHost, uint16_t targetPort,
                                const std::string& socksUsername = "", const std::string& socksPassword = "") {
    struct timeval tv {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Support both no-auth (0x00) and username/password (0x02) methods
    const uint8_t greeting[4] = {0x05, 0x02, 0x00, 0x02};
    if (!sendAllBlocking(sock, greeting, sizeof(greeting))) return false;

    uint8_t authResp[2] = {};
    if (!recvAllBlocking(sock, authResp, sizeof(authResp))) return false;
    if (authResp[0] != 0x05) return false;

    if (authResp[1] == 0x02) {
        // Username/password authentication subnegotiation (RFC 1929)
        if (socksUsername.empty()) return false;
        std::vector<uint8_t> authReq;
        authReq.push_back(0x01); // subnegotiation version
        authReq.push_back(static_cast<uint8_t>(socksUsername.size()));
        authReq.insert(authReq.end(), socksUsername.begin(), socksUsername.end());
        authReq.push_back(static_cast<uint8_t>(socksPassword.size()));
        authReq.insert(authReq.end(), socksPassword.begin(), socksPassword.end());
        if (!sendAllBlocking(sock, authReq.data(), authReq.size())) return false;

        uint8_t authResult[2] = {};
        if (!recvAllBlocking(sock, authResult, sizeof(authResult))) return false;
        if (authResult[1] != 0x00) return false; // authentication failed
    } else if (authResp[1] != 0x00) {
        return false; // unsupported auth method
    }

    std::vector<uint8_t> req;
    req.reserve(6 + targetHost.size());
    req.push_back(0x05);
    req.push_back(0x01);
    req.push_back(0x00);

    in_addr ipv4{};
    in6_addr ipv6{};
    if (inet_pton(AF_INET, targetHost.c_str(), &ipv4) == 1) {
        req.push_back(0x01);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&ipv4);
        req.insert(req.end(), p, p + 4);
    } else if (inet_pton(AF_INET6, targetHost.c_str(), &ipv6) == 1) {
        req.push_back(0x04);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&ipv6);
        req.insert(req.end(), p, p + 16);
    } else {
        if (targetHost.size() > 255) return false;
        req.push_back(0x03);
        req.push_back(static_cast<uint8_t>(targetHost.size()));
        req.insert(req.end(), targetHost.begin(), targetHost.end());
    }

    req.push_back(static_cast<uint8_t>((targetPort >> 8) & 0xFF));
    req.push_back(static_cast<uint8_t>(targetPort & 0xFF));
    if (!sendAllBlocking(sock, req.data(), req.size())) return false;

    uint8_t respHead[4] = {};
    if (!recvAllBlocking(sock, respHead, sizeof(respHead))) return false;
    if (!(respHead[0] == 0x05 && respHead[1] == 0x00)) return false;

    size_t addrLen = 0;
    if (respHead[3] == 0x01) addrLen = 4;
    else if (respHead[3] == 0x04) addrLen = 16;
    else if (respHead[3] == 0x03) {
        uint8_t n = 0;
        if (!recvAllBlocking(sock, &n, 1)) return false;
        addrLen = n;
    } else {
        return false;
    }

    std::vector<uint8_t> discard(addrLen + 2, 0);
    return recvAllBlocking(sock, discard.data(), discard.size());
}

static bool resolveHostWithTimeout(const std::string& host, uint16_t port,
                                    struct sockaddr_storage* outAddr, socklen_t* outLen,
                                    int timeoutMs = 3000) {
    std::atomic<bool> done{false};
    bool result = false;
    std::thread resolver([&]() {
        result = resolveHost(host, port, outAddr, outLen);
        done = true;
    });
    auto start = std::chrono::steady_clock::now();
    while (!done) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() > timeoutMs) {
            resolver.detach();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    resolver.join();
    return result;
}

static std::string banAddressKeyFromPeerId(const std::string& peerIdOrAddress) {
    if (peerIdOrAddress.empty()) return {};
    size_t pos = peerIdOrAddress.find(':');
    if (pos == std::string::npos) return peerIdOrAddress;
    if (pos == 0) return {};
    return peerIdOrAddress.substr(0, pos);
}

}

std::vector<uint8_t> Message::serialize() const {
    if (payload.size() > MAX_MESSAGE_SIZE) return {};
    if (command.empty() || command.size() > 12) return {};
    for (char c : command) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc > 126) return {};
    }

    std::vector<uint8_t> out;
    MessageHeader hdr{};
    hdr.magic = PROTOCOL_MAGIC;
    std::memset(hdr.command, 0, sizeof(hdr.command));
    std::memcpy(hdr.command, command.data(), command.size());
    hdr.length = payload.size();
    
    crypto::Hash256 hash = crypto::doubleSha256(payload.data(), payload.size());
    std::memcpy(&hdr.checksum, hash.data(), 4);
    
    out.resize(sizeof(MessageHeader) + payload.size());
    std::memcpy(out.data(), &hdr, sizeof(MessageHeader));
    std::memcpy(out.data() + sizeof(MessageHeader), payload.data(), payload.size());
    return out;
}

Message Message::deserialize(const std::vector<uint8_t>& data) {
    Message msg;
    if (data.size() < sizeof(MessageHeader)) return msg;
    
    MessageHeader hdr;
    std::memcpy(&hdr, data.data(), sizeof(MessageHeader));
    
    if (hdr.magic != PROTOCOL_MAGIC) return msg;
    if (hdr.length > MAX_MESSAGE_SIZE) return msg;
    if (!isValidCommand(hdr.command)) return msg;
    if (data.size() != sizeof(MessageHeader) + hdr.length) return msg;
    
    msg.command = std::string(hdr.command, strnlen(hdr.command, 12));
    if (msg.command.empty()) return Message{};
    msg.payload.assign(data.begin() + sizeof(MessageHeader), 
                       data.begin() + sizeof(MessageHeader) + hdr.length);
    
    crypto::Hash256 hash = crypto::doubleSha256(msg.payload.data(), msg.payload.size());
    uint32_t checksum;
    std::memcpy(&checksum, hash.data(), 4);
    if (checksum != hdr.checksum) {
        msg.command.clear();
        msg.payload.clear();
    }
    
    return msg;
}

struct Network::Impl {
    std::unordered_map<std::string, Peer> peers;
    std::unordered_map<std::string, uint64_t> bannedPeers;
    std::unordered_map<std::string, PeerRxState> rx;
    std::unordered_map<std::string, scale::PenaltyState> penalties;
    mutable std::mutex mtx;
    std::atomic<bool> running{false};
    uint16_t port = 0;
    int listenSocket = -1;
    std::thread acceptThread;
    std::thread recvThread;
    NetworkConfig config;
    uint64_t startTime = 0;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint64_t messagesSent = 0;
    uint64_t messagesReceived = 0;
    uint64_t rejectedConnections = 0;
    uint64_t evictedPeers = 0;
    uint64_t tempBans = 0;
    uint64_t malformedMessages = 0;
    uint64_t rateLimitedEvents = 0;
    uint64_t overloadTransitions = 0;
    bool overloadMode = false;
    // outbound queue tracking (per-peer)
    std::unordered_map<std::string, std::deque<std::vector<uint8_t>>> outboundQueues;
    std::unordered_map<std::string, size_t> outboundQueueBytes;
    // statistics for idle/zombie disconnects
    uint64_t zombieDisconnects = 0;
    
    std::function<void(const std::string&, const Message&)> messageHandler;
    std::function<void(const Peer&)> connectHandler;
    std::function<void(const Peer&)> disconnectHandler;
    
    uint64_t bufferedRxBytesLocked() const {
        uint64_t total = 0;
        for (const auto& [peerId, state] : rx) {
            total += static_cast<uint64_t>(state.buffer.size());
        }
        return total;
    }

    void pruneExpiredBansLocked(uint64_t now) {
        for (auto it = bannedPeers.begin(); it != bannedPeers.end();) {
            if (it->second <= now) {
                it = bannedPeers.erase(it);
            } else {
                ++it;
            }
        }
    }

    void updateOverloadModeLocked(uint64_t now) {
        (void)now;
        if (!config.enableOverloadMode) {
            if (overloadMode) {
                overloadMode = false;
                overloadTransitions += 1;
            }
            return;
        }
        const uint32_t enterPeerPercent = std::min<uint32_t>(100, std::max<uint32_t>(1, config.overloadEnterPeerPercent));
        const uint32_t exitPeerPercent = std::min<uint32_t>(enterPeerPercent, std::max<uint32_t>(0, config.overloadExitPeerPercent));
        const uint64_t peerPercent = config.maxPeers == 0
            ? 0
            : static_cast<uint64_t>(peers.size() * 100ULL / config.maxPeers);
        const uint64_t buffered = bufferedRxBytesLocked();
        const bool shouldEnter = peerPercent >= enterPeerPercent ||
            buffered >= config.overloadEnterBufferedRxBytes;
        const bool shouldExit = peerPercent <= exitPeerPercent &&
            buffered <= config.overloadExitBufferedRxBytes;

        if (!overloadMode && shouldEnter) {
            overloadMode = true;
            overloadTransitions += 1;
            utils::Logger::warn("network overload mode entered (peerPressure=" +
                std::to_string(peerPercent) + "% bufferedRx=" + std::to_string(buffered) + ")");
        } else if (overloadMode && shouldExit) {
            overloadMode = false;
            overloadTransitions += 1;
            utils::Logger::info("network overload mode exited (peerPressure=" +
                std::to_string(peerPercent) + "% bufferedRx=" + std::to_string(buffered) + ")");
        }
    }

    std::vector<scale::PeerSnapshot> buildPeerSnapshotsLocked() const {
        std::vector<scale::PeerSnapshot> out;
        out.reserve(peers.size());
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        for (const auto& [peerId, peer] : peers) {
            scale::PeerSnapshot s;
            s.id = peerId;
            s.address = peer.address;
            s.isOutbound = peer.isOutbound;
            s.connectedAt = peer.connectedAt;
            s.lastSeen = peer.lastSeen;
            s.bytesReceived = peer.bytesRecv;
            auto it = penalties.find(peer.address);
            if (it != penalties.end()) {
                s.penaltyScore = scale::decayPenalty(it->second.score, now > it->second.lastUpdateSec ? now - it->second.lastUpdateSec : 0, config.penaltyHalfLifeSeconds);
            } else {
                s.penaltyScore = 0.0;
            }
            out.push_back(std::move(s));
        }
        return out;
    }

    std::optional<Peer> removePeerLocked(const std::string& peerId) {
        auto it = peers.find(peerId);
        if (it == peers.end()) return std::nullopt;
        Peer removed = it->second;
        if (it->second.socket >= 0) close(it->second.socket);
        peers.erase(it);
        rx.erase(peerId);
        // clear any queued outbound messages and accounting for this peer
        auto oqit = outboundQueues.find(peerId);
        if (oqit != outboundQueues.end()) {
            outboundQueues.erase(oqit);
        }
        auto obit = outboundQueueBytes.find(peerId);
        if (obit != outboundQueueBytes.end()) outboundQueueBytes.erase(obit);
        return removed;
    }

    uint32_t addPenaltyAndMaybeBanLocked(const std::string& address, double deltaPenalty, uint64_t now) {
        if (address.empty()) return 0;
        auto& p = penalties[address];
        const double score = scale::addPenalty(p, now, deltaPenalty, config.penaltyHalfLifeSeconds);
        const uint32_t banSeconds = scale::banSecondsFromPenalty(score, config.baseBanSeconds, config.maxBanSeconds);
        if (banSeconds > 0) {
            bannedPeers[address] = now + banSeconds;
            tempBans += 1;
        }
        return banSeconds;
    }

    void acceptLoop();
    void recvLoop();
    bool sendRaw(int sock, const std::vector<uint8_t>& data);
};

void Network::Impl::acceptLoop() {
    while (running) {
        struct pollfd pfd;
        pfd.fd = listenSocket;
        pfd.events = POLLIN;
        
        if (poll(&pfd, 1, 100) <= 0) continue;
        
        struct sockaddr_storage clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(listenSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);

        if (clientSock < 0) continue;

        setNonBlocking(clientSock);

        std::string addr;
        uint16_t clientPort = 0;
        if (clientAddr.ss_family == AF_INET6) {
            auto* sa6 = reinterpret_cast<struct sockaddr_in6*>(&clientAddr);
            clientPort = ntohs(sa6->sin6_port);
            auto* raw = sa6->sin6_addr.s6_addr;
            bool v4mapped = (memcmp(raw, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12) == 0);
            if (v4mapped) {
                struct in_addr v4;
                memcpy(&v4, raw + 12, 4);
                char buf[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &v4, buf, sizeof(buf));
                addr = buf;
            } else {
                char buf[INET6_ADDRSTRLEN]{};
                inet_ntop(AF_INET6, &sa6->sin6_addr, buf, sizeof(buf));
                addr = buf;
            }
        } else {
            auto* sa4 = reinterpret_cast<struct sockaddr_in*>(&clientAddr);
            clientPort = ntohs(sa4->sin_port);
            char buf[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &sa4->sin_addr, buf, sizeof(buf));
            addr = buf;
        }
        std::string peerId = addr + ":" + std::to_string(clientPort);
        
        Peer peer;
        bool accepted = false;
        std::optional<Peer> evicted;
        {
            std::lock_guard<std::mutex> lock(mtx);
            const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
            pruneExpiredBansLocked(now);
            updateOverloadModeLocked(now);

            auto banIt = bannedPeers.find(addr);
            if (banIt != bannedPeers.end()) {
                if (now < banIt->second) {
                    close(clientSock);
                    continue;
                }
                bannedPeers.erase(banIt);
            }

            bool allow = true;
            std::string evictPeerId;
            if (config.adaptiveAdmission) {
                scale::AdmissionLimits limits;
                limits.maxPeers = config.maxPeers;
                limits.maxInbound = config.maxInbound;
                limits.maxOutbound = config.maxOutbound;
                limits.maxPeersPerIp = config.maxPeersPerIp;
                limits.maxPeersPerSubnet = config.maxPeersPerSubnet;
                limits.subnetPrefixBits = config.subnetPrefixBits;
                limits.overloadMode = overloadMode;
                limits.enableDeterministicEviction = config.deterministicEviction;

                auto decision = scale::evaluateInboundAdmission(buildPeerSnapshotsLocked(), addr, limits);
                allow = decision.accept;
                evictPeerId = decision.evictPeerId;
            } else {
                if (peers.size() >= config.maxPeers) {
                    allow = false;
                }
                if (allow) {
                    size_t inboundCount = 0;
                    for (const auto& kv : peers) {
                        if (!kv.second.isOutbound) inboundCount++;
                    }
                    if (inboundCount >= config.maxInbound) {
                        allow = false;
                    }
                }
            }

            if (!allow) {
                rejectedConnections += 1;
                close(clientSock);
                continue;
            }

            if (!evictPeerId.empty()) {
                auto removed = removePeerLocked(evictPeerId);
                if (removed) {
                    evicted = removed;
                    evictedPeers += 1;
                } else {
                    rejectedConnections += 1;
                    close(clientSock);
                    continue;
                }
            }

            peer.id = peerId;
            peer.address = addr;
            peer.port = clientPort;
            peer.connectedAt = std::time(nullptr);
            peer.lastSeen = peer.connectedAt;
            peer.bytesRecv = 0;
            peer.bytesSent = 0;
            peer.version = 0;
            peer.startHeight = 0;
            peer.isOutbound = false;
            peer.state = PeerState::CONNECTED;
            peer.socket = clientSock;

            peers[peerId] = peer;
            rx[peerId] = PeerRxState{};
            accepted = true;
        }
        if (evicted && disconnectHandler) disconnectHandler(*evicted);
        if (accepted && connectHandler) connectHandler(peer);
    }
}

void Network::Impl::recvLoop() {
    while (running) {
        std::vector<struct pollfd> fds;
        std::vector<std::string> peerIds;
        
        std::vector<Peer> removedPeers;
        {
            std::lock_guard<std::mutex> lock(mtx);
            const uint64_t nowSec = static_cast<uint64_t>(std::time(nullptr));
            std::vector<std::string> toRemove;
            for (const auto& [id, peer] : peers) {
                if (peer.state == PeerState::CONNECTED && peer.socket >= 0) {
                    if (nowSec > peer.lastSeen && (nowSec - peer.lastSeen) > (config.pingInterval * 3)) {
                        toRemove.push_back(id);
                    }
                }
            }
            for (const auto& id : toRemove) {
                auto removed = removePeerLocked(id);
                if (removed) {
                    removedPeers.push_back(*removed);
                    zombieDisconnects += 1;
                }
            }

            for (auto& [id, peer] : peers) {
                if (peer.state == PeerState::CONNECTED && peer.socket >= 0) {
                    struct pollfd pfd;
                    pfd.fd = peer.socket;
                    pfd.events = POLLIN;
                    auto oqit = outboundQueues.find(id);
                    if (oqit != outboundQueues.end() && !oqit->second.empty()) pfd.events |= POLLOUT;
                    fds.push_back(pfd);
                    peerIds.push_back(id);
                }
            }
        }
        for (auto& p : removedPeers) {
            if (disconnectHandler) disconnectHandler(p);
        }
        
        if (fds.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        int ret = poll(fds.data(), fds.size(), 100);
        if (ret <= 0) continue;
        
        for (size_t i = 0; i < fds.size(); i++) {
            if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                std::optional<Peer> dropped;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    dropped = removePeerLocked(peerIds[i]);
                }
                if (dropped && disconnectHandler) disconnectHandler(*dropped);
                continue;
            }
            if (!(fds[i].revents & POLLIN)) continue;

            std::vector<Message> decoded;
            decoded.reserve(4);
            std::optional<Peer> droppedPeer;

            {
                std::lock_guard<std::mutex> lock(mtx);
                auto pit = peers.find(peerIds[i]);
                if (pit == peers.end()) continue;

                const uint64_t nowSec = static_cast<uint64_t>(std::time(nullptr));
                const uint64_t nowMs = monotonicMs();
                pruneExpiredBansLocked(nowSec);
                updateOverloadModeLocked(nowSec);

                auto& peer = pit->second;
                // flush any queued outbound messages if socket writable
                if (fds[i].revents & POLLOUT) {
                    auto oqit = outboundQueues.find(peerIds[i]);
                    if (oqit != outboundQueues.end()) {
                        auto &dq = oqit->second;
                        auto& qbytes = outboundQueueBytes[peerIds[i]];
                        while (!dq.empty()) {
                            const auto &buf = dq.front();
                            if (sendRaw(peer.socket, buf)) {
                                peer.bytesSent += buf.size();
                                if (qbytes >= buf.size()) qbytes -= buf.size();
                                else qbytes = 0;
                                dq.pop_front();
                            } else {
                                break;
                            }
                        }
                        if (dq.empty()) {
                            outboundQueues.erase(oqit);
                            outboundQueueBytes.erase(peerIds[i]);
                        }
                    }
                }

                auto& st = rx[peerIds[i]];

                enum class DropAction {
                    NONE,
                    DISCONNECT,
                    TEMP_BAN
                };
                DropAction dropAction = DropAction::NONE;
                double penaltyDelta = 0.0;

                std::array<uint8_t, 64 * 1024> tmp{};
                for (;;) {
                    ssize_t n = ::recv(peer.socket, tmp.data(), tmp.size(), 0);
                    if (n > 0) {
                        if (config.enableTokenBucketDosGuard) {
                            const bool allowed = scale::consumeTokenBucket(
                                st.byteBucket,
                                nowMs,
                                static_cast<double>(config.tokenBucketBytesPerSecond),
                                static_cast<double>(config.tokenBucketBytesBurst),
                                static_cast<double>(n));
                            if (!allowed) {
                                dropAction = DropAction::TEMP_BAN;
                                penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.ratePenaltyScore));
                                rateLimitedEvents += 1;
                                break;
                            }
                        }

                        peer.lastSeen = nowSec;
                        peer.bytesRecv += static_cast<uint64_t>(n);
                        bytesReceived += static_cast<uint64_t>(n);

                        if (st.buffer.size() + static_cast<size_t>(n) > RX_BUFFER_LIMIT) {
                            dropAction = DropAction::TEMP_BAN;
                            penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.ratePenaltyScore));
                            rateLimitedEvents += 1;
                            break;
                        }
                        st.buffer.insert(st.buffer.end(), tmp.data(), tmp.data() + n);
                        continue;
                    }
                    if (n == 0) {
                        dropAction = DropAction::DISCONNECT;
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    dropAction = DropAction::TEMP_BAN;
                    penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.ratePenaltyScore / 2));
                    rateLimitedEvents += 1;
                    break;
                }

                if (dropAction == DropAction::NONE) {
                    size_t consumed = 0;
                    while (st.buffer.size() - consumed >= sizeof(MessageHeader)) {
                        MessageHeader hdr{};
                        std::memcpy(&hdr, st.buffer.data() + consumed, sizeof(MessageHeader));
                        if (hdr.magic != PROTOCOL_MAGIC) {
                            dropAction = DropAction::TEMP_BAN;
                            penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.malformedPenaltyScore));
                            malformedMessages += 1;
                            break;
                        }
                        if (!isValidCommand(hdr.command)) {
                            dropAction = DropAction::TEMP_BAN;
                            penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.malformedPenaltyScore));
                            malformedMessages += 1;
                            break;
                        }
                        if (hdr.length > MAX_MESSAGE_SIZE) {
                            dropAction = DropAction::TEMP_BAN;
                            penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.malformedPenaltyScore));
                            malformedMessages += 1;
                            break;
                        }
                        const size_t total = sizeof(MessageHeader) + static_cast<size_t>(hdr.length);
                        if (st.buffer.size() - consumed < total) break;

                        std::vector<uint8_t> frame;
                        frame.insert(frame.end(),
                                     st.buffer.begin() + static_cast<std::ptrdiff_t>(consumed),
                                     st.buffer.begin() + static_cast<std::ptrdiff_t>(consumed + total));
                        consumed += total;

                        Message msg = Message::deserialize(frame);
                        if (!msg.command.empty()) {
                            if (config.enableTokenBucketDosGuard) {
                                const bool allowed = scale::consumeTokenBucket(
                                    st.messageBucket,
                                    nowMs,
                                    static_cast<double>(config.tokenBucketMessagesPerSecond),
                                    static_cast<double>(config.tokenBucketMessagesBurst),
                                    1.0);
                                if (!allowed) {
                                    dropAction = DropAction::TEMP_BAN;
                                    penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.ratePenaltyScore));
                                    rateLimitedEvents += 1;
                                    break;
                                }
                            }
                            msg.from = peer.id;
                            msg.timestamp = nowSec;
                            decoded.push_back(std::move(msg));
                            messagesReceived += 1;
                        } else {
                            dropAction = DropAction::TEMP_BAN;
                            penaltyDelta = static_cast<double>(std::max<uint32_t>(1, config.malformedPenaltyScore));
                            malformedMessages += 1;
                            break;
                        }
                    }
                    if (consumed > 0) {
                        st.buffer.erase(st.buffer.begin(), st.buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
                    }
                }

                if (dropAction != DropAction::NONE) {
                    droppedPeer = removePeerLocked(peer.id);
                    if (droppedPeer && dropAction == DropAction::TEMP_BAN) {
                        addPenaltyAndMaybeBanLocked(
                            droppedPeer->address,
                            penaltyDelta <= 0.0 ? static_cast<double>(std::max<uint32_t>(1, config.ratePenaltyScore)) : penaltyDelta,
                            nowSec);
                    }
                }
                updateOverloadModeLocked(nowSec);
            }

            if (droppedPeer && disconnectHandler) disconnectHandler(*droppedPeer);
            if (!decoded.empty() && messageHandler) {
                for (auto& msg : decoded) {
                    messageHandler(peerIds[i], msg);
                }
            }
        }
    }
}

bool Network::Impl::sendRaw(int sock, const std::vector<uint8_t>& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(sock, data.data() + sent, data.size() - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) return false;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            struct pollfd pfd;
            pfd.fd = sock;
            pfd.events = POLLOUT;
            int pr = poll(&pfd, 1, 250);
            if (pr <= 0) return false;
            continue;
        }
        return false;
    }
    bytesSent += data.size();
    messagesSent++;
    return true;
}

Network::Network() : impl_(std::make_unique<Impl>()) {}

Network::~Network() { stop(); }

bool Network::start(uint16_t port) {
    if (impl_->running) return false;
    
    std::vector<uint16_t> portsToTry = {port, 8334, 8335, 8336, 8337, 8338, 8339, 8340, 18333, 28333, 38333, 48333};
    
    for (uint16_t tryPort : portsToTry) {
        int sock = -1;
        bool isDualStack = false;

        // Try IPv6 dual-stack first
        sock = socket(AF_INET6, SOCK_STREAM, 0);
        if (sock >= 0) {
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
            setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
            int v6only = 0;
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
            int flags = fcntl(sock, F_GETFL, 0);
            if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);

            struct sockaddr_in6 addr6{};
            addr6.sin6_family = AF_INET6;
            addr6.sin6_addr = in6addr_any;
            addr6.sin6_port = htons(tryPort);

            if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr6), sizeof(addr6)) == 0 && listen(sock, 10) == 0) {
                isDualStack = true;
            } else {
                close(sock);
                sock = -1;
            }
        }

        // Fallback to IPv4 if IPv6 dual-stack failed
        if (sock < 0) {
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) continue;
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
            setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
            int flags = fcntl(sock, F_GETFL, 0);
            if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);

            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(tryPort);

            if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 && listen(sock, 10) == 0) {
                // success
            } else {
                int err = errno;
                utils::Logger::warn("Failed to bind/listen on port " + std::to_string(tryPort) + " errno=" + std::to_string(err));
                close(sock);
                continue;
            }
        }

        impl_->listenSocket = sock;
        impl_->port = tryPort;
        impl_->running = true;
        impl_->startTime = std::time(nullptr);
        impl_->acceptThread = std::thread(&Impl::acceptLoop, impl_.get());
        impl_->recvThread = std::thread(&Impl::recvLoop, impl_.get());
        utils::Logger::info("Network listening on port " + std::to_string(tryPort));
        return true;
    }
    
    impl_->port = 0;
    impl_->listenSocket = -1;
    utils::Logger::error("Network failed to bind any port");
    return false;
}

void Network::stop() {
    if (!impl_->running) return;
    
    impl_->running = false;
    
    int ls = impl_->listenSocket;
    if (ls >= 0) {
        shutdown(ls, SHUT_RDWR);
        close(ls);
        impl_->listenSocket = -1;
    }
    
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        for (auto& [id, peer] : impl_->peers) {
            if (peer.socket >= 0) {
                shutdown(peer.socket, SHUT_RDWR);
                close(peer.socket);
                peer.socket = -1;
            }
        }
        impl_->peers.clear();
        impl_->rx.clear();
    }
    
    if (impl_->acceptThread.joinable()) impl_->acceptThread.join();
    if (impl_->recvThread.joinable()) impl_->recvThread.join();
    impl_->port = 0;
}

bool Network::isRunning() const {
    return impl_->running;
}

bool Network::connect(const std::string& address, uint16_t port) {
    std::string peerId = address + ":" + std::to_string(port);
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        impl_->pruneExpiredBansLocked(now);
        impl_->updateOverloadModeLocked(now);
        auto banIt = impl_->bannedPeers.find(address);
        if (banIt != impl_->bannedPeers.end() && now < banIt->second) {
            return false;
        }
        if (impl_->peers.find(peerId) != impl_->peers.end()) {
            return true;
        }

        if (impl_->config.adaptiveAdmission) {
            scale::AdmissionLimits limits;
            limits.maxPeers = impl_->config.maxPeers;
            limits.maxInbound = impl_->config.maxInbound;
            limits.maxOutbound = impl_->config.maxOutbound;
            limits.maxPeersPerIp = impl_->config.maxPeersPerIp;
            limits.maxPeersPerSubnet = impl_->config.maxPeersPerSubnet;
            limits.subnetPrefixBits = impl_->config.subnetPrefixBits;
            limits.overloadMode = impl_->overloadMode;
            limits.enableDeterministicEviction = false;
            std::string reason;
            if (!scale::allowOutboundConnection(impl_->buildPeerSnapshotsLocked(), address, limits, &reason)) {
                impl_->rejectedConnections += 1;
                return false;
            }
        }
    }
    
    int sock = -1;
    bool connected = false;
    const bool isOnionAddr = address.size() > 6 &&
        address.compare(address.size() - 6, 6, ".onion") == 0;
    const bool hasSocksProxy = impl_->config.useSocksProxy &&
        !impl_->config.socksProxyHost.empty() && impl_->config.socksProxyPort > 0;
    const bool needSocks = impl_->config.useSocksProxy ||
        (impl_->config.hybridMode && isOnionAddr && hasSocksProxy);

    if (needSocks && hasSocksProxy) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        static constexpr int SOCKS5_CONNECT_TIMEOUT_MS = 5000;
        struct timeval tvSocks;
        tvSocks.tv_sec = SOCKS5_CONNECT_TIMEOUT_MS / 1000;
        tvSocks.tv_usec = (SOCKS5_CONNECT_TIMEOUT_MS % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tvSocks, sizeof(tvSocks));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tvSocks, sizeof(tvSocks));
        struct sockaddr_in proxyAddr{};
        proxyAddr.sin_family = AF_INET;
        proxyAddr.sin_port = htons(impl_->config.socksProxyPort);
        if (!resolveIpv4Host(impl_->config.socksProxyHost, &proxyAddr.sin_addr)) {
            close(sock);
            return false;
        }
        if (::connect(sock, reinterpret_cast<struct sockaddr*>(&proxyAddr), sizeof(proxyAddr)) == 0) {
            connected = socks5ConnectTarget(sock, address, port);
        }
        if (!connected) {
            close(sock);
            sock = -1;
            if (impl_->config.torRequired && isOnionAddr) {
                return false;
            }
            if (impl_->config.torRequired && !impl_->config.hybridMode) {
                return false;
            }
        }
    }

    if (!connected && !isOnionAddr) {
        if (impl_->config.torRequired && !impl_->config.hybridMode) {
            return false;
        }
        struct sockaddr_storage addrStorage{};
        socklen_t addrLen = 0;
        if (!resolveHost(address, port, &addrStorage, &addrLen)) {
            return false;
        }
        int family = reinterpret_cast<struct sockaddr*>(&addrStorage)->sa_family;
        sock = socket(family, SOCK_STREAM, 0);
        if (sock < 0) return false;
        connected = (::connect(sock, reinterpret_cast<struct sockaddr*>(&addrStorage), addrLen) == 0);
    }

    if (isOnionAddr && !connected) {
        return false;
    }

    if (!connected) {
        close(sock);
        return false;
    }

    setNonBlocking(sock);
    
    Peer peer;
    peer.id = peerId;
    peer.address = address;
    peer.port = port;
    peer.connectedAt = std::time(nullptr);
    peer.lastSeen = peer.connectedAt;
    peer.bytesRecv = 0;
    peer.bytesSent = 0;
    peer.version = 0;
    peer.startHeight = 0;
    peer.isOutbound = true;
    peer.state = PeerState::CONNECTED;
    peer.socket = sock;

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        impl_->pruneExpiredBansLocked(now);
        auto banIt = impl_->bannedPeers.find(address);
        if (banIt != impl_->bannedPeers.end() && now < banIt->second) {
            close(sock);
            return false;
        }

        if (impl_->peers.find(peerId) != impl_->peers.end()) {
            close(sock);
            return true;
        }

        if (impl_->config.adaptiveAdmission) {
            scale::AdmissionLimits limits;
            limits.maxPeers = impl_->config.maxPeers;
            limits.maxInbound = impl_->config.maxInbound;
            limits.maxOutbound = impl_->config.maxOutbound;
            limits.maxPeersPerIp = impl_->config.maxPeersPerIp;
            limits.maxPeersPerSubnet = impl_->config.maxPeersPerSubnet;
            limits.subnetPrefixBits = impl_->config.subnetPrefixBits;
            limits.overloadMode = impl_->overloadMode;
            limits.enableDeterministicEviction = false;
            std::string reason;
            if (!scale::allowOutboundConnection(impl_->buildPeerSnapshotsLocked(), address, limits, &reason)) {
                impl_->rejectedConnections += 1;
                close(sock);
                return false;
            }
        } else {
            size_t outboundCount = 0;
            for (const auto& kv : impl_->peers) {
                if (kv.second.isOutbound) outboundCount++;
            }
            if (outboundCount >= impl_->config.maxOutbound) {
                close(sock);
                return false;
            }
        }

        impl_->peers[peerId] = peer;
        impl_->rx[peerId] = PeerRxState{};
        impl_->updateOverloadModeLocked(now);
    }
    if (impl_->connectHandler) impl_->connectHandler(peer);
    return true;
}

void Network::disconnect(const std::string& peerId) {
    std::optional<Peer> removed;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        removed = impl_->removePeerLocked(peerId);
        impl_->updateOverloadModeLocked(static_cast<uint64_t>(std::time(nullptr)));
    }
    if (removed && impl_->disconnectHandler) impl_->disconnectHandler(*removed);
}

void Network::ban(const std::string& peerId, uint32_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->peers.find(peerId);
    if (it != impl_->peers.end()) {
        impl_->bannedPeers[it->second.address] = std::time(nullptr) + seconds;
        if (it->second.socket >= 0) close(it->second.socket);
        impl_->peers.erase(it);
        impl_->rx.erase(peerId);
        impl_->updateOverloadModeLocked(static_cast<uint64_t>(std::time(nullptr)));
        return;
    }

    std::string addrKey = banAddressKeyFromPeerId(peerId);
    if (!addrKey.empty()) {
        impl_->bannedPeers[addrKey] = std::time(nullptr) + seconds;
    }
}

bool Network::broadcast(const Message& msg) {
    std::vector<uint8_t> data = msg.serialize();
    std::vector<std::string> targets;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        for (const auto& [id, peer] : impl_->peers) {
            if (peer.state == PeerState::CONNECTED && peer.socket >= 0) targets.push_back(id);
        }
    }
    for (const auto& id : targets) {
        send(id, msg);
    }
    return true;
}

bool Network::send(const std::string& peerId, const Message& msg) {
    std::vector<uint8_t> data = msg.serialize();
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->peers.find(peerId);
    if (it == impl_->peers.end()) return false;
    if (it->second.state != PeerState::CONNECTED) return false;
    if (it->second.socket < 0) return false;
    // try immediate send
    if (impl_->sendRaw(it->second.socket, data)) {
        it->second.bytesSent += data.size();
        return true;
    }

    // enqueue outbound if send would block or fail transiently
    auto &dq = impl_->outboundQueues[peerId];
    dq.push_back(data);
    auto &qbytes = impl_->outboundQueueBytes[peerId];
    qbytes += data.size();

    // enforce per-peer outbound queue limit by dropping oldest messages first
    while (qbytes > impl_->config.maxOutboundQueueBytes && !dq.empty()) {
        const auto &old = dq.front();
        if (qbytes >= old.size()) qbytes -= old.size();
        else qbytes = 0;
        dq.pop_front();
    }

    // if queue still exceeds limit after dropping, disconnect peer
    if (qbytes > impl_->config.maxOutboundQueueBytes) {
        impl_->zombieDisconnects += 1;
        auto removed = impl_->removePeerLocked(peerId);
        if (removed && impl_->disconnectHandler) impl_->disconnectHandler(*removed);
        return false;
    }
    return true;
}

std::vector<Peer> Network::getPeers() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Peer> result;
    for (const auto& [id, peer] : impl_->peers) {
        result.push_back(peer);
    }
    return result;
}

Peer Network::getPeer(const std::string& peerId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->peers.find(peerId);
    if (it != impl_->peers.end()) return it->second;
    return Peer{};
}

size_t Network::peerCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->peers.size();
}

size_t Network::outboundCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    size_t count = 0;
    for (const auto& [id, peer] : impl_->peers) {
        if (peer.isOutbound) count++;
    }
    return count;
}

void Network::onMessage(std::function<void(const std::string&, const Message&)> handler) {
    impl_->messageHandler = handler;
}

void Network::onPeerConnected(std::function<void(const Peer&)> handler) {
    impl_->connectHandler = handler;
}

void Network::onPeerDisconnected(std::function<void(const Peer&)> handler) {
    impl_->disconnectHandler = handler;
}

uint16_t Network::getPort() const {
    return impl_->port;
}

std::string Network::getLocalAddress() const {
    return "0.0.0.0";
}

NetworkStats Network::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    NetworkStats stats{};
    stats.totalPeers = impl_->peers.size();
    stats.inboundPeers = 0;
    stats.outboundPeers = 0;
    stats.bytesSent = impl_->bytesSent;
    stats.bytesReceived = impl_->bytesReceived;
    stats.messagesSent = impl_->messagesSent;
    stats.messagesReceived = impl_->messagesReceived;
    stats.uptime = std::time(nullptr) - impl_->startTime;
    stats.overloadMode = impl_->overloadMode;
    stats.bufferedRxBytes = impl_->bufferedRxBytesLocked();
    stats.rejectedConnections = impl_->rejectedConnections;
    stats.evictedPeers = impl_->evictedPeers;
    stats.tempBans = impl_->tempBans;
    stats.malformedMessages = impl_->malformedMessages;
    stats.rateLimitedEvents = impl_->rateLimitedEvents;
    stats.overloadTransitions = impl_->overloadTransitions;
    stats.zombieDisconnects = impl_->zombieDisconnects;
    
    for (const auto& [id, peer] : impl_->peers) {
        if (peer.isOutbound) stats.outboundPeers++;
        else stats.inboundPeers++;
    }
    return stats;
}

void Network::setMaxPeers(uint32_t max) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.maxPeers = max;
}

void Network::setMaxInbound(uint32_t max) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.maxInbound = max;
}

void Network::setMaxOutbound(uint32_t max) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.maxOutbound = max;
}

bool Network::banPeer(const std::string& peerId, uint32_t duration) {
    ban(peerId, duration);
    return true;
}

bool Network::unbanPeer(const std::string& peerId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string key = peerId;
    auto it = impl_->peers.find(peerId);
    if (it != impl_->peers.end()) {
        key = it->second.address;
    } else {
        key = banAddressKeyFromPeerId(peerId);
    }
    if (!key.empty()) impl_->bannedPeers.erase(key);
    if (key != peerId) impl_->bannedPeers.erase(peerId);
    return true;
}

bool Network::isBanned(const std::string& peerId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->bannedPeers.find(peerId);
    if (it != impl_->bannedPeers.end()) {
        return std::time(nullptr) < static_cast<time_t>(it->second);
    }

    std::string key = peerId;
    auto pit = impl_->peers.find(peerId);
    if (pit != impl_->peers.end()) {
        key = pit->second.address;
    } else {
        key = banAddressKeyFromPeerId(peerId);
    }
    if (key.empty()) return false;

    auto bit = impl_->bannedPeers.find(key);
    if (bit == impl_->bannedPeers.end()) return false;
    return std::time(nullptr) < static_cast<time_t>(bit->second);
}

std::vector<std::string> Network::getBannedPeers() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> result;
    uint64_t now = std::time(nullptr);
    for (const auto& [id, until] : impl_->bannedPeers) {
        if (now < until) result.push_back(id);
    }
    return result;
}

void Network::setConfig(const NetworkConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
    if (impl_->config.maxPeers == 0) impl_->config.maxPeers = 1;
    if (impl_->config.maxInbound > impl_->config.maxPeers) impl_->config.maxInbound = impl_->config.maxPeers;
    if (impl_->config.maxOutbound > impl_->config.maxPeers) impl_->config.maxOutbound = impl_->config.maxPeers;
    if (impl_->config.subnetPrefixBits > 32) impl_->config.subnetPrefixBits = 32;
    if (impl_->config.maxBanSeconds < impl_->config.baseBanSeconds) {
        impl_->config.maxBanSeconds = impl_->config.baseBanSeconds;
    }
    if (impl_->config.overloadEnterPeerPercent == 0) impl_->config.overloadEnterPeerPercent = 1;
    if (impl_->config.overloadEnterPeerPercent > 100) impl_->config.overloadEnterPeerPercent = 100;
    if (impl_->config.overloadExitPeerPercent > impl_->config.overloadEnterPeerPercent) {
        impl_->config.overloadExitPeerPercent = impl_->config.overloadEnterPeerPercent;
    }
    if (impl_->config.overloadExitPeerPercent > 100) impl_->config.overloadExitPeerPercent = 100;
    impl_->updateOverloadModeLocked(static_cast<uint64_t>(std::time(nullptr)));
}

NetworkConfig Network::getConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->config;
}

bool Network::isConnected() const {
    return impl_->running && peerCount() > 0;
}

void Network::ping(const std::string& peerId) {
    Message msg;
    msg.type = MessageType::PING;
    msg.command = "ping";
    msg.timestamp = std::time(nullptr);
    uint64_t nonce = (static_cast<uint64_t>(std::random_device{}()) << 32) | std::random_device{}();
    msg.payload.resize(8);
    for (int i = 0; i < 8; i++) {
        msg.payload[i] = static_cast<uint8_t>((nonce >> (i * 8)) & 0xff);
    }
    send(peerId, msg);
}

void Network::pingAll() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        for (const auto& [id, peer] : impl_->peers) {
            ids.push_back(id);
        }
    }
    for (const auto& id : ids) {
        ping(id);
    }
}

}
}
