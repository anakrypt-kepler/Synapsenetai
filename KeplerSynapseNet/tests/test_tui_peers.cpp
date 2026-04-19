#include "tui/primary_ui_spec.h"

#include <cassert>
#include <string>

static void testOverviewIncludesNetworkPeerSection() {
    synapse::tui::AttachedAgentStatusInfo info;
    info.runtimeState = "RUN";
    info.runtimeInitialized = true;
    info.agentScore = 10;
    info.agentScoreBand = "FULL";
    info.schedulerState = "SCHEDULE";
    info.schedulerTick = 12;
    info.schedulerEpochIndex = 1;
    info.schedulerBudgetCpu = 5;
    info.schedulerBudgetRam = 5;
    info.schedulerBudgetNetwork = 5;
    info.routeMode = "TOR_ONLY";
    info.torRequired = true;
    info.torReachable = true;
    info.networkOverloadMode = false;
    info.networkPeerPressurePercent = 77;
    info.networkInboundPressurePercent = 61;
    info.networkOutboundPressurePercent = 55;
    info.networkInvBackpressureDrops = 3;
    info.networkGetDataBackpressureDrops = 2;
    info.networkGossipSuppressed = 9;
    info.networkGossipSubsetRouted = 4;
    info.miningActive = true;
    info.miningWorkTarget = "leading_zero_bits>=4";
    info.miningHashAttemptsLast = 111;
    info.miningHashAttemptsTotal = 222;
    info.miningCandidateHash = "abc123";
    info.quarantined = false;
    info.quarantineReason = "none";
    info.configPath = "/cfg";
    info.webConfigPath = "/webcfg";
    info.dataDir = "/data";

    const auto lines = synapse::tui::primary_ui::renderMiningOverviewSnapshot(info, 120);
    bool hasPeerPressure = false;
    for (const auto& line : lines) {
        if (line.find("peer/in/out=77/61/55") != std::string::npos) {
            hasPeerPressure = true;
            break;
        }
    }
    assert(hasPeerPressure);
}

int main() {
    testOverviewIncludesNetworkPeerSection();
    return 0;
}
