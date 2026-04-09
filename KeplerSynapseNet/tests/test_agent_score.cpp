#include "core/agent_score.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace synapse::core;

namespace fs = std::filesystem;

static AgentScorePolicy makePolicy() {
    AgentScorePolicy p;
    p.scoreMin = -500;
    p.scoreMax = 500;
    p.initialScore = 0;
    p.decayNumerator = 95;
    p.decayDenominator = 100;
    p.acceptWeight = 10;
    p.rejectWeight = 30;
    p.violationWeight = 120;
    p.throttledBelowOrEqual = -60;
    p.reviewOnlyBelowOrEqual = -120;
    p.localDraftOnlyBelowOrEqual = -200;
    p.localDraftRecoveryAbove = -120;
    p.localDraftRecoveryCleanSteps = 3;
    p.normalBatchLimit = 16;
    p.throttledBatchLimit = 4;
    p.reviewOnlyBatchLimit = 0;
    p.localDraftOnlyBatchLimit = 0;
    return p;
}

static void testDeterministicUpdateRule() {
    AgentScoreTracker tracker(makePolicy());

    tracker.applyStep(2, 0, 0);
    auto s = tracker.snapshot();
    assert(s.score == 20);

    tracker.applyStep(0, 1, 0);
    s = tracker.snapshot();
    assert(s.score == -11);

    tracker.applyStep(0, 0, 1);
    s = tracker.snapshot();
    assert(s.score == -131);
    assert(s.steps == 3);
    assert(s.acceptedTotal == 2);
    assert(s.rejectedTotal == 1);
    assert(s.violationsTotal == 1);
}

static void testBandMappingAndBatchLimits() {
    AgentScoreTracker tracker(makePolicy());
    auto s = tracker.snapshot();
    assert(s.band == AgentScoreBand::FULL);
    assert(s.batchLimit == 16);

    tracker.applyStep(0, 3, 0);
    s = tracker.snapshot();
    assert(s.band == AgentScoreBand::THROTTLED);
    assert(s.throttled);
    assert(!s.reviewOnly);
    assert(!s.localDraftOnly);
    assert(s.batchLimit == 4);

    tracker.applyStep(0, 1, 0);
    tracker.applyStep(0, 1, 0);
    s = tracker.snapshot();
    assert(s.band == AgentScoreBand::REVIEW_ONLY);
    assert(s.throttled);
    assert(s.reviewOnly);
    assert(!s.quarantined);
    assert(s.batchLimit == 0);

    tracker.applyStep(0, 3, 0);
    s = tracker.snapshot();
    assert(s.band == AgentScoreBand::LOCAL_DRAFT_ONLY);
    assert(s.quarantined);
    assert(s.localDraftOnly);
    assert(s.batchLimit == 0);
}

static void testLocalDraftRecoveryCriteria() {
    AgentScorePolicy p = makePolicy();
    p.throttledBelowOrEqual = -60;
    p.reviewOnlyBelowOrEqual = -150;
    p.localDraftOnlyBelowOrEqual = -320;
    p.localDraftRecoveryAbove = -310;
    p.localDraftRecoveryCleanSteps = 3;

    AgentScoreTracker tracker(p);
    tracker.applyStep(0, 0, 3);
    auto s = tracker.snapshot();
    assert(s.band == AgentScoreBand::LOCAL_DRAFT_ONLY);
    assert(s.quarantined);
    assert(s.cleanSteps == 0);

    tracker.applyStep(0, 0, 0);
    s = tracker.snapshot();
    assert(s.band == AgentScoreBand::LOCAL_DRAFT_ONLY);
    assert(s.cleanSteps == 1);

    tracker.applyStep(0, 0, 0);
    s = tracker.snapshot();
    assert(s.band == AgentScoreBand::LOCAL_DRAFT_ONLY);
    assert(s.cleanSteps == 2);

    tracker.applyStep(0, 0, 0);
    s = tracker.snapshot();
    assert(s.band == AgentScoreBand::REVIEW_ONLY);
    assert(!s.quarantined);
    assert(s.throttled);
}

static void testClampBehavior() {
    AgentScorePolicy p = makePolicy();
    p.scoreMin = -100;
    p.scoreMax = 100;
    AgentScoreTracker tracker(p);

    tracker.applyStep(50, 0, 0);
    auto s = tracker.snapshot();
    assert(s.score == 100);

    tracker.applyStep(0, 0, 5);
    s = tracker.snapshot();
    assert(s.score == -100);
}

static void testStatePersistence() {
    AgentScoreTracker tracker(makePolicy());
    tracker.applyStep(0, 3, 0);
    tracker.applyStep(0, 1, 0);
    tracker.applyStep(0, 1, 0);
    auto before = tracker.snapshot();

    const auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path tmpDir = fs::temp_directory_path() / ("synapsenet_agent_score_" + uniq);
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);
    fs::path statePath = tmpDir / "score.state";

    std::string reason;
    assert(tracker.saveState(statePath.string(), &reason));
    assert(reason == "ok");

    AgentScoreTracker loaded(makePolicy());
    assert(loaded.loadState(statePath.string(), &reason));
    assert(reason == "ok");

    auto after = loaded.snapshot();
    assert(after.score == before.score);
    assert(after.band == before.band);
    assert(after.cleanSteps == before.cleanSteps);
    assert(after.steps == before.steps);
    assert(after.acceptedTotal == before.acceptedTotal);
    assert(after.rejectedTotal == before.rejectedTotal);
    assert(after.violationsTotal == before.violationsTotal);

    AgentScoreTracker missing(makePolicy());
    assert(missing.loadState((tmpDir / "missing.state").string(), &reason));
    assert(reason == "not_found");

    fs::remove_all(tmpDir, ec);
}

int main() {
    std::cout << "Running agent_score tests...\n";
    testDeterministicUpdateRule();
    testBandMappingAndBatchLimits();
    testLocalDraftRecoveryCriteria();
    testClampBehavior();
    testStatePersistence();
    std::cout << "All agent_score tests passed!\n";
    return 0;
}
