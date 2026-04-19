#pragma once

#include "tui/tui.h"

#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace synapse::tui {

struct TuiChatMessage {
    std::string role;
    std::string content;
};

struct TuiCoreSnapshot {
    NetworkInfo networkInfo;
    size_t peerCount = 0;
    std::vector<NodeInfo> peers;
    AIModelInfo modelInfo;
    WalletInfo walletInfo;
    StatusInfo statusInfo;
};

struct TuiKnowledgeRefresh {
    std::vector<KnowledgeEntrySummary> entries;
    std::vector<TuiChatMessage> chatMessages;
};

struct TuiUpdateHooks {
    std::function<bool()> shouldKeepRunning;
    std::function<void()> refreshWalletState;
    std::function<TuiCoreSnapshot()> getCoreSnapshot;
    std::function<TuiKnowledgeRefresh()> getKnowledgeRefresh;
    std::function<AttachedAgentStatusInfo()> getAttachedAgentStatus;
    std::function<std::vector<ObservatoryArtifactInfo>()> getObservatoryFeed;
    std::function<std::vector<AgentEventInfo>()> getAgentEvents;
};

std::thread startTuiUpdateThread(TUI& ui, const TuiUpdateHooks& hooks);

} // namespace synapse::tui
