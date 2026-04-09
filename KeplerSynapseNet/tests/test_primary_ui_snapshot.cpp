#include "tui/primary_ui_spec.h"

#include <cassert>
#include <vector>

static void testOverviewSnapshotGoldenAndBoundedWidth() {
    synapse::tui::AttachedAgentStatusInfo info;
    info.runtimeState = "RUN";
    info.runtimeInitialized = true;
    info.agentScore = 42;
    info.agentScoreBand = "OK";
    info.schedulerState = "IDLE";
    info.schedulerTick = 7;
    info.schedulerEpochIndex = 3;
    info.schedulerBudgetCpu = 2;
    info.schedulerBudgetRam = 4;
    info.schedulerBudgetNetwork = 6;
    info.routeMode = "TOR_ONLY";
    info.torRequired = true;
    info.torReachable = true;
    info.networkOverloadMode = false;
    info.networkPeerPressurePercent = 42;
    info.networkInboundPressurePercent = 38;
    info.networkOutboundPressurePercent = 47;
    info.networkInvBackpressureDrops = 5;
    info.networkGetDataBackpressureDrops = 2;
    info.networkGossipSuppressed = 11;
    info.networkGossipSubsetRouted = 7;
    info.miningActive = true;
    info.miningWorkTarget = "leading_zero_bits>=3";
    info.miningHashAttemptsLast = 1024;
    info.miningHashAttemptsTotal = 4096;
    info.miningCandidateHash = "abcd";
    info.quarantined = false;
    info.quarantineReason = "none";
    info.configPath = "/cfg";
    info.webConfigPath = "/webcfg";
    info.dataDir = "/data";

    const int width = 64;
    const auto lines = synapse::tui::primary_ui::renderMiningOverviewSnapshot(info, width);

    const std::vector<std::string> expected = {
        "AI MINING (NAAN) OVERVIEW [ALPHA DEVNET]",
        "Runtime: RUN init=yes",
        "Score: 42 band=OK",
        "Scheduler: IDLE tick=7 epoch=3",
        "Budget cpu/ram/net: 2/4/6",
        "Route: TOR_ONLY tor_required=yes tor_reachable=yes",
        "Network health overload=no peer/in/out=42/38/47",
        "Network drops inv/getdata: 5/2",
        "Network gossip suppr/subset: 11/7",
        "Hashing: active target=leading_zero_bits>=3",
        "Hash attempts last/total: 1024/4096",
        "Hash candidate: abcd",
        "Quarantine: no reason=none",
        "Config path: /cfg",
        "Web config: /webcfg",
        "Storage dir: /data"
    };

    assert(lines == expected);
    for (const auto& line : lines) {
        assert(static_cast<int>(line.size()) <= width);
    }
}

static void testSecuritySnapshotReflectsBackendTruth() {
    synapse::tui::StatusInfo info;
    info.quantumEnabled = true;
    info.liboqsEnabled = false;
    info.quantumCapabilityMode = "development_simulation";
    info.kyberReal = false;
    info.dilithiumReal = false;
    info.sphincsReal = false;
    info.kyberImplementation = "simulation";
    info.dilithiumImplementation = "simulation";
    info.sphincsImplementation = "simulation";

    const auto lines = synapse::tui::primary_ui::renderSecurityStatusSnapshot(info, 120);

    assert(lines.size() == 7);
    assert(lines[0] == "PQC configured: enabled");
    assert(lines[1] == "Capability mode: Development Simulation");
    assert(lines[2] == "Backend availability: simulation only");
    assert(lines[3] == "Kyber ML-KEM-768: simulated (simulation)");
    assert(lines[4] == "Dilithium ML-DSA-65: simulated (simulation)");
    assert(lines[5] == "SPHINCS+ SLH-DSA-128s: simulated (simulation)");
    assert(lines[6] == "Operator note: this node is running simulated PQC only.");
}

int main() {
    testOverviewSnapshotGoldenAndBoundedWidth();
    testSecuritySnapshotReflectsBackendTruth();
    return 0;
}
