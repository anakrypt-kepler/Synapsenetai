#pragma once

#include "tui/tui.h"

#include <string>
#include <vector>

namespace synapse::tui::primary_ui {

enum class DashboardRoute {
    NONE,
    AI_MINING,
    AI_QUERY,
    QUIT
};

DashboardRoute routeDashboardKey(int ch);

enum class MiningPage {
    OVERVIEW = 0,
    NETWORK_MINING = 1,
    WALLET_EARNINGS = 2,
    CONTRIBUTIONS = 3,
    SECURITY_POLICY = 4,
    LOGS_DIAGNOSTICS = 5,
    AGENT_NETWORK = 6,
    IDE_CODE = 7,
    STORAGE_RECOVERY = 8
};

MiningPage routeMiningPageKey(MiningPage current, int ch, bool* handled = nullptr);

struct ChatStatusStripInput {
    bool torRequired = true;
    bool torReachable = false;
    bool torDegraded = false;
    bool onionServiceActive = false;
    bool clearnetFallbackAllowed = false;
    std::string agentActivity;
};

std::string renderChatStatusStrip(const ChatStatusStripInput& input, int width);

std::vector<std::string> renderMiningOverviewSnapshot(const AttachedAgentStatusInfo& info, int width);
std::vector<std::string> renderSecurityStatusSnapshot(const StatusInfo& info, int width);

}
