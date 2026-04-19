#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace synapse::core {

enum class AgentScoreBand : uint8_t {
    FULL = 0,
    THROTTLED = 1,
    REVIEW_ONLY = 2,
    LOCAL_DRAFT_ONLY = 3
};

std::string agentScoreBandToString(AgentScoreBand band);

struct AgentScorePolicy {
    int64_t scoreMin = -1000;
    int64_t scoreMax = 1000;
    int64_t initialScore = 0;

    uint32_t decayNumerator = 95;
    uint32_t decayDenominator = 100;

    int64_t acceptWeight = 12;
    int64_t rejectWeight = 30;
    int64_t violationWeight = 120;

    int64_t throttledBelowOrEqual = -60;
    int64_t reviewOnlyBelowOrEqual = -120;
    int64_t localDraftOnlyBelowOrEqual = -200;
    int64_t localDraftRecoveryAbove = -120;
    uint32_t localDraftRecoveryCleanSteps = 3;

    uint32_t normalBatchLimit = 16;
    uint32_t throttledBatchLimit = 4;
    uint32_t reviewOnlyBatchLimit = 0;
    uint32_t localDraftOnlyBatchLimit = 0;
};

struct AgentScoreSnapshot {
    int64_t score = 0;
    AgentScoreBand band = AgentScoreBand::FULL;
    bool throttled = false;
    bool quarantined = false;
    bool reviewOnly = false;
    bool localDraftOnly = false;
    uint32_t batchLimit = 0;
    uint64_t cleanSteps = 0;
    uint64_t steps = 0;
    uint64_t acceptedTotal = 0;
    uint64_t rejectedTotal = 0;
    uint64_t violationsTotal = 0;
};

class AgentScoreTracker {
public:
    explicit AgentScoreTracker(const AgentScorePolicy& policy = AgentScorePolicy{});

    AgentScoreSnapshot snapshot() const;
    AgentScorePolicy policy() const;
    void setPolicy(const AgentScorePolicy& policy);

    void applyStep(uint32_t accepted, uint32_t rejected, uint32_t violations);

    bool loadState(const std::string& path, std::string* reason = nullptr);
    bool saveState(const std::string& path, std::string* reason = nullptr) const;

private:
    static AgentScoreBand bandForScore(const AgentScorePolicy& policy, int64_t score);
    void updateBandAfterStep();
    AgentScoreSnapshot snapshotLocked() const;

    AgentScorePolicy policy_;
    mutable std::mutex mtx_;
    int64_t score_ = 0;
    AgentScoreBand band_ = AgentScoreBand::FULL;
    uint64_t cleanSteps_ = 0;
    uint64_t steps_ = 0;
    uint64_t acceptedTotal_ = 0;
    uint64_t rejectedTotal_ = 0;
    uint64_t violationsTotal_ = 0;
};

}
