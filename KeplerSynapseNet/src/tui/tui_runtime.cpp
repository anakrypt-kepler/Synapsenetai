#include "tui/tui_runtime.h"

#include "utils/logger.h"

#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace synapse::tui {

static std::vector<HarvestEntrySummary> scanHarvestFiles(const std::string& dataDir) {
    std::vector<HarvestEntrySummary> out;
    std::string dir = dataDir + "/knowledge";
    try {
        if (!std::filesystem::exists(dir)) return out;
    } catch (...) { return out; }

    std::vector<std::pair<std::filesystem::file_time_type, std::string>> files;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            auto name = entry.path().filename().string();
            if (name.find("harvest_") == 0 && name.find(".json") != std::string::npos) {
                files.push_back({entry.last_write_time(), entry.path().string()});
            }
        }
    } catch (...) { return out; }

    std::sort(files.begin(), files.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    for (size_t i = 0; i < files.size() && i < 200; i++) {
        std::ifstream f(files[i].second);
        if (!f.good()) continue;
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

        HarvestEntrySummary entry;

        auto extractStr = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\"";
            size_t p = content.find(search);
            if (p == std::string::npos) return "";
            size_t colon = content.find(':', p + search.size());
            if (colon == std::string::npos) return "";
            size_t q1 = content.find('"', colon);
            if (q1 == std::string::npos) return "";
            size_t q2 = q1 + 1;
            bool escaped = false;
            while (q2 < content.size()) {
                if (escaped) { escaped = false; q2++; continue; }
                if (content[q2] == '\\') { escaped = true; q2++; continue; }
                if (content[q2] == '"') break;
                q2++;
            }
            return content.substr(q1 + 1, q2 - q1 - 1);
        };

        auto extractInt = [&](const std::string& key) -> uint64_t {
            std::string search = "\"" + key + "\"";
            size_t p = content.find(search);
            if (p == std::string::npos) return 0;
            size_t colon = content.find(':', p + search.size());
            if (colon == std::string::npos) return 0;
            return std::strtoull(content.c_str() + colon + 1, nullptr, 10);
        };

        entry.draftSha256 = extractStr("draft_sha256");
        entry.topic = extractStr("topic");
        entry.title = extractStr("title");
        entry.timestamp = extractInt("timestamp");

        size_t bypassP = content.find("\"bypass\"");
        if (bypassP != std::string::npos) {
            size_t brace = content.find('{', bypassP);
            size_t braceEnd = content.find('}', brace);
            if (brace != std::string::npos && braceEnd != std::string::npos) {
                std::string block = content.substr(brace, braceEnd - brace + 1);
                size_t cp = block.find("\"cve\"");
                if (cp != std::string::npos) {
                    size_t q1 = block.find('"', cp + 5);
                    size_t q2 = block.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        entry.cveId = block.substr(q1 + 1, q2 - q1 - 1);
                }
                size_t mp = block.find("\"method\"");
                if (mp != std::string::npos) {
                    size_t q1 = block.find('"', mp + 8);
                    size_t q2 = block.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        entry.bypassMethod = block.substr(q1 + 1, q2 - q1 - 1);
                }
                size_t tp = block.find("\"transport\"");
                if (tp != std::string::npos) {
                    size_t q1 = block.find('"', tp + 11);
                    size_t q2 = block.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        entry.transport = block.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }

        size_t assetsP = content.find("\"assets\"");
        if (assetsP != std::string::npos) {
            size_t arrStart = content.find('[', assetsP);
            size_t arrEnd = content.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = content.substr(arrStart, arrEnd - arrStart + 1);
                int count = 0;
                size_t pos = 0;
                while ((pos = arr.find('{', pos)) != std::string::npos) {
                    count++;
                    pos++;
                }
                entry.assetCount = count;

                bool allClean = true;
                bool anyMal = false;
                size_t vtPos = 0;
                while ((vtPos = arr.find("\"vt\"", vtPos)) != std::string::npos) {
                    size_t q1 = arr.find('"', vtPos + 4);
                    size_t q2 = arr.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        std::string vt = arr.substr(q1 + 1, q2 - q1 - 1);
                        if (vt != "clean") allClean = false;
                        if (vt.find("malicious") != std::string::npos) anyMal = true;
                    }
                    vtPos = q2 + 1;
                }
                if (count == 0) entry.vtStatus = "-";
                else if (anyMal) entry.vtStatus = "MAL";
                else if (allClean) entry.vtStatus = "OK";
                else entry.vtStatus = "UNK";
            }
        }

        std::string text = extractStr("text");
        if (text.size() > 120) text = text.substr(0, 120) + "...";
        entry.textPreview = text;

        out.push_back(entry);
    }
    return out;
}

std::thread startTuiUpdateThread(TUI& ui, const TuiUpdateHooks& hooks) {
    return std::thread([&ui, hooks]() {
        if (!hooks.shouldKeepRunning || !hooks.refreshWalletState ||
            !hooks.getCoreSnapshot || !hooks.getKnowledgeRefresh ||
            !hooks.getAttachedAgentStatus || !hooks.getObservatoryFeed ||
            !hooks.getAgentEvents) {
            utils::Logger::error("Invalid TUI runtime hooks");
            return;
        }

        int harvestTick = 0;
        std::string dataDir;

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

            if (harvestTick % 5 == 0) {
                if (dataDir.empty()) {
                    const char* home = std::getenv("HOME");
                    if (home) dataDir = std::string(home) + "/.synapsenet";
                    else dataDir = "/tmp/.synapsenet";
                }
                auto entries = scanHarvestFiles(dataDir);
                ui.updateHarvestEntries(entries);
            }
            harvestTick++;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

} // namespace synapse::tui
