#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace synapse::core {

enum class TorBridgeLineParseStatus {
    Skip,
    Valid,
    Invalid,
};

struct TorBridgeLineParseResult {
    TorBridgeLineParseStatus status = TorBridgeLineParseStatus::Skip;
    std::string normalized;
    std::string error;
};

TorBridgeLineParseResult normalizeObfs4BridgeLine(const std::string& raw);

std::vector<std::string> sanitizeAndDedupeObfs4BridgeLines(const std::vector<std::string>& rawLines);

std::vector<std::string> selectDeterministicObfs4BridgeSubset(const std::vector<std::string>& rawLines,
                                                              const std::string& nodeId,
                                                              uint64_t epoch,
                                                              size_t maxCount);

struct TorReadinessState {
    bool socksReachable = false;
    bool webReady = false;
    bool degraded = true;
};

std::vector<std::string> describeTorReadinessTransitions(const TorReadinessState& prev,
                                                         const TorReadinessState& next);

}
