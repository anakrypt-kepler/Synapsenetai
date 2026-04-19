#include "core/agent_runtime.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace synapse::core {

namespace {

namespace fs = std::filesystem;

static int32_t clampI32(int32_t value, int32_t minValue, int32_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static std::string toLowerCopy(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

static fs::path normalizePath(const fs::path& path, const fs::path& base) {
    std::error_code ec;
    fs::path p = path;
    if (p.empty()) return {};
    if (!p.is_absolute()) p = base / p;
    p = p.lexically_normal();
    fs::path weak = fs::weakly_canonical(p, ec);
    if (!ec && !weak.empty()) {
        return weak.lexically_normal();
    }
    return p.lexically_normal();
}

static bool isSubpath(const fs::path& childIn, const fs::path& parentIn) {
    fs::path child = childIn.lexically_normal();
    fs::path parent = parentIn.lexically_normal();

    auto pit = parent.begin();
    auto cit = child.begin();
    for (; pit != parent.end() && cit != child.end(); ++pit, ++cit) {
        if (*pit != *cit) return false;
    }
    return pit == parent.end();
}

static uint32_t clampPositiveInterval(uint32_t value, uint32_t fallbackValue) {
    return value == 0 ? fallbackValue : value;
}

static uint64_t clampU64(uint64_t value, uint64_t minValue, uint64_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static AgentAdaptiveSchedulingPolicy sanitizeSchedulingPolicy(const AgentAdaptiveSchedulingPolicy& policy) {
    AgentAdaptiveSchedulingPolicy out = policy;
    out.draftIntervalNormalSeconds = clampPositiveInterval(out.draftIntervalNormalSeconds, 300);
    out.draftIntervalThrottledSeconds = clampPositiveInterval(out.draftIntervalThrottledSeconds, out.draftIntervalNormalSeconds);
    out.draftIntervalQuarantinedSeconds = clampPositiveInterval(out.draftIntervalQuarantinedSeconds, out.draftIntervalThrottledSeconds);

    out.pipelineIntervalNormalSeconds = clampPositiveInterval(out.pipelineIntervalNormalSeconds, 15);
    out.pipelineIntervalThrottledSeconds = clampPositiveInterval(out.pipelineIntervalThrottledSeconds, out.pipelineIntervalNormalSeconds);
    out.pipelineIntervalQuarantinedSeconds = clampPositiveInterval(out.pipelineIntervalQuarantinedSeconds, out.pipelineIntervalThrottledSeconds);

    out.heartbeatIntervalNormalSeconds = clampPositiveInterval(out.heartbeatIntervalNormalSeconds, 60);
    out.heartbeatIntervalThrottledSeconds = clampPositiveInterval(out.heartbeatIntervalThrottledSeconds, out.heartbeatIntervalNormalSeconds);
    out.heartbeatIntervalQuarantinedSeconds = clampPositiveInterval(out.heartbeatIntervalQuarantinedSeconds, out.heartbeatIntervalThrottledSeconds);
    return out;
}

static AgentRuntimeSupervisorConfig sanitizeSupervisorConfig(const AgentRuntimeSupervisorConfig& config) {
    AgentRuntimeSupervisorConfig out = config;
    out.backoffBaseSeconds = static_cast<uint32_t>(clampU64(out.backoffBaseSeconds, 1, 3600));
    out.backoffMaxSeconds = static_cast<uint32_t>(clampU64(out.backoffMaxSeconds, out.backoffBaseSeconds, 86400));
    out.backoffExponentCap = static_cast<uint32_t>(clampU64(out.backoffExponentCap, 0, 31));
    return out;
}

static bool parseU64(const std::string& token, uint64_t* out) {
    if (token.empty() || out == nullptr) return false;
    uint64_t v = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
        uint64_t digit = static_cast<uint64_t>(c - '0');
        if (v > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        v = v * 10 + digit;
    }
    *out = v;
    return true;
}

}

std::string capabilityToString(AgentCapability capability) {
    switch (capability) {
        case AgentCapability::READ_LEDGER: return "read_ledger";
        case AgentCapability::READ_NETWORK: return "read_network";
        case AgentCapability::FETCH_EXTERNAL: return "fetch_external";
        case AgentCapability::PROPOSE_KNOWLEDGE: return "propose_knowledge";
        case AgentCapability::PUBLISH_UPDATE: return "publish_update";
        case AgentCapability::INSTALL_UPDATE: return "install_update";
    }
    return "unknown";
}

std::string pathAccessModeToString(PathAccessMode mode) {
    switch (mode) {
        case PathAccessMode::READ: return "read";
        case PathAccessMode::WRITE: return "write";
    }
    return "read";
}

std::string runtimeActionResultToString(RuntimeActionResult result) {
    switch (result) {
        case RuntimeActionResult::ALLOWED: return "allowed";
        case RuntimeActionResult::DENIED_CAPABILITY: return "denied_capability";
        case RuntimeActionResult::DENIED_SIDE_EFFECTS: return "denied_side_effects";
        case RuntimeActionResult::DENIED_EXPLICIT_FLAG: return "denied_explicit_flag";
        case RuntimeActionResult::DENIED_PATH_OUTSIDE_ROOT: return "denied_path_outside_root";
        case RuntimeActionResult::DENIED_SECRET_PATH: return "denied_secret_path";
        case RuntimeActionResult::DENIED_TOOL_SCHEMA: return "denied_tool_schema";
        case RuntimeActionResult::DENIED_TOOL_PAYLOAD: return "denied_tool_payload";
        case RuntimeActionResult::DENIED_TOOL_ARGUMENTS: return "denied_tool_arguments";
        case RuntimeActionResult::DENIED_INVALID_PATH: return "denied_invalid_path";
    }
    return "denied_unknown";
}

AgentRuntimeSandbox::AgentRuntimeSandbox() = default;

AgentRuntimeSandbox::AgentRuntimeSandbox(const SandboxPolicy& policy) : policy_(policy) {}

void AgentRuntimeSandbox::setPolicy(const SandboxPolicy& policy) {
    policy_ = policy;
}

SandboxPolicy AgentRuntimeSandbox::getPolicy() const {
    return policy_;
}

bool AgentRuntimeSandbox::isAllowed(AgentCapability capability) const {
    return std::find(policy_.allowlist.begin(), policy_.allowlist.end(), capability) != policy_.allowlist.end();
}

RuntimeActionResult AgentRuntimeSandbox::authorize(
    AgentCapability capability,
    bool hasSideEffects,
    bool explicitSideEffectFlag
) const {
    if (!isAllowed(capability)) return RuntimeActionResult::DENIED_CAPABILITY;
    if (!hasSideEffects) return RuntimeActionResult::ALLOWED;
    if (!policy_.allowSideEffects) return RuntimeActionResult::DENIED_SIDE_EFFECTS;
    if (!explicitSideEffectFlag) return RuntimeActionResult::DENIED_EXPLICIT_FLAG;
    return RuntimeActionResult::ALLOWED;
}

RuntimeActionResult AgentRuntimeSandbox::authorizePath(AgentCapability capability,
                                                       PathAccessMode mode,
                                                       const std::string& path,
                                                       bool explicitSideEffectFlag) const {
    const bool hasSideEffects = mode == PathAccessMode::WRITE;
    RuntimeActionResult gate = authorize(capability, hasSideEffects, explicitSideEffectFlag);
    if (gate != RuntimeActionResult::ALLOWED) return gate;

    const fs::path rawRoot = policy_.workspaceRoot.empty() ? fs::path(".") : fs::path(policy_.workspaceRoot);
    const fs::path base = rawRoot.is_absolute() ? rawRoot : fs::current_path();
    const fs::path workspaceRoot = normalizePath(rawRoot, base);
    const fs::path target = normalizePath(fs::path(path), workspaceRoot);
    if (target.empty()) return RuntimeActionResult::DENIED_INVALID_PATH;

    for (const auto& deniedPrefixRaw : policy_.deniedPathPrefixes) {
        fs::path deniedPrefix = normalizePath(fs::path(deniedPrefixRaw), workspaceRoot);
        if (!deniedPrefix.empty() && isSubpath(target, deniedPrefix)) {
            return RuntimeActionResult::DENIED_SECRET_PATH;
        }
    }

    const std::string fileName = toLowerCopy(target.filename().string());
    for (const auto& deniedName : policy_.deniedFileNames) {
        if (fileName == toLowerCopy(deniedName)) {
            return RuntimeActionResult::DENIED_SECRET_PATH;
        }
    }

    const std::string ext = toLowerCopy(target.extension().string());
    for (const auto& deniedExt : policy_.deniedExtensions) {
        if (ext == toLowerCopy(deniedExt)) {
            return RuntimeActionResult::DENIED_SECRET_PATH;
        }
    }

    std::vector<std::string> roots = (mode == PathAccessMode::READ) ? policy_.readRoots : policy_.writeRoots;
    if (roots.empty()) roots.push_back(policy_.workspaceRoot.empty() ? "." : policy_.workspaceRoot);

    for (const auto& rootRaw : roots) {
        fs::path root = normalizePath(fs::path(rootRaw), workspaceRoot);
        if (!root.empty() && isSubpath(target, root)) {
            return RuntimeActionResult::ALLOWED;
        }
    }
    return RuntimeActionResult::DENIED_PATH_OUTSIDE_ROOT;
}

RuntimeActionResult AgentRuntimeSandbox::authorizeTool(const ToolInvocation& invocation,
                                                       const std::vector<ToolSchemaRule>& schemas) const {
    if (invocation.toolName.empty()) {
        return RuntimeActionResult::DENIED_TOOL_SCHEMA;
    }

    RuntimeActionResult baseGate = authorize(
        invocation.capability,
        invocation.hasSideEffects,
        invocation.explicitSideEffectFlag
    );
    if (baseGate != RuntimeActionResult::ALLOWED) return baseGate;

    const ToolSchemaRule* schema = nullptr;
    for (const auto& s : schemas) {
        if (s.toolName == invocation.toolName) {
            schema = &s;
            break;
        }
    }
    if (schema == nullptr) return RuntimeActionResult::DENIED_TOOL_SCHEMA;
    if (schema->capability != invocation.capability) return RuntimeActionResult::DENIED_TOOL_SCHEMA;
    if (invocation.payloadBytes > policy_.maxToolPayloadBytes || invocation.payloadBytes > schema->maxPayloadBytes) {
        return RuntimeActionResult::DENIED_TOOL_PAYLOAD;
    }
    if (invocation.hasSideEffects && !schema->allowSideEffects) {
        return RuntimeActionResult::DENIED_SIDE_EFFECTS;
    }
    if (invocation.hasSideEffects && schema->requireExplicitSideEffectFlag && !invocation.explicitSideEffectFlag) {
        return RuntimeActionResult::DENIED_EXPLICIT_FLAG;
    }

    std::unordered_set<std::string> required(schema->requiredKeys.begin(), schema->requiredKeys.end());
    std::unordered_set<std::string> optional(schema->optionalKeys.begin(), schema->optionalKeys.end());
    std::unordered_set<std::string> given(invocation.keys.begin(), invocation.keys.end());

    for (const auto& key : required) {
        if (given.find(key) == given.end()) {
            return RuntimeActionResult::DENIED_TOOL_ARGUMENTS;
        }
    }
    for (const auto& key : given) {
        if (required.find(key) == required.end() && optional.find(key) == optional.end()) {
            return RuntimeActionResult::DENIED_TOOL_ARGUMENTS;
        }
    }

    return RuntimeActionResult::ALLOWED;
}

std::string schedulingStateToString(AgentSchedulingState state) {
    switch (state) {
        case AgentSchedulingState::NORMAL: return "NORMAL";
        case AgentSchedulingState::THROTTLED: return "THROTTLED";
        case AgentSchedulingState::QUARANTINED: return "QUARANTINED";
    }
    return "NORMAL";
}

AgentAdaptiveScheduler::AgentAdaptiveScheduler(const AgentAdaptiveSchedulingPolicy& policy)
    : policy_(sanitizeSchedulingPolicy(policy)) {}

void AgentAdaptiveScheduler::setPolicy(const AgentAdaptiveSchedulingPolicy& policy) {
    policy_ = sanitizeSchedulingPolicy(policy);
}

AgentAdaptiveSchedulingPolicy AgentAdaptiveScheduler::policy() const {
    return policy_;
}

AgentAdaptiveSchedule AgentAdaptiveScheduler::schedule(bool throttled, bool quarantined) const {
    AgentAdaptiveSchedule out;
    if (quarantined) {
        out.state = AgentSchedulingState::QUARANTINED;
        out.draftIntervalSeconds = policy_.draftIntervalQuarantinedSeconds;
        out.pipelineIntervalSeconds = policy_.pipelineIntervalQuarantinedSeconds;
        out.heartbeatIntervalSeconds = policy_.heartbeatIntervalQuarantinedSeconds;
        return out;
    }
    if (throttled) {
        out.state = AgentSchedulingState::THROTTLED;
        out.draftIntervalSeconds = policy_.draftIntervalThrottledSeconds;
        out.pipelineIntervalSeconds = policy_.pipelineIntervalThrottledSeconds;
        out.heartbeatIntervalSeconds = policy_.heartbeatIntervalThrottledSeconds;
        return out;
    }

    out.state = AgentSchedulingState::NORMAL;
    out.draftIntervalSeconds = policy_.draftIntervalNormalSeconds;
    out.pipelineIntervalSeconds = policy_.pipelineIntervalNormalSeconds;
    out.heartbeatIntervalSeconds = policy_.heartbeatIntervalNormalSeconds;
    return out;
}

std::string failoverStateToString(AgentRuntimeFailoverState state) {
    switch (state) {
        case AgentRuntimeFailoverState::ACTIVE: return "ACTIVE";
        case AgentRuntimeFailoverState::THROTTLED: return "THROTTLED";
        case AgentRuntimeFailoverState::QUARANTINED: return "QUARANTINED";
        case AgentRuntimeFailoverState::RECOVERY: return "RECOVERY";
    }
    return "RECOVERY";
}

AgentRuntimeSupervisor::AgentRuntimeSupervisor(const AgentRuntimeSupervisorConfig& config)
    : config_(sanitizeSupervisorConfig(config)) {}

void AgentRuntimeSupervisor::setConfig(const AgentRuntimeSupervisorConfig& config) {
    std::lock_guard<std::mutex> lock(mtx_);
    config_ = sanitizeSupervisorConfig(config);
}

AgentRuntimeSupervisorConfig AgentRuntimeSupervisor::config() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return config_;
}

AgentRuntimeCrashState AgentRuntimeSupervisor::crashState() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return state_;
}

bool AgentRuntimeSupervisor::inRecovery(uint64_t nowTimestamp) const {
    std::lock_guard<std::mutex> lock(mtx_);
    return state_.recoveryUntilTimestamp != 0 && nowTimestamp < state_.recoveryUntilTimestamp;
}

uint64_t AgentRuntimeSupervisor::markFailure(uint64_t atTimestamp) {
    std::lock_guard<std::mutex> lock(mtx_);

    state_.totalCrashes += 1;
    state_.consecutiveCrashes += 1;
    state_.lastCrashTimestamp = atTimestamp;

    uint64_t exp = state_.consecutiveCrashes <= 1 ? 0 : (state_.consecutiveCrashes - 1);
    if (exp > config_.backoffExponentCap) exp = config_.backoffExponentCap;

    uint64_t backoff = config_.backoffBaseSeconds;
    if (exp > 0) {
        const uint64_t maxShift = 62;
        uint64_t shift = exp > maxShift ? maxShift : exp;
        if (backoff > (std::numeric_limits<uint64_t>::max() >> shift)) {
            backoff = std::numeric_limits<uint64_t>::max();
        } else {
            backoff <<= shift;
        }
    }
    if (backoff > config_.backoffMaxSeconds) backoff = config_.backoffMaxSeconds;

    state_.recoveryUntilTimestamp = atTimestamp + backoff;
    return backoff;
}

void AgentRuntimeSupervisor::markSuccess() {
    std::lock_guard<std::mutex> lock(mtx_);
    state_.consecutiveCrashes = 0;
    state_.recoveryUntilTimestamp = 0;
}

AgentRuntimeFailoverState AgentRuntimeSupervisor::failoverState(bool throttled,
                                                                bool quarantined,
                                                                uint64_t nowTimestamp) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (state_.recoveryUntilTimestamp != 0 && nowTimestamp < state_.recoveryUntilTimestamp) {
        return AgentRuntimeFailoverState::RECOVERY;
    }
    if (quarantined) return AgentRuntimeFailoverState::QUARANTINED;
    if (throttled) return AgentRuntimeFailoverState::THROTTLED;
    return AgentRuntimeFailoverState::ACTIVE;
}

bool AgentRuntimeSupervisor::loadCrashState(const std::string& path, std::string* reason) {
    std::lock_guard<std::mutex> lock(mtx_);

    std::ifstream in(path);
    if (!in.good()) {
        if (reason) *reason = "not_found";
        return true;
    }

    std::string line;
    if (!std::getline(in, line)) {
        if (reason) *reason = "empty";
        return false;
    }

    std::stringstream ss(line);
    std::string token;
    std::vector<std::string> parts;
    while (std::getline(ss, token, ',')) {
        parts.push_back(token);
    }
    if (parts.size() != 5 || parts[0] != "v1") {
        if (reason) *reason = "invalid_format";
        return false;
    }

    AgentRuntimeCrashState loaded;
    if (!parseU64(parts[1], &loaded.totalCrashes) ||
        !parseU64(parts[2], &loaded.consecutiveCrashes) ||
        !parseU64(parts[3], &loaded.lastCrashTimestamp) ||
        !parseU64(parts[4], &loaded.recoveryUntilTimestamp)) {
        if (reason) *reason = "invalid_numbers";
        return false;
    }

    state_ = loaded;
    if (reason) *reason = "ok";
    return true;
}

bool AgentRuntimeSupervisor::saveCrashState(const std::string& path, std::string* reason) const {
    std::lock_guard<std::mutex> lock(mtx_);

    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
        if (ec) {
            if (reason) *reason = "mkdir_failed";
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) {
        if (reason) *reason = "open_failed";
        return false;
    }

    out << "v1,"
        << state_.totalCrashes << ","
        << state_.consecutiveCrashes << ","
        << state_.lastCrashTimestamp << ","
        << state_.recoveryUntilTimestamp;
    if (!out.good()) {
        if (reason) *reason = "write_failed";
        return false;
    }

    if (reason) *reason = "ok";
    return true;
}

AgentScoreCard::AgentScoreCard(const AgentScoreConfig& cfg) : cfg_(cfg) {
    if (cfg_.minScore > cfg_.maxScore) std::swap(cfg_.minScore, cfg_.maxScore);
    cfg_.baseScore = clampI32(cfg_.baseScore, cfg_.minScore, cfg_.maxScore);
    cfg_.quarantineThreshold = clampI32(cfg_.quarantineThreshold, cfg_.minScore, cfg_.maxScore);
    if (cfg_.decayPerInterval < 0) cfg_.decayPerInterval = 0;
}

AgentScoreConfig AgentScoreCard::config() const {
    return cfg_;
}

int32_t AgentScoreCard::score() const {
    return clampI32(cfg_.baseScore - penaltyPoints_, cfg_.minScore, cfg_.maxScore);
}

int32_t AgentScoreCard::penalty() const {
    return penaltyPoints_;
}

bool AgentScoreCard::quarantined() const {
    return score() < cfg_.quarantineThreshold;
}

void AgentScoreCard::applyDecay(uint64_t nowTimestamp) {
    if (cfg_.decayIntervalSeconds == 0 || cfg_.decayPerInterval == 0) {
        if (lastDecayTs_ == 0) lastDecayTs_ = nowTimestamp;
        return;
    }

    if (lastDecayTs_ == 0) {
        lastDecayTs_ = nowTimestamp;
        return;
    }

    if (nowTimestamp <= lastDecayTs_) return;

    uint64_t elapsed = nowTimestamp - lastDecayTs_;
    uint64_t steps = elapsed / cfg_.decayIntervalSeconds;
    if (steps == 0) return;

    int64_t decayAmount64 = static_cast<int64_t>(steps) * static_cast<int64_t>(cfg_.decayPerInterval);
    int32_t decayAmount = decayAmount64 > static_cast<int64_t>(std::numeric_limits<int32_t>::max())
        ? std::numeric_limits<int32_t>::max()
        : static_cast<int32_t>(decayAmount64);
    penaltyPoints_ = std::max<int32_t>(0, penaltyPoints_ - decayAmount);
    lastDecayTs_ += steps * cfg_.decayIntervalSeconds;
}

void AgentScoreCard::applyPenalty(int32_t amount, uint64_t atTimestamp) {
    applyDecay(atTimestamp);
    if (amount <= 0) return;

    int64_t nextPenalty = static_cast<int64_t>(penaltyPoints_) + static_cast<int64_t>(amount);
    if (nextPenalty > static_cast<int64_t>(cfg_.baseScore - cfg_.minScore)) {
        penaltyPoints_ = cfg_.baseScore - cfg_.minScore;
    } else {
        penaltyPoints_ = static_cast<int32_t>(nextPenalty);
    }
}

void AgentScoreCard::applyReward(int32_t amount, uint64_t atTimestamp) {
    applyDecay(atTimestamp);
    if (amount <= 0) return;
    penaltyPoints_ = std::max<int32_t>(0, penaltyPoints_ - amount);
}

}
