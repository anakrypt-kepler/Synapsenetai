#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace synapse::core {

enum class AgentCapability : uint8_t {
    READ_LEDGER = 0,
    READ_NETWORK = 1,
    FETCH_EXTERNAL = 2,
    PROPOSE_KNOWLEDGE = 3,
    PUBLISH_UPDATE = 4,
    INSTALL_UPDATE = 5
};

struct SandboxPolicy {
    bool allowSideEffects = false;
    std::vector<AgentCapability> allowlist;
    std::string workspaceRoot = ".";
    std::vector<std::string> readRoots;
    std::vector<std::string> writeRoots;
    std::vector<std::string> deniedPathPrefixes;
    std::vector<std::string> deniedFileNames;
    std::vector<std::string> deniedExtensions;
    uint32_t maxToolPayloadBytes = 131072;
};

enum class PathAccessMode : uint8_t {
    READ = 0,
    WRITE = 1
};

struct ToolSchemaRule {
    std::string toolName;
    AgentCapability capability = AgentCapability::READ_NETWORK;
    std::vector<std::string> requiredKeys;
    std::vector<std::string> optionalKeys;
    uint32_t maxPayloadBytes = 16384;
    bool allowSideEffects = false;
    bool requireExplicitSideEffectFlag = true;
};

struct ToolInvocation {
    std::string toolName;
    AgentCapability capability = AgentCapability::READ_NETWORK;
    std::vector<std::string> keys;
    uint32_t payloadBytes = 0;
    bool hasSideEffects = false;
    bool explicitSideEffectFlag = false;
};

enum class RuntimeActionResult : uint8_t {
    ALLOWED = 0,
    DENIED_CAPABILITY = 1,
    DENIED_SIDE_EFFECTS = 2,
    DENIED_EXPLICIT_FLAG = 3,
    DENIED_PATH_OUTSIDE_ROOT = 4,
    DENIED_SECRET_PATH = 5,
    DENIED_TOOL_SCHEMA = 6,
    DENIED_TOOL_PAYLOAD = 7,
    DENIED_TOOL_ARGUMENTS = 8,
    DENIED_INVALID_PATH = 9
};

std::string capabilityToString(AgentCapability capability);
std::string pathAccessModeToString(PathAccessMode mode);
std::string runtimeActionResultToString(RuntimeActionResult result);

class AgentRuntimeSandbox {
public:
    AgentRuntimeSandbox();
    explicit AgentRuntimeSandbox(const SandboxPolicy& policy);

    void setPolicy(const SandboxPolicy& policy);
    SandboxPolicy getPolicy() const;

    bool isAllowed(AgentCapability capability) const;
    RuntimeActionResult authorize(AgentCapability capability, bool hasSideEffects, bool explicitSideEffectFlag) const;
    RuntimeActionResult authorizePath(AgentCapability capability,
                                      PathAccessMode mode,
                                      const std::string& path,
                                      bool explicitSideEffectFlag = false) const;
    RuntimeActionResult authorizeTool(const ToolInvocation& invocation,
                                      const std::vector<ToolSchemaRule>& schemas) const;

private:
    SandboxPolicy policy_;
};

enum class AgentSchedulingState : uint8_t {
    NORMAL = 0,
    THROTTLED = 1,
    QUARANTINED = 2
};

struct AgentAdaptiveSchedulingPolicy {
    uint32_t draftIntervalNormalSeconds = 300;
    uint32_t draftIntervalThrottledSeconds = 600;
    uint32_t draftIntervalQuarantinedSeconds = 900;

    uint32_t pipelineIntervalNormalSeconds = 15;
    uint32_t pipelineIntervalThrottledSeconds = 45;
    uint32_t pipelineIntervalQuarantinedSeconds = 120;

    uint32_t heartbeatIntervalNormalSeconds = 60;
    uint32_t heartbeatIntervalThrottledSeconds = 90;
    uint32_t heartbeatIntervalQuarantinedSeconds = 120;
};

struct AgentAdaptiveSchedule {
    AgentSchedulingState state = AgentSchedulingState::NORMAL;
    uint32_t draftIntervalSeconds = 300;
    uint32_t pipelineIntervalSeconds = 15;
    uint32_t heartbeatIntervalSeconds = 60;
};

std::string schedulingStateToString(AgentSchedulingState state);

class AgentAdaptiveScheduler {
public:
    explicit AgentAdaptiveScheduler(const AgentAdaptiveSchedulingPolicy& policy = AgentAdaptiveSchedulingPolicy{});

    void setPolicy(const AgentAdaptiveSchedulingPolicy& policy);
    AgentAdaptiveSchedulingPolicy policy() const;

    AgentAdaptiveSchedule schedule(bool throttled, bool quarantined) const;

private:
    AgentAdaptiveSchedulingPolicy policy_{};
};

enum class AgentRuntimeFailoverState : uint8_t {
    ACTIVE = 0,
    THROTTLED = 1,
    QUARANTINED = 2,
    RECOVERY = 3
};

std::string failoverStateToString(AgentRuntimeFailoverState state);

struct AgentRuntimeSupervisorConfig {
    uint32_t backoffBaseSeconds = 5;
    uint32_t backoffMaxSeconds = 300;
    uint32_t backoffExponentCap = 6;
};

struct AgentRuntimeCrashState {
    uint64_t totalCrashes = 0;
    uint64_t consecutiveCrashes = 0;
    uint64_t lastCrashTimestamp = 0;
    uint64_t recoveryUntilTimestamp = 0;
};

class AgentRuntimeSupervisor {
public:
    explicit AgentRuntimeSupervisor(const AgentRuntimeSupervisorConfig& config = AgentRuntimeSupervisorConfig{});

    void setConfig(const AgentRuntimeSupervisorConfig& config);
    AgentRuntimeSupervisorConfig config() const;

    AgentRuntimeCrashState crashState() const;
    bool inRecovery(uint64_t nowTimestamp) const;

    uint64_t markFailure(uint64_t atTimestamp);
    void markSuccess();

    AgentRuntimeFailoverState failoverState(bool throttled,
                                            bool quarantined,
                                            uint64_t nowTimestamp) const;

    bool loadCrashState(const std::string& path, std::string* reason = nullptr);
    bool saveCrashState(const std::string& path, std::string* reason = nullptr) const;

private:
    AgentRuntimeSupervisorConfig config_{};
    AgentRuntimeCrashState state_{};
    mutable std::mutex mtx_;
};

struct AgentScoreConfig {
    int32_t baseScore = 1000;
    int32_t minScore = 0;
    int32_t maxScore = 2000;
    int32_t quarantineThreshold = 400;
    uint32_t decayIntervalSeconds = 60;
    int32_t decayPerInterval = 5;
};

class AgentScoreCard {
public:
    explicit AgentScoreCard(const AgentScoreConfig& cfg = AgentScoreConfig{});

    AgentScoreConfig config() const;
    int32_t score() const;
    int32_t penalty() const;
    bool quarantined() const;

    void applyPenalty(int32_t amount, uint64_t atTimestamp);
    void applyReward(int32_t amount, uint64_t atTimestamp);
    void applyDecay(uint64_t nowTimestamp);

private:
    AgentScoreConfig cfg_{};
    int32_t penaltyPoints_ = 0;
    uint64_t lastDecayTs_ = 0;
};

}
