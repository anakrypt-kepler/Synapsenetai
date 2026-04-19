#include "tui/tui_runtime.h"

#include "utils/logger.h"

#include <chrono>
#include <thread>

namespace synapse::tui {

std::thread startTuiUpdateThread(TUI& ui, const TuiUpdateHooks& hooks) {
    return std::thread([&ui, hooks]() {
        if (!hooks.shouldKeepRunning || !hooks.refreshWalletState ||
            !hooks.getCoreSnapshot || !hooks.getKnowledgeRefresh ||
            !hooks.getAttachedAgentStatus || !hooks.getObservatoryFeed ||
            !hooks.getAgentEvents) {
            utils::Logger::error("Invalid TUI runtime hooks");
            return;
        }

        while (hooks.shouldKeepRunning()) {
            hooks.refreshWalletState();

            TuiCoreSnapshot core = hooks.getCoreSnapshot();
            ui.updateNetworkInfo(core.networkInfo);
            ui.setPeerCount(core.peerCount);
            ui.updatePeers(core.peers);
            ui.updateModelInfo(core.modelInfo);
            ui.updateWalletInfo(core.walletInfo);
            ui.updateStatus(core.statusInfo);

            TuiKnowledgeRefresh knowledge = hooks.getKnowledgeRefresh();
            ui.updateKnowledgeEntries(knowledge.entries);
            for (const auto& message : knowledge.chatMessages) {
                ui.appendChatMessage(message.role, message.content);
            }

            ui.updateAttachedAgentStatus(hooks.getAttachedAgentStatus());
            ui.updateObservatoryFeed(hooks.getObservatoryFeed());
            ui.updateAgentEvents(hooks.getAgentEvents());

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

} // namespace synapse::tui
