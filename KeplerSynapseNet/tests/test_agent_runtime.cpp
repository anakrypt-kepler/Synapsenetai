#include "core/agent_runtime.h"
#include "core/agent_score.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

static void testSandboxDefaultDeny() {
    synapse::core::AgentRuntimeSandbox sandbox;
    auto result = sandbox.authorize(synapse::core::AgentCapability::READ_LEDGER, false, false);
    assert(result == synapse::core::RuntimeActionResult::DENIED_CAPABILITY);
}

static void testSandboxAllowlistAndSideEffectRules() {
    synapse::core::SandboxPolicy policy;
    policy.allowSideEffects = false;
    policy.allowlist = {
        synapse::core::AgentCapability::READ_LEDGER,
        synapse::core::AgentCapability::PROPOSE_KNOWLEDGE
    };
    synapse::core::AgentRuntimeSandbox sandbox(policy);

    auto readResult = sandbox.authorize(synapse::core::AgentCapability::READ_LEDGER, false, false);
    assert(readResult == synapse::core::RuntimeActionResult::ALLOWED);

    auto sideEffectDenied = sandbox.authorize(synapse::core::AgentCapability::PROPOSE_KNOWLEDGE, true, true);
    assert(sideEffectDenied == synapse::core::RuntimeActionResult::DENIED_SIDE_EFFECTS);

    policy.allowSideEffects = true;
    sandbox.setPolicy(policy);
    auto explicitDenied = sandbox.authorize(synapse::core::AgentCapability::PROPOSE_KNOWLEDGE, true, false);
    assert(explicitDenied == synapse::core::RuntimeActionResult::DENIED_EXPLICIT_FLAG);

    auto explicitAllowed = sandbox.authorize(synapse::core::AgentCapability::PROPOSE_KNOWLEDGE, true, true);
    assert(explicitAllowed == synapse::core::RuntimeActionResult::ALLOWED);
}

static void testAgentScoreQuarantineAndDecay() {
    synapse::core::AgentScoreConfig cfg;
    cfg.baseScore = 1000;
    cfg.quarantineThreshold = 400;
    cfg.decayIntervalSeconds = 60;
    cfg.decayPerInterval = 10;
    synapse::core::AgentScoreCard score(cfg);

    score.applyPenalty(700, 100);
    assert(score.score() == 300);
    assert(score.quarantined());

    score.applyDecay(700);
    assert(score.score() == 400);
    assert(!score.quarantined());

    score.applyReward(50, 700);
    assert(score.score() == 450);
}

static void testSandboxFileBoundaries() {
    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path tmpRoot = fs::temp_directory_path() / ("synapsenet_agent_runtime_" + uniq);
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);
    fs::create_directories(tmpRoot / "public", ec);
    fs::create_directories(tmpRoot / "safe_write", ec);
    fs::create_directories(tmpRoot / "secrets", ec);

    synapse::core::SandboxPolicy policy;
    policy.allowSideEffects = true;
    policy.allowlist = {
        synapse::core::AgentCapability::READ_LEDGER,
        synapse::core::AgentCapability::PROPOSE_KNOWLEDGE,
        synapse::core::AgentCapability::READ_NETWORK
    };
    policy.workspaceRoot = tmpRoot.string();
    policy.readRoots = {"public"};
    policy.writeRoots = {"safe_write"};
    policy.deniedPathPrefixes = {"secrets"};
    policy.deniedFileNames = {"wallet.dat"};
    policy.deniedExtensions = {".pem", ".key"};

    synapse::core::AgentRuntimeSandbox sandbox(policy);

    auto readAllowed = sandbox.authorizePath(
        synapse::core::AgentCapability::READ_LEDGER,
        synapse::core::PathAccessMode::READ,
        (tmpRoot / "public" / "snapshot.json").string(),
        false
    );
    assert(readAllowed == synapse::core::RuntimeActionResult::ALLOWED);

    auto secretDenied = sandbox.authorizePath(
        synapse::core::AgentCapability::READ_LEDGER,
        synapse::core::PathAccessMode::READ,
        (tmpRoot / "wallet.dat").string(),
        false
    );
    assert(secretDenied == synapse::core::RuntimeActionResult::DENIED_SECRET_PATH);

    auto writeAllowed = sandbox.authorizePath(
        synapse::core::AgentCapability::PROPOSE_KNOWLEDGE,
        synapse::core::PathAccessMode::WRITE,
        (tmpRoot / "safe_write" / "draft.json").string(),
        true
    );
    assert(writeAllowed == synapse::core::RuntimeActionResult::ALLOWED);

    auto writeNoFlag = sandbox.authorizePath(
        synapse::core::AgentCapability::PROPOSE_KNOWLEDGE,
        synapse::core::PathAccessMode::WRITE,
        (tmpRoot / "safe_write" / "draft2.json").string(),
        false
    );
    assert(writeNoFlag == synapse::core::RuntimeActionResult::DENIED_EXPLICIT_FLAG);

    auto outsideDenied = sandbox.authorizePath(
        synapse::core::AgentCapability::PROPOSE_KNOWLEDGE,
        synapse::core::PathAccessMode::WRITE,
        (tmpRoot / "public" / "out.json").string(),
        true
    );
    assert(outsideDenied == synapse::core::RuntimeActionResult::DENIED_PATH_OUTSIDE_ROOT);

    auto extDenied = sandbox.authorizePath(
        synapse::core::AgentCapability::PROPOSE_KNOWLEDGE,
        synapse::core::PathAccessMode::WRITE,
        (tmpRoot / "safe_write" / "private.pem").string(),
        true
    );
    assert(extDenied == synapse::core::RuntimeActionResult::DENIED_SECRET_PATH);

    fs::remove_all(tmpRoot, ec);
}

static void testToolSchemaHardening() {
    synapse::core::SandboxPolicy policy;
    policy.allowSideEffects = true;
    policy.allowlist = {
        synapse::core::AgentCapability::READ_NETWORK,
        synapse::core::AgentCapability::PROPOSE_KNOWLEDGE
    };
    policy.maxToolPayloadBytes = 8192;
    synapse::core::AgentRuntimeSandbox sandbox(policy);

    std::vector<synapse::core::ToolSchemaRule> schemas;
    synapse::core::ToolSchemaRule observatory;
    observatory.toolName = "observatory.post";
    observatory.capability = synapse::core::AgentCapability::READ_NETWORK;
    observatory.requiredKeys = {"room", "kind", "payload"};
    observatory.maxPayloadBytes = 4096;
    observatory.allowSideEffects = true;
    observatory.requireExplicitSideEffectFlag = true;
    schemas.push_back(observatory);

    synapse::core::ToolSchemaRule draft;
    draft.toolName = "draft.enqueue";
    draft.capability = synapse::core::AgentCapability::PROPOSE_KNOWLEDGE;
    draft.requiredKeys = {"title", "body", "powBits", "powNonce"};
    draft.maxPayloadBytes = 65536;
    draft.allowSideEffects = true;
    draft.requireExplicitSideEffectFlag = true;
    schemas.push_back(draft);

    synapse::core::ToolInvocation ok;
    ok.toolName = "observatory.post";
    ok.capability = synapse::core::AgentCapability::READ_NETWORK;
    ok.keys = {"room", "kind", "payload"};
    ok.payloadBytes = 128;
    ok.hasSideEffects = true;
    ok.explicitSideEffectFlag = true;
    assert(sandbox.authorizeTool(ok, schemas) == synapse::core::RuntimeActionResult::ALLOWED);

    auto missingKey = ok;
    missingKey.keys = {"room", "kind"};
    assert(sandbox.authorizeTool(missingKey, schemas) == synapse::core::RuntimeActionResult::DENIED_TOOL_ARGUMENTS);

    auto extraKey = ok;
    extraKey.keys = {"room", "kind", "payload", "unexpected"};
    assert(sandbox.authorizeTool(extraKey, schemas) == synapse::core::RuntimeActionResult::DENIED_TOOL_ARGUMENTS);

    auto payloadTooLarge = ok;
    payloadTooLarge.payloadBytes = 50000;
    assert(sandbox.authorizeTool(payloadTooLarge, schemas) == synapse::core::RuntimeActionResult::DENIED_TOOL_PAYLOAD);

    auto explicitDenied = ok;
    explicitDenied.explicitSideEffectFlag = false;
    assert(sandbox.authorizeTool(explicitDenied, schemas) == synapse::core::RuntimeActionResult::DENIED_EXPLICIT_FLAG);

    auto unknownTool = ok;
    unknownTool.toolName = "shell.exec";
    assert(sandbox.authorizeTool(unknownTool, schemas) == synapse::core::RuntimeActionResult::DENIED_TOOL_SCHEMA);

    auto wrongCapability = ok;
    wrongCapability.capability = synapse::core::AgentCapability::PROPOSE_KNOWLEDGE;
    assert(sandbox.authorizeTool(wrongCapability, schemas) == synapse::core::RuntimeActionResult::DENIED_TOOL_SCHEMA);
}

static void testAdaptiveSchedulingFromAgentScore() {
    synapse::core::AgentScoreTracker score;
    synapse::core::AgentAdaptiveScheduler scheduler;

    auto scoreSnap = score.snapshot();
    auto schedule = scheduler.schedule(scoreSnap.throttled, scoreSnap.quarantined);
    assert(schedule.state == synapse::core::AgentSchedulingState::NORMAL);
    assert(schedule.pipelineIntervalSeconds == 15);

    score.applyStep(0, 3, 0);
    scoreSnap = score.snapshot();
    schedule = scheduler.schedule(scoreSnap.throttled, scoreSnap.quarantined);
    assert(schedule.state == synapse::core::AgentSchedulingState::THROTTLED);
    assert(schedule.pipelineIntervalSeconds == 45);

    score.applyStep(0, 5, 0);
    scoreSnap = score.snapshot();
    schedule = scheduler.schedule(scoreSnap.throttled, scoreSnap.quarantined);
    assert(schedule.state == synapse::core::AgentSchedulingState::QUARANTINED);
    assert(schedule.pipelineIntervalSeconds == 120);
}

static void testRuntimeSupervisorBackoffAndPersistence() {
    synapse::core::AgentRuntimeSupervisor supervisor;
    auto st = supervisor.crashState();
    assert(st.totalCrashes == 0);
    assert(st.consecutiveCrashes == 0);

    uint64_t b1 = supervisor.markFailure(1000);
    assert(b1 == 5);
    st = supervisor.crashState();
    assert(st.totalCrashes == 1);
    assert(st.consecutiveCrashes == 1);
    assert(st.recoveryUntilTimestamp == 1005);
    assert(supervisor.inRecovery(1002));

    uint64_t b2 = supervisor.markFailure(1010);
    assert(b2 == 10);
    st = supervisor.crashState();
    assert(st.totalCrashes == 2);
    assert(st.consecutiveCrashes == 2);
    assert(st.recoveryUntilTimestamp == 1020);

    auto s = supervisor.failoverState(false, false, 1015);
    assert(s == synapse::core::AgentRuntimeFailoverState::RECOVERY);

    supervisor.markSuccess();
    st = supervisor.crashState();
    assert(st.totalCrashes == 2);
    assert(st.consecutiveCrashes == 0);
    assert(st.recoveryUntilTimestamp == 0);

    s = supervisor.failoverState(true, false, 1021);
    assert(s == synapse::core::AgentRuntimeFailoverState::THROTTLED);
    s = supervisor.failoverState(false, true, 1021);
    assert(s == synapse::core::AgentRuntimeFailoverState::QUARANTINED);
    s = supervisor.failoverState(false, false, 1021);
    assert(s == synapse::core::AgentRuntimeFailoverState::ACTIVE);

    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path tmpRoot = fs::temp_directory_path() / ("synapsenet_runtime_supervisor_" + uniq);
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);
    fs::create_directories(tmpRoot, ec);
    fs::path stateFile = tmpRoot / "runtime.state";

    std::string reason;
    assert(supervisor.saveCrashState(stateFile.string(), &reason));
    assert(reason == "ok");

    synapse::core::AgentRuntimeSupervisor loaded;
    assert(loaded.loadCrashState(stateFile.string(), &reason));
    assert(reason == "ok");
    auto loadedState = loaded.crashState();
    assert(loadedState.totalCrashes == st.totalCrashes);
    assert(loadedState.consecutiveCrashes == st.consecutiveCrashes);
    assert(loadedState.lastCrashTimestamp == st.lastCrashTimestamp);
    assert(loadedState.recoveryUntilTimestamp == st.recoveryUntilTimestamp);

    synapse::core::AgentRuntimeSupervisor missing;
    assert(missing.loadCrashState((tmpRoot / "missing.state").string(), &reason));
    assert(reason == "not_found");

    fs::remove_all(tmpRoot, ec);
}

int main() {
    testSandboxDefaultDeny();
    testSandboxAllowlistAndSideEffectRules();
    testAgentScoreQuarantineAndDecay();
    testSandboxFileBoundaries();
    testToolSchemaHardening();
    testAdaptiveSchedulingFromAgentScore();
    testRuntimeSupervisorBackoffAndPersistence();
    return 0;
}
