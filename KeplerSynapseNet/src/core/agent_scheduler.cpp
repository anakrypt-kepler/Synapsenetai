#include "core/agent_scheduler.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace synapse::core {

namespace {

namespace fs = std::filesystem;

uint32_t clampU32(uint32_t value, uint32_t minValue, uint32_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

bool uniqueAndCompletePriority(const std::array<AgentTaskClass, kAgentTaskClassCount>& prio) {
    std::unordered_set<size_t> seen;
    for (const auto taskClass : prio) {
        seen.insert(agentTaskClassIndex(taskClass));
    }
    return seen.size() == kAgentTaskClassCount;
}

} // namespace

size_t agentTaskClassIndex(AgentTaskClass taskClass) {
    return static_cast<size_t>(taskClass);
}

std::string agentTaskClassToString(AgentTaskClass taskClass) {
    switch (taskClass) {
        case AgentTaskClass::RESEARCH: return "research";
        case AgentTaskClass::VERIFY: return "verify";
        case AgentTaskClass::REVIEW: return "review";
        case AgentTaskClass::DRAFT: return "draft";
        case AgentTaskClass::SUBMIT: return "submit";
    }
    return "research";
}

AgentTaskTokens AgentTaskScheduler::defaultCostFor(AgentTaskClass taskClass) {
    switch (taskClass) {
        case AgentTaskClass::RESEARCH: return AgentTaskTokens{30, 16, 20};
        case AgentTaskClass::VERIFY: return AgentTaskTokens{20, 12, 8};
        case AgentTaskClass::REVIEW: return AgentTaskTokens{18, 10, 8};
        case AgentTaskClass::DRAFT: return AgentTaskTokens{40, 24, 16};
        case AgentTaskClass::SUBMIT: return AgentTaskTokens{45, 28, 18};
    }
    return AgentTaskTokens{30, 16, 20};
}

AgentTaskSchedulerPolicy AgentTaskScheduler::sanitizePolicy(const AgentTaskSchedulerPolicy& policy) {
    AgentTaskSchedulerPolicy out = policy;
    out.tickSeconds = clampU32(out.tickSeconds, 1, 3600);
    out.epochTicks = clampU32(out.epochTicks, 1, 86400);
    out.starvationThresholdTicks = clampU32(out.starvationThresholdTicks, 1, 1000000);

    if (!uniqueAndCompletePriority(out.fixedPriority)) {
        out.fixedPriority = {
            AgentTaskClass::SUBMIT,
            AgentTaskClass::REVIEW,
            AgentTaskClass::VERIFY,
            AgentTaskClass::DRAFT,
            AgentTaskClass::RESEARCH
        };
    }

    out.epochBudget.cpu = clampU32(out.epochBudget.cpu, 1, 1000000000);
    out.epochBudget.ram = clampU32(out.epochBudget.ram, 1, 1000000000);
    out.epochBudget.network = clampU32(out.epochBudget.network, 1, 1000000000);

    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        AgentTaskClass taskClass = static_cast<AgentTaskClass>(i);
        auto& cls = out.classes[i];
        cls.minIntervalTicks = clampU32(cls.minIntervalTicks, 1, 1000000);
        if (cls.cost.cpu == 0 && cls.cost.ram == 0 && cls.cost.network == 0) {
            cls.cost = defaultCostFor(taskClass);
        }
        cls.cost.cpu = clampU32(cls.cost.cpu, 1, out.epochBudget.cpu);
        cls.cost.ram = clampU32(cls.cost.ram, 1, out.epochBudget.ram);
        cls.cost.network = clampU32(cls.cost.network, 1, out.epochBudget.network);
    }

    return out;
}

bool AgentTaskScheduler::hasTokens(const AgentTaskTokens& have, const AgentTaskTokens& need) {
    return have.cpu >= need.cpu && have.ram >= need.ram && have.network >= need.network;
}

void AgentTaskScheduler::consumeTokens(AgentTaskTokens* have, const AgentTaskTokens& need) {
    have->cpu -= need.cpu;
    have->ram -= need.ram;
    have->network -= need.network;
}

bool AgentTaskScheduler::parseU64(const std::string& token, uint64_t* outValue) {
    if (token.empty() || outValue == nullptr) return false;
    uint64_t value = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
        uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *outValue = value;
    return true;
}

bool AgentTaskScheduler::parseU32(const std::string& token, uint32_t* outValue) {
    uint64_t tmp = 0;
    if (!parseU64(token, &tmp)) return false;
    if (tmp > std::numeric_limits<uint32_t>::max()) return false;
    *outValue = static_cast<uint32_t>(tmp);
    return true;
}

AgentTaskScheduler::AgentTaskScheduler(const AgentTaskSchedulerPolicy& policy)
    : policy_(sanitizePolicy(policy)) {
    resetEpochLocked(0);
}

void AgentTaskScheduler::setPolicy(const AgentTaskSchedulerPolicy& policy) {
    std::lock_guard<std::mutex> lock(mtx_);
    policy_ = sanitizePolicy(policy);
    state_.remaining.cpu = std::min(state_.remaining.cpu, policy_.epochBudget.cpu);
    state_.remaining.ram = std::min(state_.remaining.ram, policy_.epochBudget.ram);
    state_.remaining.network = std::min(state_.remaining.network, policy_.epochBudget.network);
    state_.roundRobinCursor %= static_cast<uint32_t>(kAgentTaskClassCount);
}

AgentTaskSchedulerPolicy AgentTaskScheduler::policy() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return policy_;
}

void AgentTaskScheduler::resetEpochLocked(uint64_t tickValue) {
    state_.epochStartTick = tickValue;
    state_.remaining = policy_.epochBudget;
}

uint64_t AgentTaskScheduler::epochIndexLocked() const {
    if (policy_.epochTicks == 0) return 0;
    if (state_.tick <= state_.epochStartTick) return 0;
    return (state_.epochStartTick - 1) / static_cast<uint64_t>(policy_.epochTicks);
}

AgentTaskDecision AgentTaskScheduler::tick(const std::array<AgentTaskRequest, kAgentTaskClassCount>& requests) {
    std::lock_guard<std::mutex> lock(mtx_);

    state_.tick += 1;
    const uint64_t currentTick = state_.tick;

    if (state_.epochStartTick == 0) {
        resetEpochLocked(currentTick);
    } else if (currentTick - state_.epochStartTick >= policy_.epochTicks) {
        resetEpochLocked(currentTick);
    }

    std::array<bool, kAgentTaskClassCount> ready{};
    for (const auto& req : requests) {
        const size_t idx = agentTaskClassIndex(req.taskClass);
        ready[idx] = ready[idx] || req.ready;
    }

    std::array<bool, kAgentTaskClassCount> intervalOk{};
    std::array<bool, kAgentTaskClassCount> budgetOk{};
    std::array<bool, kAgentTaskClassCount> eligible{};

    bool anyReady = false;
    bool anyIntervalOk = false;
    bool anyBudgetOk = false;

    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        if (!ready[i]) continue;
        anyReady = true;

        const auto& cls = policy_.classes[i];
        const uint64_t lastRun = state_.lastRunTick[i];
        if (lastRun == 0 || currentTick >= lastRun + cls.minIntervalTicks) {
            intervalOk[i] = true;
            anyIntervalOk = true;
        }

        if (hasTokens(state_.remaining, cls.cost)) {
            budgetOk[i] = true;
            anyBudgetOk = true;
        }

        eligible[i] = intervalOk[i] && budgetOk[i];
    }

    AgentTaskDecision decision;
    decision.reason = "no_eligible_task";

    std::array<bool, kAgentTaskClassCount> starved{};
    bool anyStarved = false;
    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        if (!eligible[i]) continue;
        uint64_t delta = state_.lastRunTick[i] == 0 ? currentTick : (currentTick - state_.lastRunTick[i]);
        if (delta >= policy_.starvationThresholdTicks) {
            starved[i] = true;
            anyStarved = true;
        }
    }

    size_t selectedIdx = kAgentTaskClassCount;
    if (anyStarved) {
        for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
            const size_t idx = (state_.roundRobinCursor + i) % kAgentTaskClassCount;
            if (starved[idx]) {
                selectedIdx = idx;
                decision.starvationFallback = true;
                break;
            }
        }
    }

    if (selectedIdx == kAgentTaskClassCount) {
        for (const auto taskClass : policy_.fixedPriority) {
            const size_t idx = agentTaskClassIndex(taskClass);
            if (eligible[idx]) {
                selectedIdx = idx;
                break;
            }
        }
    }

    if (selectedIdx != kAgentTaskClassCount) {
        decision.scheduled = true;
        decision.taskClass = static_cast<AgentTaskClass>(selectedIdx);
        consumeTokens(&state_.remaining, policy_.classes[selectedIdx].cost);
        state_.lastRunTick[selectedIdx] = currentTick;
        state_.roundRobinCursor = static_cast<uint32_t>((selectedIdx + 1) % kAgentTaskClassCount);
        decision.reason = decision.starvationFallback ? "scheduled_starvation_fallback" : "scheduled_fixed_priority";
    } else {
        if (!anyReady) {
            decision.reason = "no_ready_tasks";
        } else if (!anyIntervalOk) {
            decision.reason = "blocked_min_interval";
        } else if (!anyBudgetOk) {
            decision.reason = "blocked_budget";
        }
    }

    decision.snapshot.tick = state_.tick;
    decision.snapshot.epochStartTick = state_.epochStartTick;
    decision.snapshot.epochIndex = epochIndexLocked();
    decision.snapshot.remaining = state_.remaining;
    decision.snapshot.roundRobinCursor = state_.roundRobinCursor;
    decision.snapshot.lastRunTick = state_.lastRunTick;
    return decision;
}

AgentTaskSchedulerSnapshot AgentTaskScheduler::snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    AgentTaskSchedulerSnapshot out;
    out.tick = state_.tick;
    out.epochStartTick = state_.epochStartTick;
    out.epochIndex = epochIndexLocked();
    out.remaining = state_.remaining;
    out.roundRobinCursor = state_.roundRobinCursor;
    out.lastRunTick = state_.lastRunTick;
    return out;
}

bool AgentTaskScheduler::loadCheckpoint(const std::string& path, std::string* reason) {
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
    std::vector<std::string> parts;
    std::string token;
    while (std::getline(ss, token, ',')) {
        parts.push_back(token);
    }

    const size_t expectedParts = 1 + 6 + kAgentTaskClassCount;
    if (parts.size() != expectedParts || parts[0] != "v1") {
        if (reason) *reason = "invalid_format";
        return false;
    }

    uint64_t tick = 0;
    uint64_t epochStartTick = 0;
    uint32_t cpu = 0;
    uint32_t ram = 0;
    uint32_t net = 0;
    uint32_t rr = 0;
    if (!parseU64(parts[1], &tick) ||
        !parseU64(parts[2], &epochStartTick) ||
        !parseU32(parts[3], &cpu) ||
        !parseU32(parts[4], &ram) ||
        !parseU32(parts[5], &net) ||
        !parseU32(parts[6], &rr)) {
        if (reason) *reason = "invalid_numbers";
        return false;
    }

    std::array<uint64_t, kAgentTaskClassCount> lastRun{};
    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        if (!parseU64(parts[7 + i], &lastRun[i])) {
            if (reason) *reason = "invalid_last_run";
            return false;
        }
    }

    state_.tick = tick;
    state_.epochStartTick = epochStartTick;
    state_.remaining.cpu = std::min(cpu, policy_.epochBudget.cpu);
    state_.remaining.ram = std::min(ram, policy_.epochBudget.ram);
    state_.remaining.network = std::min(net, policy_.epochBudget.network);
    state_.roundRobinCursor = rr % static_cast<uint32_t>(kAgentTaskClassCount);
    state_.lastRunTick = lastRun;

    if (state_.tick == 0 || state_.epochStartTick == 0 || state_.epochStartTick > state_.tick) {
        resetEpochLocked(state_.tick == 0 ? 1 : state_.tick);
    }

    if (reason) *reason = "ok";
    return true;
}

bool AgentTaskScheduler::saveCheckpoint(const std::string& path, std::string* reason) const {
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
        << state_.tick << ","
        << state_.epochStartTick << ","
        << state_.remaining.cpu << ","
        << state_.remaining.ram << ","
        << state_.remaining.network << ","
        << state_.roundRobinCursor;
    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        out << "," << state_.lastRunTick[i];
    }

    if (!out.good()) {
        if (reason) *reason = "write_failed";
        return false;
    }

    if (reason) *reason = "ok";
    return true;
}

} // namespace synapse::core
