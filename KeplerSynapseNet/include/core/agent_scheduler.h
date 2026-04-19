#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace synapse::core {

enum class AgentTaskClass : uint8_t {
    RESEARCH = 0,
    VERIFY = 1,
    REVIEW = 2,
    DRAFT = 3,
    SUBMIT = 4
};

constexpr size_t kAgentTaskClassCount = 5;

size_t agentTaskClassIndex(AgentTaskClass taskClass);
std::string agentTaskClassToString(AgentTaskClass taskClass);

struct AgentTaskTokens {
    uint32_t cpu = 0;
    uint32_t ram = 0;
    uint32_t network = 0;
};

struct AgentTaskClassPolicy {
    AgentTaskTokens cost{};
    uint32_t minIntervalTicks = 1;
};

struct AgentTaskSchedulerPolicy {
    uint32_t tickSeconds = 1;
    uint32_t epochTicks = 300;
    AgentTaskTokens epochBudget{1200, 1200, 1200};
    uint32_t starvationThresholdTicks = 120;
    std::array<AgentTaskClass, kAgentTaskClassCount> fixedPriority{
        AgentTaskClass::SUBMIT,
        AgentTaskClass::REVIEW,
        AgentTaskClass::VERIFY,
        AgentTaskClass::DRAFT,
        AgentTaskClass::RESEARCH
    };
    std::array<AgentTaskClassPolicy, kAgentTaskClassCount> classes{};
};

struct AgentTaskRequest {
    AgentTaskClass taskClass = AgentTaskClass::RESEARCH;
    bool ready = false;
};

struct AgentTaskSchedulerSnapshot {
    uint64_t tick = 0;
    uint64_t epochStartTick = 0;
    uint64_t epochIndex = 0;
    AgentTaskTokens remaining{};
    uint32_t roundRobinCursor = 0;
    std::array<uint64_t, kAgentTaskClassCount> lastRunTick{};
};

struct AgentTaskDecision {
    bool scheduled = false;
    AgentTaskClass taskClass = AgentTaskClass::RESEARCH;
    bool starvationFallback = false;
    std::string reason;
    AgentTaskSchedulerSnapshot snapshot{};
};

class AgentTaskScheduler {
public:
    explicit AgentTaskScheduler(const AgentTaskSchedulerPolicy& policy = AgentTaskSchedulerPolicy{});

    void setPolicy(const AgentTaskSchedulerPolicy& policy);
    AgentTaskSchedulerPolicy policy() const;

    AgentTaskDecision tick(const std::array<AgentTaskRequest, kAgentTaskClassCount>& requests);
    AgentTaskSchedulerSnapshot snapshot() const;

    bool loadCheckpoint(const std::string& path, std::string* reason = nullptr);
    bool saveCheckpoint(const std::string& path, std::string* reason = nullptr) const;

private:
    struct InternalState {
        uint64_t tick = 0;
        uint64_t epochStartTick = 0;
        AgentTaskTokens remaining{};
        uint32_t roundRobinCursor = 0;
        std::array<uint64_t, kAgentTaskClassCount> lastRunTick{};
    };

    static AgentTaskSchedulerPolicy sanitizePolicy(const AgentTaskSchedulerPolicy& policy);
    static AgentTaskTokens defaultCostFor(AgentTaskClass taskClass);
    static bool parseU64(const std::string& token, uint64_t* outValue);
    static bool parseU32(const std::string& token, uint32_t* outValue);

    static bool hasTokens(const AgentTaskTokens& have, const AgentTaskTokens& need);
    static void consumeTokens(AgentTaskTokens* have, const AgentTaskTokens& need);

    void resetEpochLocked(uint64_t tickValue);
    uint64_t epochIndexLocked() const;

    AgentTaskSchedulerPolicy policy_;
    InternalState state_{};
    mutable std::mutex mtx_;
};

}
