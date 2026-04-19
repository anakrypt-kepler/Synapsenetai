#pragma once

#include "crypto/crypto.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace synapse::core {

struct AgentStorageAuditPolicy {
    uint32_t maxSegments = 8;
    uint64_t maxSegmentBytes = 1024 * 1024;
};

struct AgentStorageAuditEvent {
    uint64_t sequence = 0;
    uint64_t timestamp = 0;
    std::string kind;
    std::string objectId;
    std::string payload;
    crypto::Hash256 prevHash{};
    crypto::Hash256 hash{};
};

struct AgentStorageAuditStats {
    std::string rootDir;
    uint64_t segmentCount = 0;
    uint64_t retainedEvents = 0;
    uint64_t lastSequence = 0;
    crypto::Hash256 lastHash{};
    uint64_t recoveredTruncatedLines = 0;
    uint64_t droppedSegments = 0;
};

class AgentStorageAuditLog {
public:
    explicit AgentStorageAuditLog(const AgentStorageAuditPolicy& policy = AgentStorageAuditPolicy{});

    AgentStorageAuditPolicy policy() const;
    void setPolicy(const AgentStorageAuditPolicy& policy);

    bool open(const std::string& rootDir, std::string* reason = nullptr);

    bool append(uint64_t atTimestamp,
                const std::string& kind,
                const std::string& objectId,
                const std::string& payload,
                std::string* reason = nullptr);

    AgentStorageAuditStats stats() const;

private:
    struct SegmentState {
        uint64_t index = 0;
        std::string path;
        uint64_t bytes = 0;
        uint64_t events = 0;
    };

    AgentStorageAuditPolicy policy_;
    mutable std::mutex mtx_;

    std::string rootDir_;
    std::string auditDir_;
    std::vector<SegmentState> segments_;
    uint64_t retainedEvents_ = 0;
    uint64_t lastSequence_ = 0;
    crypto::Hash256 lastHash_{};
    uint64_t recoveredTruncatedLines_ = 0;
    uint64_t droppedSegments_ = 0;
    bool opened_ = false;

    bool appendLocked(uint64_t atTimestamp,
                      const std::string& kind,
                      const std::string& objectId,
                      const std::string& payload,
                      bool allowRotate,
                      std::string* reason);
    bool rotateLocked(uint64_t atTimestamp, std::string* reason);
    void enforceRetentionLocked();
};

}
