#pragma once

#include "network/network.h"
#include <cstdint>
#include <string>
#include <vector>

namespace synapse {

struct NodeConfig {
    std::string dataDir;
    std::string configPath;
    std::string networkType = "mainnet";
    std::string logLevel = "info";
    std::string bindAddress = "0.0.0.0";
    uint16_t port = 8333;
    uint16_t rpcPort = 8332;
    std::string rpcBindAddress = "127.0.0.1";
    bool rpcAuthRequired = true;
    std::string rpcUser;
    std::string rpcPassword;
    std::string rpcCookieFile = "rpc.cookie";
    uint32_t maxPeers = 125;
    uint32_t maxConnections = 125;
    uint32_t maxInbound = 100;
    uint32_t maxOutbound = 25;
    bool networkAdaptiveAdmission = true;
    bool networkDeterministicEviction = true;
    uint32_t networkMaxPeersPerIp = 8;
    uint32_t networkMaxPeersPerSubnet = 32;
    uint32_t networkSubnetPrefixBits = 24;
    bool networkTokenBucketEnabled = true;
    uint32_t networkTokenBucketBytesPerSecond = static_cast<uint32_t>(network::MAX_MESSAGE_SIZE * 2);
    uint32_t networkTokenBucketBytesBurst = static_cast<uint32_t>(network::MAX_MESSAGE_SIZE * 4);
    uint32_t networkTokenBucketMessagesPerSecond = 500;
    uint32_t networkTokenBucketMessagesBurst = 1000;
    uint32_t networkMalformedPenalty = 20;
    uint32_t networkRatePenalty = 10;
    uint32_t networkPenaltyHalfLifeSeconds = 900;
    uint32_t networkBaseBanSeconds = 120;
    uint32_t networkMaxBanSeconds = 3600;
    bool networkOverloadMode = true;
    uint32_t networkOverloadEnterPeerPercent = 90;
    uint32_t networkOverloadExitPeerPercent = 70;
    uint64_t networkOverloadEnterBufferedRxBytes = network::MAX_MESSAGE_SIZE * 32;
    uint64_t networkOverloadExitBufferedRxBytes = network::MAX_MESSAGE_SIZE * 16;
    uint32_t networkInvMaxItems = 256;
    uint32_t networkInvOverloadItems = 32;
    uint32_t networkGetDataMaxItems = 128;
    uint32_t networkGetDataOverloadItems = 32;
    uint32_t networkGossipFanoutLimit = 64;
    uint32_t networkGossipDedupWindowSeconds = 5;
    uint32_t networkVoteDedupWindowSeconds = 600;
    uint32_t networkVoteDedupMaxEntries = 20000;
    uint32_t dbCacheSize = 450;
    uint32_t maxMempool = 300;
    bool daemon = false;
    bool tui = true;
    bool amnesia = false;
    bool testnet = false;
    bool regtest = false;
    bool discovery = true;
    bool networkUseHardcodedBootstrap = true;
    bool showVersion = false;
    bool showHelp = false;
    bool privacyMode = false;
    bool quantumSecurity = false;
    bool resetNgt = false;
    bool dev = false;
    std::string poeValidators;
    std::string poeValidatorMode = "static";
    std::string poeMinStake = "0";
    bool cli = false;
    std::string securityLevel = "standard";
    bool securityLevelSetByCli = false;
    bool quantumSecuritySetByCli = false;
    std::vector<std::string> connectNodes;
    std::vector<std::string> addNodes;
    std::vector<std::string> seedNodes;
    std::vector<std::string> commandArgs;
};

} // namespace synapse
