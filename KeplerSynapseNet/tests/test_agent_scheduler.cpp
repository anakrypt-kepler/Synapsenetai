#include "core/agent_scheduler.h"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace synapse::core;

static std::array<AgentTaskRequest, kAgentTaskClassCount> allReadyRequests() {
    return {
        AgentTaskRequest{AgentTaskClass::RESEARCH, true},
        AgentTaskRequest{AgentTaskClass::VERIFY, true},
        AgentTaskRequest{AgentTaskClass::REVIEW, true},
        AgentTaskRequest{AgentTaskClass::DRAFT, true},
        AgentTaskRequest{AgentTaskClass::SUBMIT, true}
    };
}

static AgentTaskSchedulerPolicy basePolicy() {
    AgentTaskSchedulerPolicy p;
    p.tickSeconds = 1;
    p.epochTicks = 100;
    p.starvationThresholdTicks = 20;
    p.epochBudget = {1000, 1000, 1000};
    p.fixedPriority = {
        AgentTaskClass::SUBMIT,
        AgentTaskClass::DRAFT,
        AgentTaskClass::REVIEW,
        AgentTaskClass::VERIFY,
        AgentTaskClass::RESEARCH
    };
    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        p.classes[i].cost = {1, 1, 1};
        p.classes[i].minIntervalTicks = 1;
    }
    return p;
}

static void testFixedPriorityClasses() {
    auto p = basePolicy();
    p.classes[agentTaskClassIndex(AgentTaskClass::SUBMIT)].minIntervalTicks = 10;
    p.classes[agentTaskClassIndex(AgentTaskClass::DRAFT)].minIntervalTicks = 10;
    p.classes[agentTaskClassIndex(AgentTaskClass::REVIEW)].minIntervalTicks = 10;
    p.classes[agentTaskClassIndex(AgentTaskClass::VERIFY)].minIntervalTicks = 10;
    p.classes[agentTaskClassIndex(AgentTaskClass::RESEARCH)].minIntervalTicks = 10;

    AgentTaskScheduler scheduler(p);
    const auto req = allReadyRequests();

    auto d1 = scheduler.tick(req);
    auto d2 = scheduler.tick(req);
    auto d3 = scheduler.tick(req);
    auto d4 = scheduler.tick(req);
    auto d5 = scheduler.tick(req);

    assert(d1.scheduled && d1.taskClass == AgentTaskClass::SUBMIT);
    assert(d2.scheduled && d2.taskClass == AgentTaskClass::DRAFT);
    assert(d3.scheduled && d3.taskClass == AgentTaskClass::REVIEW);
    assert(d4.scheduled && d4.taskClass == AgentTaskClass::VERIFY);
    assert(d5.scheduled && d5.taskClass == AgentTaskClass::RESEARCH);
}

static void testBudgetPerEpochEnforced() {
    auto p = basePolicy();
    p.epochTicks = 2;
    p.epochBudget = {50, 50, 50};
    for (size_t i = 0; i < kAgentTaskClassCount; ++i) {
        p.classes[i].cost = {50, 50, 50};
    }

    AgentTaskScheduler scheduler(p);
    std::array<AgentTaskRequest, kAgentTaskClassCount> req = {
        AgentTaskRequest{AgentTaskClass::RESEARCH, false},
        AgentTaskRequest{AgentTaskClass::VERIFY, false},
        AgentTaskRequest{AgentTaskClass::REVIEW, false},
        AgentTaskRequest{AgentTaskClass::DRAFT, false},
        AgentTaskRequest{AgentTaskClass::SUBMIT, true}
    };

    auto d1 = scheduler.tick(req);
    auto d2 = scheduler.tick(req);
    auto d3 = scheduler.tick(req);

    assert(d1.scheduled && d1.taskClass == AgentTaskClass::SUBMIT);
    assert(!d2.scheduled && d2.reason == "blocked_budget");
    assert(d3.scheduled && d3.taskClass == AgentTaskClass::SUBMIT);
}

static void testStarvationRoundRobinFallback() {
    auto p = basePolicy();
    p.starvationThresholdTicks = 3;

    AgentTaskScheduler scheduler(p);
    const auto req = allReadyRequests();

    auto d1 = scheduler.tick(req);
    auto d2 = scheduler.tick(req);
    auto d3 = scheduler.tick(req);
    auto d4 = scheduler.tick(req);
    auto d5 = scheduler.tick(req);

    assert(d1.scheduled && d1.taskClass == AgentTaskClass::SUBMIT && !d1.starvationFallback);
    assert(d2.scheduled && d2.taskClass == AgentTaskClass::SUBMIT && !d2.starvationFallback);
    assert(d3.scheduled && d3.taskClass == AgentTaskClass::RESEARCH && d3.starvationFallback);
    assert(d4.scheduled && d4.taskClass == AgentTaskClass::VERIFY && d4.starvationFallback);
    assert(d5.scheduled && d5.taskClass == AgentTaskClass::REVIEW && d5.starvationFallback);
}

static void testCheckpointPersistence() {
    auto p = basePolicy();
    p.starvationThresholdTicks = 2;

    AgentTaskScheduler schedulerA(p);
    const auto req = allReadyRequests();
    for (int i = 0; i < 4; ++i) {
        (void)schedulerA.tick(req);
    }

    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path tmpRoot = fs::temp_directory_path() / ("synapsenet_agent_scheduler_" + uniq);
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);
    fs::create_directories(tmpRoot, ec);
    fs::path checkpoint = tmpRoot / "scheduler.state";

    std::string reason;
    assert(schedulerA.saveCheckpoint(checkpoint.string(), &reason));
    assert(reason == "ok");

    AgentTaskScheduler schedulerB(p);
    assert(schedulerB.loadCheckpoint(checkpoint.string(), &reason));
    assert(reason == "ok");

    auto aNext = schedulerA.tick(req);
    auto bNext = schedulerB.tick(req);

    assert(aNext.scheduled == bNext.scheduled);
    assert(aNext.taskClass == bNext.taskClass);
    assert(aNext.starvationFallback == bNext.starvationFallback);
    assert(aNext.snapshot.tick == bNext.snapshot.tick);
    assert(aNext.snapshot.remaining.cpu == bNext.snapshot.remaining.cpu);
    assert(aNext.snapshot.remaining.ram == bNext.snapshot.remaining.ram);
    assert(aNext.snapshot.remaining.network == bNext.snapshot.remaining.network);

    fs::remove_all(tmpRoot, ec);
}

int main() {
    std::cout << "Running agent_scheduler tests...\n";
    testFixedPriorityClasses();
    testBudgetPerEpochEnforced();
    testStarvationRoundRobinFallback();
    testCheckpointPersistence();
    std::cout << "All agent_scheduler tests passed!\n";
    return 0;
}
