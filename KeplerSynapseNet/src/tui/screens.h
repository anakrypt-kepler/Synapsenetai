#pragma once

#include "tui/tui.h"
#include <string>
#include <vector>
#include <cstdint>

namespace synapse {
namespace tui {

struct AppState {
    WalletInfo wallet;
    NetworkInfo network;
    AIModelInfo model;
    std::vector<NodeInfo> peers;
    std::vector<ContributionInfo> recentContributions;
    int qualityScore;
    int networkRank;
    int knowledgeShared;
    int validations;
    std::string nodeId;
    bool isFirstRun;
    double miningProgress;
};

extern const char* SYNAPSENET_LOGO[];
extern const int SYNAPSENET_LOGO_LINES;

extern const char* KEPLER_LOGO[];
extern const int KEPLER_LOGO_LINES;

}
}
