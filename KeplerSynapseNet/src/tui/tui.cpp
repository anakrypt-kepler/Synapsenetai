#include "tui/tui.h"
#include "tui/bip39_wordlist.h"
#include "tui/primary_ui_spec.h"
#include "screens.h"
#include <ncurses.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <random>
#include <array>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstdlib>
#include <cmath>
#include <unordered_set>
#include "model/model_loader.h"
#include "utils/config.h"
#include "utils/utils.h"
#include "crypto/keys.h"
#include "core/tor_bridge_utils.h"
#include "web/web.h"

namespace synapse {
namespace tui {

static const char* LOGO_SYNAPSENET[] = {
    " _____                                 _   ",
    "|   __|_ _ ___ ___ ___ ___ ___ ___ ___| |_ ",
    "|__   | | |   | .'| . |_ -| -_|   | -_|  _|",
    "|_____|_  |_|_|__,|  _|___|___|_|_|___|_|  ",
    "      |___|       |_|                      "
};
static const int LOGO_SYNAPSENET_COUNT = 5;

static const char* LOGO_KEPLER[] = {
    " _____         _         ",
    "|  |  |___ ___| |___ ___ ",
    "|    -| -_| . | | -_|  _|",
    "|__|__|___|  _|_|___|_|  ",
    "          |_|            "
};
static const int LOGO_KEPLER_COUNT = 5;

static const char* STARTUP_BANNER[] = {
    "                                     _   ",
    " ___ _ _ ___ ___ ___ ___ ___ ___ ___| |_ ",
    "|_ -| | |   | .'| . |_ -| -_|   | -_|  _|",
    "|___|_  |_|_|__,|  _|___|___|_|_|___|_|  ",
    "    |___|       |_|                      "
};
static const int STARTUP_BANNER_COUNT = 5;

static std::string shortPqcProtectionLabel(const StatusInfo& status) {
    if (!status.quantumEnabled) {
        return "Configured PQC Disabled";
    }
    if (status.kyberReal && status.dilithiumReal && status.sphincsReal) {
        return "Real PQC Backends";
    }
    if (status.kyberReal || status.dilithiumReal || status.sphincsReal) {
        return "Mixed Real/Simulated PQC";
    }
    return "Simulated PQC Only";
}

static std::string shortPqcCapabilityLabel(const StatusInfo& status) {
    if (status.quantumCapabilityMode == "real_pqc") {
        return "Real PQC";
    }
    if (status.quantumCapabilityMode == "mixed_real_and_simulated") {
        return "Mixed PQC";
    }
    if (status.quantumCapabilityMode == "compiled_real_backend_unavailable") {
        return "Compiled Backend Unavailable";
    }
    if (status.quantumCapabilityMode == "development_simulation") {
        return "Development Simulation";
    }
    if (!status.quantumCapabilityMode.empty()) {
        return status.quantumCapabilityMode;
    }
    return "Unknown";
}

static std::array<uint8_t, 7> glyph5x7(char c) {
    switch (c) {
        case 'A': return {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
        case 'B': return {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110};
        case 'C': return {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110};
        case 'D': return {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110};
        case 'E': return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
        case 'F': return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000};
        case 'G': return {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110};
        case 'H': return {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
        case 'I': return {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
        case 'J': return {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100};
        case 'K': return {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001};
        case 'L': return {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
        case 'M': return {0b10001, 0b11011, 0b10101, 0b10001, 0b10001, 0b10001, 0b10001};
        case 'N': return {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001};
        case 'O': return {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
        case 'P': return {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
        case 'Q': return {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101};
        case 'R': return {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001};
        case 'S': return {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
        case 'T': return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
        case 'U': return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
        case 'V': return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100};
        case 'W': return {0b10001, 0b10001, 0b10001, 0b10001, 0b10101, 0b11011, 0b10001};
        case 'X': return {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001};
        case 'Y': return {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100};
        case 'Z': return {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111};
        default: return {0, 0, 0, 0, 0, 0, 0};
    }
}

static std::vector<std::string> renderDotText5x7(const std::string& text) {
    constexpr int H = 7;
    std::vector<std::string> lines(H, "");
    for (char raw : text) {
        if (raw == ' ') {
            for (int r = 0; r < H; ++r) lines[r].append("   ");
            continue;
        }
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        auto rows = glyph5x7(c);
        for (int r = 0; r < H; ++r) {
            for (int b = 4; b >= 0; --b) {
                bool on = (rows[static_cast<size_t>(r)] >> b) & 1U;
                lines[r].push_back(on ? '.' : ' ');
                lines[r].push_back(on ? '.' : ' ');
            }
            lines[r].push_back(' ');
        }
    }
    return lines;
}

static int safeScreenWidth(int x, int requested) {
    if (requested <= 0) return 0;
    int maxW = COLS - x;
    if (maxW <= 0) return 0;
    if (maxW > 1) maxW -= 1;
    return std::min(requested, maxW);
}

static std::string truncEnd(const std::string& s, int maxLen) {
    if (maxLen <= 0) return {};
    if (static_cast<int>(s.size()) <= maxLen) return s;
    if (maxLen <= 3) return s.substr(0, static_cast<size_t>(maxLen));
    return s.substr(0, static_cast<size_t>(maxLen - 3)) + "...";
}

static std::string truncStart(const std::string& s, int maxLen) {
    if (maxLen <= 0) return {};
    if (static_cast<int>(s.size()) <= maxLen) return s;
    if (maxLen <= 3) return s.substr(s.size() - static_cast<size_t>(maxLen));
    return "..." + s.substr(s.size() - static_cast<size_t>(maxLen - 3));
}

static void printClippedLine(int y, int x, int width, const std::string& s) {
    int w = safeScreenWidth(x, width);
    if (w <= 0) return;
    mvhline(y, x, ' ', w);
    mvaddnstr(y, x, s.c_str(), w);
}

static std::vector<std::string> wrapLines(const std::string& s, int width, int maxLines) {
    std::vector<std::string> out;
    if (width <= 0 || maxLines <= 0) return out;
    if (s.empty()) {
        out.emplace_back();
        return out;
    }
    size_t pos = 0;
    while (pos < s.size() && static_cast<int>(out.size()) < maxLines) {
        size_t len = std::min(static_cast<size_t>(width), s.size() - pos);
        out.push_back(s.substr(pos, len));
        pos += len;
    }
    if (pos < s.size() && !out.empty()) {
        out.back() = truncStart(s, width);
    }
    return out;
}

static std::string formatUnixTime(uint64_t ts) {
    if (ts == 0) return "n/a";
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tmLocal{};
#if defined(_WIN32)
    localtime_s(&tmLocal, &t);
#else
    localtime_r(&t, &tmLocal);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmLocal) == 0) {
        return "n/a";
    }
    return std::string(buf);
}

static std::string sanitizeEventField(std::string s) {
    for (char& c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127) c = ' ';
    }
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static std::string trimAsciiCopy(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

static std::string toLowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static bool startsWithPrefix(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static std::string joinCsvList(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << ",";
        out << values[i];
    }
    return out.str();
}

static void dedupeStringsInPlace(std::vector<std::string>& values) {
    std::vector<std::string> deduped;
    std::unordered_set<std::string> seen;
    deduped.reserve(values.size());
    for (const auto& raw : values) {
        const std::string value = trimAsciiCopy(raw);
        if (value.empty()) continue;
        if (seen.insert(value).second) {
            deduped.push_back(value);
        }
    }
    values.swap(deduped);
}

static bool isLikelyOnionRule(const std::string& value) {
    return value.find(".onion") != std::string::npos;
}

static std::string normalizeSiteRuleInput(const std::string& rawInput,
                                          bool onionTarget,
                                          std::string* errorOut) {
    std::string value = toLowerAsciiCopy(trimAsciiCopy(rawInput));
    if (value.empty()) {
        if (errorOut) *errorOut = "empty rule";
        return {};
    }

    if (value == "*") {
        if (errorOut) *errorOut = "";
        return value;
    }

    auto validateTarget = [&](const std::string& normalized) -> bool {
        if (onionTarget) {
            return isLikelyOnionRule(normalized);
        }
        return !isLikelyOnionRule(normalized);
    };

    if (startsWithPrefix(value, "host:") || startsWithPrefix(value, "suffix:")) {
        const size_t sep = value.find(':');
        const std::string kind = value.substr(0, sep + 1);
        std::string host = trimAsciiCopy(value.substr(sep + 1));
        if (host.empty()) {
            if (errorOut) *errorOut = "missing host after rule prefix";
            return {};
        }
        host = toLowerAsciiCopy(host);
        if (!validateTarget(host)) {
            if (errorOut) *errorOut = onionTarget ? "rule must target .onion host" : "rule must target clearnet host";
            return {};
        }
        if (errorOut) *errorOut = "";
        return kind + host;
    }

    if (startsWithPrefix(value, "http://") || startsWithPrefix(value, "https://")) {
        const std::string host = toLowerAsciiCopy(trimAsciiCopy(synapse::web::extractDomain(value)));
        if (host.empty()) {
            if (errorOut) *errorOut = "cannot parse host from URL";
            return {};
        }
        value = host;
    } else {
        size_t slash = value.find('/');
        if (slash != std::string::npos) value = value.substr(0, slash);
        size_t colon = value.find(':');
        if (colon != std::string::npos) value = value.substr(0, colon);
        if (startsWithPrefix(value, "*.")) {
            value = "*." + trimAsciiCopy(value.substr(2));
        }
    }

    value = trimAsciiCopy(value);
    if (value.empty()) {
        if (errorOut) *errorOut = "empty host";
        return {};
    }
    while (!value.empty() && value.back() == '.') value.pop_back();
    if (value.empty()) {
        if (errorOut) *errorOut = "invalid host";
        return {};
    }
    if (!validateTarget(value)) {
        if (errorOut) *errorOut = onionTarget ? "host must end with .onion" : "clearnet list does not accept .onion";
        return {};
    }
    if (errorOut) *errorOut = "";
    return value;
}

static bool ensureNaanWebConfigFileExists(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (std::filesystem::exists(path)) return true;

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    out << "clearnet_engines=duckduckgo\n";
    out << "darknet_engines=ahmia,torch,darksearch,deepsearch\n";
    out << "custom_darknet_urls=http://juhanurmihxlp77nkq76byazcldy2hlmovfu2epvl5ankdibsot4csyd.onion/\n";
    out << "route_clearnet_through_tor=1\n";
    out << "naan_force_tor_mode=1\n";
    out << "naan_auto_search_enabled=1\n";
    out << "naan_auto_search_mode=both\n";
    out << "naan_auto_search_queries=latest space engineering research,latest ai research papers,open source systems engineering best practices\n";
    out << "naan_auto_search_max_results=4\n";
    out << "clearnet_site_allowlist=\n";
    out << "clearnet_site_denylist=\n";
    out << "onion_site_allowlist=\n";
    out << "onion_site_denylist=\n";
    out << "clearnet_route_bypass_hosts=\n";
    out << "onion_route_bypass_hosts=\n";
    out << "bypass_onion_https_fallback=1\n";
    return out.good();
}

struct ChatMessage {
    std::string role;
    std::string content;
};

struct LocalAppState {
    WalletInfo wallet;
    NetworkInfo network;
    AIModelInfo model;
    std::vector<NodeInfo> peers;
    std::vector<ContributionInfo> recentContributions;
    std::vector<KnowledgeEntrySummary> knowledgeEntries;
    std::mutex knowledgeMutex;
    int qualityScore = 0;
    int networkRank = 0;
    int knowledgeShared = 0;
    int validations = 0;
    std::string nodeId;
    bool isFirstRun = true;
    std::vector<std::string> generatedSeedWords;
    double miningProgress = 0;
    std::vector<ChatMessage> chatHistory;
    std::mutex chatMutex;
    bool aiGenerating = false;
    std::string modelPath;
    uint16_t listeningPort = 0;
    bool networkOnline = false;
    std::string sendToAddress;
    double sendAmount = 0.0;
    std::string sendAmountStr;
    int walletScreen = 0;
    std::vector<std::string> availableModels;
    int selectedModelIndex = 0;
    bool modelSelectionActive = false;
    std::string aiCurrentResponse;
    std::mutex aiResponseMutex;
    bool forceNewWallet = false;
    std::string downloadPath;
    std::string downloadPartPath;
    std::string knowledgeQuestion;
    std::string knowledgeAnswer;
    std::string knowledgeSource;
    int knowledgeField = 0;
    std::string codeTitle;
    std::string codePatchFile;
    std::string codeCitations;
    int codeField = 0;
    bool webInjectEnabled = false;
    bool webOnionEnabled = false;
    bool webTorForClearnet = false;
    bool webSearching = false;
    uint64_t webLastResults = 0;
    uint64_t webLastClearnetResults = 0;
    uint64_t webLastDarknetResults = 0;
    std::string webLastError;
    std::mutex webMutex;
    bool startupKeyDataLoaded = false;
    bool startupKeyDataAvailable = false;
    bool startupKeyBackupSaved = false;
    std::string startupWalletPath;
    std::string startupWalletAddress;
    std::string startupPublicKeyHex;
    std::string startupPrivateKeyHex;
    std::string startupKeyPromptMessage;
    std::string startupKeyBackupPath;
    
    // Operation status tracking
    struct OperationStatus {
        std::string operation;
        std::string status;
        std::string details;
        uint64_t timestamp;
    };
    OperationStatus currentOperation;
    std::vector<OperationStatus> operationHistory;
    std::mutex operationMutex;
    
    struct RewardNotification {
        uint64_t amountAtoms;
        std::string reason;
        std::string entryId;
        std::string details;
        uint64_t timestamp;
    };
    std::vector<RewardNotification> rewardHistory;
    std::mutex rewardMutex;

    std::vector<AgentEventInfo> agentEvents;
    std::mutex agentEventMutex;

    AttachedAgentStatusInfo attachedAgent;
    std::vector<ObservatoryArtifactInfo> observatoryFeed;
    std::mutex naanMutex;
};

static void pushAgentEvent(LocalAppState& state,
                           uint64_t timestamp,
                           const std::string& category,
                           const std::string& message) {
    AgentEventInfo ev;
    ev.timestamp = timestamp == 0 ? static_cast<uint64_t>(std::time(nullptr)) : timestamp;
    ev.category = sanitizeEventField(category);
    ev.message = sanitizeEventField(message);
    if (ev.category.empty() || ev.message.empty()) return;

    std::lock_guard<std::mutex> lock(state.agentEventMutex);
    if (!state.agentEvents.empty()) {
        const auto& last = state.agentEvents.back();
        if (last.category == ev.category && last.message == ev.message && last.timestamp == ev.timestamp) {
            return;
        }
    }
    state.agentEvents.push_back(std::move(ev));
    if (state.agentEvents.size() > 600) {
        size_t overflow = state.agentEvents.size() - 600;
        state.agentEvents.erase(
            state.agentEvents.begin(),
            state.agentEvents.begin() + static_cast<std::vector<AgentEventInfo>::difference_type>(overflow));
    }
}

struct TUI::Impl {
    Screen screen = Screen::BOOT;
    std::atomic<bool> running{false};
    // Guards shared UI state mutated by updateThread and read by draw loop.
    std::recursive_mutex uiStateMtx;
    StatusInfo status{};
    LocalAppState state{};
    std::function<void(int)> inputHandler;
    std::function<void(const std::string&)> commandHandler;
    int initStep = 0;
    int frameCounter = 0;
    int menuSelection = 0;
    int scrollOffset = 0;
    int observatoryScroll = 0;
    int observatorySelection = 0;
    primary_ui::MiningPage attachedAgentPage = primary_ui::MiningPage::OVERVIEW;
    int attachedAgentLogScroll = 0;
    int attachedAgentLogFilter = 0;
    int chatScrollOffset = 0;
	    bool autoScrollEnabled = true;
	    int webPromptStep = 0;
	    int webPromptBridgeMode = 0;
	    bool webPromptDone = false;
	    bool bridgePasteContinueAfter = false;
	    Screen bridgePasteReturnScreen = Screen::WEB_PROMPT;
	    size_t bridgePasteInvalid = 0;
	    std::vector<std::string> bridgePasteLines;
	    std::string bridgePasteLine;
	    std::string bridgePasteStatus;
	    Screen siteAllowlistReturnScreen = Screen::SETTINGS;
	    int siteAllowlistTarget = 0;
	    size_t siteAllowlistInvalid = 0;
	    std::vector<std::string> siteAllowlistClearnet;
	    std::vector<std::string> siteAllowlistOnion;
	    std::string siteAllowlistLine;
	    std::string siteAllowlistStatus;
	    std::string inputBuffer;
	    size_t inputCursor = 0;
	    std::vector<std::string> inputHistory;
	    size_t inputHistoryIndex = 0;
    std::string inputHistoryDraft;
    bool inputHistoryBrowsing = false;
    std::unique_ptr<synapse::model::ModelLoader> modelLoader;
    std::unique_ptr<synapse::web::WebSearch> webSearch;
    std::unique_ptr<synapse::web::QueryDetector> webDetector;
    std::unique_ptr<synapse::web::HtmlExtractor> webExtractor;
    std::unique_ptr<synapse::web::AIWrapper> webAi;
    std::atomic<bool> modelDownloadActive{false};
    std::atomic<bool> aiCancelRequested{false};
    bool aiModelPanelActive = false;
    int aiModelSelection = 0;
    int aiModelScroll = 0;
    uint64_t lastModelScanMs = 0;
    
    void drawBoot();
    void drawKeyBackup();
    void drawInit();
	    void drawNetworkDiscovery();
	    void drawSyncing();
	    void drawWebPrompt();
	    void drawBridgePaste();
	    void drawSiteAllowlistPaste();
	    void drawWelcome();
	    void drawWalletCreate();
	    void drawWalletCreated();
	    void drawWalletImport();
    void drawConnected();
    void drawDashboard();
    void drawWallet();
    void drawNetwork();
    void drawPeers();
    void drawKnowledge();
    void drawKnowledgeSubmit();
    void drawCode();
    void drawCodeSubmit();
    void drawModel();
    void drawAIChat();
    void drawSecurity();
    void drawSettings();
    void drawMining();
    void drawAttachedAgent();
    void drawObservatory();
    void drawModelLoader();
    void drawSendNGT();
    void drawReceiveNGT();
    void drawStatusBar();
    void drawBox(int y, int x, int h, int w, const char* title);
    void drawDoubleBox(int y, int x, int h, int w);
	    void drawProgressBar(int y, int x, int w, double progress, int color);
	    void centerText(int y, const char* text);
	    void initDefaultState();
	    void loadStartupKeyData();
	    bool saveStartupKeyBackup(std::string& outPath);
	    void setSecureWebDefaults();
	    void applyWebPromptAndContinue();
	    void beginBridgePaste(bool continueAfter, Screen returnScreen);
	    void beginSiteAllowlistPaste(Screen returnScreen);
	    bool loadSiteAllowlistFromConfig(std::string* errorOut);
	    bool saveSiteAllowlistToConfig(std::string* errorOut);
	    std::string naanWebConfigPath() const;
	};

void TUI::Impl::initDefaultState() {
    state.wallet.address = "";
    state.wallet.balance = 0;
    state.wallet.pending = 0;
    state.wallet.totalEarned = 0;
    
    state.network.totalNodes = 0;
    state.network.knowledgeEntries = 0;
    state.network.knowledgeFinalized = 0;
    state.network.knowledgePending = 0;
    state.network.networkSize = 0.0;
    state.network.yourStorage = 0.0;
    state.network.syncProgress = 0.0;
    state.network.synced = false;
    state.network.knownPeers = 0;
    state.network.connectedPeers = 0;
    state.network.bootstrapNodes = 0;
    state.network.dnsSeeds = 0;
    state.network.dnsQueries = 0;
    state.network.peerExchanges = 0;
    state.network.lastPeerRefresh = 0;
    state.network.lastAnnounce = 0;
    
    state.model.name = "";
    state.model.status = "NOT LOADED";
    state.model.progress = 0.0;
    state.model.mode = "PRIVATE";
    state.model.slotsUsed = 0;
    state.model.slotsMax = 0;
    state.model.uptime = 0.0;
    state.model.earningsTodayAtoms = 0;
    state.model.earningsWeekAtoms = 0;
    state.model.earningsTotalAtoms = 0;
    
    state.nodeId = "";
    state.qualityScore = 0;
    state.networkRank = 0;
    state.knowledgeShared = 0;
    state.validations = 0;
    state.miningProgress = 0.0;
    state.isFirstRun = true;
    state.chatHistory.clear();
    state.aiGenerating = false;
    state.modelPath = "";
    state.listeningPort = 0;
    state.networkOnline = false;
    state.forceNewWallet = false;
    state.downloadPath.clear();
    state.downloadPartPath.clear();
    state.sendToAddress.clear();
    state.sendAmount = 0.0;
    state.sendAmountStr.clear();
    state.walletScreen = 0;
    state.knowledgeQuestion.clear();
    state.knowledgeAnswer.clear();
    state.knowledgeSource.clear();
    state.knowledgeField = 0;
    state.codeTitle.clear();
    state.codePatchFile.clear();
    state.codeCitations.clear();
    state.codeField = 0;
    
    state.peers.clear();
    state.recentContributions.clear();
    modelLoader = std::make_unique<synapse::model::ModelLoader>();
    if (modelLoader) {
        modelLoader->onStateChange([this](synapse::model::ModelState s) {
            if (s == synapse::model::ModelState::READY) {
                state.model.status = "ACTIVE";
                state.model.progress = 1.0;
                if (state.model.name.empty()) {
                    state.model.name = modelLoader->getInfo().name;
                }
                return;
            }
            if (s == synapse::model::ModelState::DOWNLOADING) {
                state.model.status = "DOWNLOADING";
                return;
            }
            if (s == synapse::model::ModelState::UNLOADED) {
                if (state.model.status != "DOWNLOADING") {
                    state.model.status = "NOT LOADED";
                    state.model.progress = 0.0;
                }
                return;
            }
        });
        modelLoader->onProgress([this](double p) {
            if (state.model.status == "DOWNLOADING") {
                return;
            }
            if (p < 0.0) p = 0.0;
            if (p > 1.0) p = 1.0;
            state.model.progress = p;
            if (state.model.status != "ACTIVE") {
                state.model.status = "LOADING";
            }
        });
        modelLoader->onError([this](const std::string&) {
            state.model.status = "ERROR";
            state.model.progress = 0.0;
        });
    }
    std::string last = synapse::utils::Config::instance().getString("model.last_path", "");
    if (!last.empty() && std::filesystem::exists(last)) {
        state.modelPath = last;
        state.model.name = std::filesystem::path(last).filename().string();
        state.model.status = "LOADING";
        state.model.progress = 0.0;
        auto pimpl = this;
        std::thread([pimpl, last]() {
            bool ok = false;
            try {
                if (pimpl->modelLoader) ok = pimpl->modelLoader->load(last);
            } catch (...) { ok = false; }
            if (ok) {
                pimpl->state.modelPath = last;
                pimpl->state.model.name = std::filesystem::path(last).filename().string();
                pimpl->state.model.status = "ACTIVE";
                pimpl->state.model.progress = 1.0;
            } else {
                pimpl->state.model.status = "ERROR";
                pimpl->state.model.progress = 0.0;
                synapse::utils::Config::instance().set("model.last_path", "");
                std::string cfgPath = synapse::utils::Config::instance().getDataDir() + "/synapsenet.conf";
                synapse::utils::Config::instance().save(cfgPath);
            }
        }).detach();
    }

    {
        std::lock_guard<std::mutex> lock(state.webMutex);
        state.webInjectEnabled = synapse::utils::Config::instance().getBool("web.inject.enabled", true);
        state.webOnionEnabled = synapse::utils::Config::instance().getBool("web.inject.onion", true);
        state.webTorForClearnet = synapse::utils::Config::instance().getBool("web.inject.tor_clearnet", true);
        state.webSearching = false;
        state.webLastResults = 0;
        state.webLastClearnetResults = 0;
        state.webLastDarknetResults = 0;
        state.webLastError.clear();
        state.startupKeyDataLoaded = false;
        state.startupKeyDataAvailable = false;
        state.startupKeyBackupSaved = false;
        state.startupWalletPath.clear();
        state.startupWalletAddress.clear();
        state.startupPublicKeyHex.clear();
        state.startupPrivateKeyHex.clear();
        state.startupKeyPromptMessage.clear();
        state.startupKeyBackupPath.clear();
        
        // Initialize operation status
        state.currentOperation.operation = "";
        state.currentOperation.status = "";
        state.currentOperation.details = "";
        state.currentOperation.timestamp = 0;
        state.operationHistory.clear();
        state.rewardHistory.clear();
        state.agentEvents.clear();

        state.attachedAgent = AttachedAgentStatusInfo{};
        state.observatoryFeed.clear();
    }

    webPromptDone = synapse::utils::Config::instance().getBool("web.prompt_done", false);

    webSearch = std::make_unique<synapse::web::WebSearch>();
    webDetector = std::make_unique<synapse::web::QueryDetector>();
    webExtractor = std::make_unique<synapse::web::HtmlExtractor>();
    webAi = std::make_unique<synapse::web::AIWrapper>();
    webAi->init();
    webAi->setWebSearch(webSearch.get());
    webAi->setDetector(webDetector.get());
    webAi->setExtractor(webExtractor.get());
    webAi->enableAutoSearch(true);
    webAi->enableContextInjection(true);

    webSearch->onSearchError([this](const std::string& err) {
        std::lock_guard<std::mutex> lock(state.webMutex);
        state.webLastError = err;
    });

    synapse::web::SearchConfig cfg;
    std::string webCfgPath = synapse::utils::Config::instance().getDataDir() + "/web_search.conf";
    synapse::web::loadSearchConfig(webCfgPath, cfg);
    {
        std::lock_guard<std::mutex> lock(state.webMutex);
        cfg.enableClearnet = true;
        cfg.enableDarknet = state.webOnionEnabled;
        cfg.routeClearnetThroughTor = state.webTorForClearnet;
    }
    webSearch->init(cfg);
}

void TUI::Impl::drawBox(int y, int x, int h, int w, const char* title) {
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    for (int i = 1; i < w - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }
    for (int i = 1; i < h - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }
    if (title && strlen(title) > 0) {
        std::string t = "[" + std::string(title) + "]";
        int avail = std::max(0, w - 4);
        int sw = safeScreenWidth(x + 2, avail);
        if (sw > 0) mvaddnstr(y, x + 2, t.c_str(), sw);
    }
}

void TUI::Impl::drawDoubleBox(int y, int x, int h, int w) {
    for (int i = x; i < x + w; i++) {
        mvprintw(y, i, "=");
        mvprintw(y + h - 1, i, "=");
    }
    for (int i = y; i < y + h; i++) {
        mvprintw(i, x, "|");
        mvprintw(i, x + w - 1, "|");
    }
    mvprintw(y, x, "+");
    mvprintw(y, x + w - 1, "+");
    mvprintw(y + h - 1, x, "+");
    mvprintw(y + h - 1, x + w - 1, "+");
}

void TUI::Impl::drawProgressBar(int y, int x, int w, double progress, int color) {
    int filled = static_cast<int>(progress * (w - 2));
    mvaddch(y, x, '[');
    attron(COLOR_PAIR(color));
    for (int i = 0; i < w - 2; i++) {
        if (i < filled) {
            mvprintw(y, x + 1 + i, "#");
        } else {
            mvaddch(y, x + 1 + i, ' ');
        }
    }
    attroff(COLOR_PAIR(color));
    mvaddch(y, x + w - 1, ']');
}

void TUI::Impl::centerText(int y, const char* text) {
    int cols = COLS;
    int len = strlen(text);
    int xpos = (cols - len) / 2;
    if (xpos < 0) xpos = 0;
    int sw = safeScreenWidth(xpos, cols - xpos);
    if (sw > 0) mvaddnstr(y, xpos, text, sw);
}

void TUI::Impl::loadStartupKeyData() {
    if (state.startupKeyDataLoaded) return;

    state.startupKeyDataLoaded = true;
    state.startupKeyDataAvailable = false;
    state.startupKeyBackupSaved = false;
    state.startupWalletPath.clear();
    state.startupWalletAddress.clear();
    state.startupPublicKeyHex.clear();
    state.startupPrivateKeyHex.clear();
    state.startupKeyBackupPath.clear();

    std::string dataDir = synapse::utils::Config::instance().getDataDir();
    if (dataDir.empty()) dataDir = ".";
    state.startupWalletPath = dataDir + "/wallet.dat";

    std::error_code ec;
    if (!std::filesystem::exists(state.startupWalletPath, ec)) {
        state.startupKeyPromptMessage = "No wallet file found yet. Continue to wallet setup.";
        return;
    }

    crypto::Keys keys;
    bool ok = false;
    try {
        ok = keys.load(state.startupWalletPath, "");
    } catch (...) {
        ok = false;
    }

    if (!ok) {
        state.startupKeyPromptMessage = "Wallet file exists but could not be loaded.";
        return;
    }

    const auto publicKey = keys.getPublicKey();
    const auto privateKey = keys.getPrivateKey();
    if (publicKey.empty() || privateKey.size() == 0) {
        state.startupKeyPromptMessage = "Wallet loaded but key material is unavailable.";
        return;
    }

    state.startupWalletAddress = keys.getAddress();
    state.startupPublicKeyHex = synapse::crypto::toHex(publicKey);
    state.startupPrivateKeyHex = synapse::crypto::toHex(privateKey.data(), privateKey.size());
    state.startupKeyDataAvailable = true;
    state.startupKeyPromptMessage = "Save these keys to a local backup file now? [Y/N]";
}

bool TUI::Impl::saveStartupKeyBackup(std::string& outPath) {
    loadStartupKeyData();
    if (!state.startupKeyDataAvailable) return false;

    std::filesystem::path walletPath(state.startupWalletPath);
    std::filesystem::path dataDir = walletPath.parent_path();
    if (dataDir.empty()) dataDir = ".";
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);

    std::time_t now = std::time(nullptr);
    std::tm localTm{};
#ifdef _WIN32
    localtime_s(&localTm, &now);
#else
    localtime_r(&now, &localTm);
#endif
    char localTs[32]{};
    std::strftime(localTs, sizeof(localTs), "%Y%m%d_%H%M%S", &localTm);

    std::tm utcTm{};
#ifdef _WIN32
    gmtime_s(&utcTm, &now);
#else
    gmtime_r(&now, &utcTm);
#endif
    char utcTs[40]{};
    std::strftime(utcTs, sizeof(utcTs), "%Y-%m-%dT%H:%M:%SZ", &utcTm);

    std::filesystem::path backupPath = dataDir / ("wallet_keys_backup_" + std::string(localTs) + ".txt");
    std::ofstream out(backupPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    out << "SynapseNet Wallet Key Backup\n";
    out << "GeneratedAtUTC: " << utcTs << "\n";
    out << "WalletPath: " << state.startupWalletPath << "\n";
    out << "Address: " << state.startupWalletAddress << "\n";
    out << "PublicKeyHex: " << state.startupPublicKeyHex << "\n";
    out << "PrivateKeyHex: " << state.startupPrivateKeyHex << "\n";
    out.close();
    if (!out.good()) return false;

    std::filesystem::permissions(
        backupPath,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        ec
    );

    outPath = backupPath.string();
    state.startupKeyBackupSaved = true;
    state.startupKeyBackupPath = outPath;
    return true;
}

void TUI::Impl::drawBoot() {
    clear();
    int row = 2;

    attron(COLOR_PAIR(5) | A_BOLD);
    for (int i = 0; i < STARTUP_BANNER_COUNT; i++) {
        centerText(row++, STARTUP_BANNER[i]);
    }
    attroff(COLOR_PAIR(5) | A_BOLD);

    row += 2;
    attron(COLOR_PAIR(6));
    centerText(row++, "Decentralized Intelligence Network v0.1");
    attroff(COLOR_PAIR(6));

    row += 1;
    int boxW = std::min(76, COLS - 4);
    int boxX = (COLS - boxW) / 2;
    int boxH = 9;
    drawBox(row, boxX, boxH, boxW, "Did you know?");
    int iy = row + 2;
    int ix = boxX + 3;
    attron(COLOR_PAIR(7));
    mvprintw(iy++, ix, "AI Query uses local GGUF files from ~/.synapsenet/models or ./models.");
    mvprintw(iy++, ix, "Routing in AI Query is read-only and follows node policy.");
    mvprintw(iy++, ix, "Ctrl+W clears chat history, Ctrl+X stops generation.");
    mvprintw(iy++, ix, "PgUp/PgDn scroll chat history.");
    attroff(COLOR_PAIR(7));

    int footerY = std::min(LINES - 3, row + boxH + 2);
    attron(A_BLINK);
    centerText(footerY, "Press [SPACE] to continue");
    attroff(A_BLINK);

	    drawStatusBar();
	    ::refresh();
	}

void TUI::Impl::drawKeyBackup() {
    loadStartupKeyData();

    clear();
    int boxW = std::min(100, COLS - 4);
    int boxH = std::min(26, LINES - 4);
    int boxX = (COLS - boxW) / 2;
    int boxY = (LINES - boxH) / 2;
    if (boxY < 1) boxY = 1;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(boxY - 1 < 0 ? 0 : boxY - 1, "WALLET KEY BACKUP");
    attroff(COLOR_PAIR(4) | A_BOLD);

    drawBox(boxY, boxX, boxH, boxW, "Startup security check");

    int y = boxY + 2;
    int innerW = boxW - 6;
    printClippedLine(y++, boxX + 3, innerW, "Review your wallet key material before node startup.");
    printClippedLine(y++, boxX + 3, innerW, "Keep backups offline and private.");
    y++;

    if (state.startupKeyDataAvailable) {
        printClippedLine(y++, boxX + 3, innerW, "Wallet file:");
        printClippedLine(y++, boxX + 3, innerW, state.startupWalletPath);
        y++;
        printClippedLine(y++, boxX + 3, innerW, "Address:");
        printClippedLine(y++, boxX + 3, innerW, state.startupWalletAddress.empty() ? "unavailable" : state.startupWalletAddress);
        y++;
        printClippedLine(y++, boxX + 3, innerW, "Public key (hex):");
        printClippedLine(y++, boxX + 3, innerW, state.startupPublicKeyHex);
        y++;
        printClippedLine(y++, boxX + 3, innerW, "Private key (hex):");
        printClippedLine(y++, boxX + 3, innerW, state.startupPrivateKeyHex);
        y += 2;
        if (state.startupKeyBackupSaved) {
            attron(COLOR_PAIR(1));
            printClippedLine(y++, boxX + 3, innerW, "Backup saved:");
            printClippedLine(y++, boxX + 3, innerW, state.startupKeyBackupPath);
            attroff(COLOR_PAIR(1));
            y++;
            attron(A_BLINK);
            printClippedLine(y, boxX + 3, innerW, "Press [ENTER] to continue");
            attroff(A_BLINK);
        } else {
            attron(COLOR_PAIR(2));
            printClippedLine(y++, boxX + 3, innerW, state.startupKeyPromptMessage);
            attroff(COLOR_PAIR(2));
            printClippedLine(y, boxX + 3, innerW, "[Y] Save to .txt and continue   [N] Continue without saving");
        }
    } else {
        attron(COLOR_PAIR(2));
        printClippedLine(y++, boxX + 3, innerW, state.startupKeyPromptMessage);
        attroff(COLOR_PAIR(2));
        y++;
        attron(A_BLINK);
        printClippedLine(y, boxX + 3, innerW, "Press [ENTER] to continue");
        attroff(A_BLINK);
    }

    drawStatusBar();
    ::refresh();
}


void TUI::Impl::drawInit() {
    clear();
    int row = 3;
    
    attron(COLOR_PAIR(1) | A_BOLD);
    centerText(row++, "Initializing SynapseNet Protocol...");
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    row += 2;
    double progress = (initStep + 1) / 5.0;
    int barWidth = 42;
    int startX = (COLS - barWidth) / 2;
    
    mvaddch(row, startX, '[');
    attron(COLOR_PAIR(1));
    for (int i = 0; i < 40; i++) {
        if (i < static_cast<int>(progress * 40)) {
            mvprintw(row, startX + 1 + i, "#");
        } else {
            mvaddch(row, startX + 1 + i, ' ');
        }
    }
    attroff(COLOR_PAIR(1));
    mvaddch(row, startX + 41, ']');
    mvprintw(row, startX + 44, "%d%%", static_cast<int>(progress * 100));
    
    row += 3;
    
    const char* steps[] = {
        "Loading configuration...",
        "Initializing quantum-resistant cryptography...",
        "Loading wallet...",
        "Starting local AI engine...",
        "Connecting to Knowledge Network..."
    };
    
    const char* details[] = {
        "Reading synapsenet.conf",
        "CRYSTALS-Dilithium + Kyber",
        "Wallet: %s",
        "Local model runtime ready",
        "Binding to port %d"
    };
    
    for (int i = 0; i < 5; i++) {
        if (i < initStep) {
            attron(COLOR_PAIR(1));
            mvprintw(row, startX, "[OK]");
            attroff(COLOR_PAIR(1));
            mvprintw(row, startX + 5, " %s", steps[i]);
            if (i == 4 && state.listeningPort > 0) {
                mvprintw(row, startX + 30, "(port %d)", state.listeningPort);
            }
        } else if (i == initStep) {
            attron(COLOR_PAIR(2));
            mvprintw(row, startX, "[..]");
            attroff(COLOR_PAIR(2));
            mvprintw(row, startX + 5, " %s", steps[i]);
            
            static int dots = 0;
            dots = (dots + 1) % 4;
            for (int d = 0; d < dots; d++) {
                mvprintw(row, startX + 5 + strlen(steps[i]) + d, ".");
            }
        } else {
            attron(COLOR_PAIR(7));
            mvprintw(row, startX, "[  ]");
            attroff(COLOR_PAIR(7));
            mvprintw(row, startX + 5, " %s", steps[i]);
        }
        row++;
    }
    
    row += 2;
    if (initStep < 5) {
        attron(COLOR_PAIR(7));
        if (initStep == 2) {
            std::string shortAddr = state.wallet.address.empty() ? "not found" :
                state.wallet.address.substr(0, 10) + "..." + state.wallet.address.substr(state.wallet.address.length() - 4);
            mvprintw(row, startX, details[initStep], shortAddr.c_str());
        } else if (initStep == 4 && state.listeningPort > 0) {
            mvprintw(row, startX, details[initStep], state.listeningPort);
        } else if (initStep == 4) {
            mvprintw(row, startX, details[initStep], 0);
        } else {
            mvprintw(row, startX, "%s", details[initStep]);
        }
        attroff(COLOR_PAIR(7));
    }
    
    ::refresh();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    if (initStep < 4) {
        initStep++;
    } else {
        screen = Screen::NETWORK_DISCOVERY;
    }
}

void TUI::Impl::drawNetworkDiscovery() {
    clear();
    int row = 2;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "Network Status");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row += 1;
    drawBox(row, boxX, 20, boxW, "");
    
    int innerRow = row + 2;
    
    size_t realPeers = state.peers.size();
    
    if (state.listeningPort > 0) {
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(innerRow++, boxX + 3, "NODE STATUS: ONLINE");
        attroff(COLOR_PAIR(1) | A_BOLD);
        innerRow++;
        attron(COLOR_PAIR(1));
        mvprintw(innerRow++, boxX + 3, "Listening:    port %d", state.listeningPort);
        mvprintw(innerRow++, boxX + 3, "Protocol:     SynapseNet v0.1");
        mvprintw(innerRow++, boxX + 3, "Encryption:   Hybrid classical + PQC (runtime-dependent)");
        attroff(COLOR_PAIR(1));
    } else {
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(innerRow++, boxX + 3, "NODE STATUS: OFFLINE");
        attroff(COLOR_PAIR(2) | A_BOLD);
        innerRow++;
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "Could not bind to any port");
        mvprintw(innerRow++, boxX + 3, "Running in local-only mode");
        attroff(COLOR_PAIR(2));
    }
    
    innerRow++;
    mvprintw(innerRow++, boxX + 3, "----------------------------------------");
    innerRow++;
    
    if (realPeers == 0) {
        mvprintw(innerRow++, boxX + 3, "Connected Peers: 0");
        innerRow++;
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "GENESIS MODE - First node on network");
        mvprintw(innerRow++, boxX + 3, "Waiting for other nodes to connect...");
        innerRow++;
        mvprintw(innerRow++, boxX + 3, "Bootstrap: %lu nodes   DNS: %lu seeds",
                 state.network.bootstrapNodes, state.network.dnsSeeds);
        mvprintw(innerRow++, boxX + 3, "Known peers: %lu   DNS queries: %lu",
                 state.network.knownPeers, state.network.dnsQueries);
        mvprintw(innerRow++, boxX + 3, "Peer exchange: %lu (last %lus ago)",
                 state.network.peerExchanges,
                 (state.network.lastPeerRefresh > 0) ? (static_cast<uint64_t>(std::time(nullptr)) - state.network.lastPeerRefresh) : 0ULL);
        mvprintw(innerRow++, boxX + 3, "PoE mode: %s   validators: %lu   self-bootstrap: %s",
                 state.network.poeBootstrapMode.empty() ? "disabled" : state.network.poeBootstrapMode.c_str(),
                 state.network.poeValidatorCount,
                 state.network.poeSelfBootstrapActive ? "on" : "off");
        attroff(COLOR_PAIR(2));
    } else {
        mvprintw(innerRow++, boxX + 3, "Connected Peers: %zu", realPeers);
        innerRow++;
        for (size_t i = 0; i < state.peers.size() && i < 5; i++) {
            attron(COLOR_PAIR(1));
            printClippedLine(innerRow++, boxX + 5, boxW - 6, "* " + truncEnd(state.peers[i].id, boxW - 8));
            attroff(COLOR_PAIR(1));
        }
        mvprintw(innerRow++, boxX + 3, "PoE mode: %s   validators: %lu   self-bootstrap: %s",
                 state.network.poeBootstrapMode.empty() ? "disabled" : state.network.poeBootstrapMode.c_str(),
                 state.network.poeValidatorCount,
                 state.network.poeSelfBootstrapActive ? "on" : "off");
    }
    
    row += 22;
    
    if (state.listeningPort > 0) {
        attron(COLOR_PAIR(1));
        centerText(row, "Network: READY");
        attroff(COLOR_PAIR(1));
    } else {
        attron(COLOR_PAIR(2));
        centerText(row, "Network: OFFLINE");
        attroff(COLOR_PAIR(2));
    }
    
    ::refresh();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    static int waitCount = 0;
    waitCount++;
    if (waitCount > 15) {
        waitCount = 0;
        screen = Screen::SYNCING;
        state.network.syncProgress = 0.0;
    }
}

void TUI::Impl::drawSyncing() {
    clear();
    int row = 2;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "Knowledge Network");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row += 1;
    drawBox(row, boxX, 14, boxW, "");
    
    int innerRow = row + 2;
    
    size_t realPeers = state.peers.size();
    
    if (realPeers == 0) {
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(innerRow++, boxX + 3, "GENESIS NODE");
        attroff(COLOR_PAIR(1) | A_BOLD);
        innerRow++;
        
        mvprintw(innerRow++, boxX + 3, "You are starting a new network.");
        mvprintw(innerRow++, boxX + 3, "");
        mvprintw(innerRow++, boxX + 3, "Knowledge Chain:  Empty (genesis block)");
        mvprintw(innerRow++, boxX + 3, "Network Size:     0 bytes");
        mvprintw(innerRow++, boxX + 3, "Your Storage:     0 bytes");
        innerRow++;
        
        attron(COLOR_PAIR(1));
        mvprintw(innerRow++, boxX + 3, "Status: Ready to accept connections");
        attroff(COLOR_PAIR(1));
        
        state.network.syncProgress = 1.0;
        state.network.synced = true;
    } else {
        attron(A_BOLD);
        mvprintw(innerRow++, boxX + 3, "SYNCHRONIZING");
        attroff(A_BOLD);
        innerRow++;
        
        mvprintw(innerRow++, boxX + 3, "Network Size:     %.1f GB", state.network.networkSize);
        mvprintw(innerRow++, boxX + 3, "Downloaded:       %.1f GB", state.network.networkSize * state.network.syncProgress);
        mvprintw(innerRow++, boxX + 3, "Entries:          %lu", state.network.knowledgeEntries);
        innerRow++;
        
        mvprintw(innerRow, boxX + 3, "Progress: ");
        int barX = boxX + 13;
        mvaddch(innerRow, barX, '[');
        attron(COLOR_PAIR(1));
        int filled = static_cast<int>(state.network.syncProgress * 40);
        for (int i = 0; i < 40; i++) {
            mvprintw(innerRow, barX + 1 + i, i < filled ? "#" : " ");
        }
        attroff(COLOR_PAIR(1));
        mvaddch(innerRow, barX + 41, ']');
        mvprintw(innerRow++, barX + 43, "%d%%", static_cast<int>(state.network.syncProgress * 100));
    }
    
    row += 16;
    
    if (state.network.synced) {
        attron(COLOR_PAIR(1));
        centerText(row, "Sync: COMPLETE");
        attroff(COLOR_PAIR(1));
    }
    
    ::refresh();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    static int syncWait = 0;
    syncWait++;
    
    if (realPeers > 0 && state.network.syncProgress < 1.0) {
        state.network.syncProgress += 0.05;
    }
    
    if (syncWait > 10) {
        syncWait = 0;
        state.network.syncProgress = 1.0;
        state.network.synced = true;
        if (!webPromptDone) {
            webPromptStep = 0;
            const std::string torMode = synapse::utils::Config::instance().getString("agent.tor.mode", "auto");
            const int torSocksPort = synapse::utils::Config::instance().getInt("agent.tor.socks_port", 9050);
            webPromptBridgeMode = (torMode == "external" && torSocksPort == 9150) ? 2 : 0;
            screen = Screen::WEB_PROMPT;
        } else {
            screen = state.isFirstRun ? Screen::WELCOME : Screen::DASHBOARD;
        }
    }
}

void TUI::Impl::drawWebPrompt() {
    clear();

    int boxW = 76;
    int boxH = 23;
    int boxX = (COLS - boxW) / 2;
    int boxY = (LINES - boxH) / 2;
    if (boxY < 1) boxY = 1;
    const bool torOnlyMode = synapse::utils::Config::instance().getBool("agent.tor.required", true);

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(boxY - 1 < 0 ? 0 : boxY - 1, "PRIVACY & WEB SEARCH");
    attroff(COLOR_PAIR(4) | A_BOLD);

    drawBox(boxY, boxX, boxH, boxW, "Startup choice");

    int y = boxY + 2;
    int innerW = boxW - 6;

    printClippedLine(y++, boxX + 3, innerW, "SynapseNet can search the web to help your local AI (chat UX only).");
    if (torOnlyMode) {
        printClippedLine(y++, boxX + 3, innerW, "Tor-only mode is enabled: clearnet web traffic is routed through Tor.");
    } else {
        printClippedLine(y++, boxX + 3, innerW, "This is not consensus: it only affects your chat UX.");
    }
    y++;

    auto onOff = [](bool v) { return v ? "ON" : "OFF"; };
    auto bridgeModeLabel = [&](int mode) {
        if (mode == 1) return std::string("obfs4 via external Tor (9150)");
        if (mode == 2) return std::string("external Tor already running (9150, shared)");
        return std::string("no bridges / default Tor path");
    };
    {
        std::lock_guard<std::mutex> lock(state.webMutex);
        std::string s1 = std::string("Web injection: ") + onOff(state.webInjectEnabled);
        std::string s2 = std::string("Onion sources: ") + onOff(state.webOnionEnabled);
        std::string s3 = torOnlyMode
            ? std::string("Tor for clearnet: FORCED ON (Tor-only mode)")
            : std::string("Tor for clearnet: ") + onOff(state.webTorForClearnet);
        printClippedLine(y++, boxX + 3, innerW, s1);
        printClippedLine(y++, boxX + 3, innerW, s2);
        printClippedLine(y++, boxX + 3, innerW, s3);
    }
    printClippedLine(y++, boxX + 3, innerW, "Bridge mode: " + bridgeModeLabel(webPromptBridgeMode));
    y++;

    std::string q;
    if (webPromptStep == 0) q = "Enable AI web injection? [Y/N]";
    else if (webPromptStep == 1) q = "Include onion sources (.onion) in search? [Y/N]";
    else if (webPromptStep == 2) {
        if (torOnlyMode) q = "Route clearnet web requests through Tor? [Y] (required)";
        else q = "Route clearnet web requests through Tor? [Y/N]";
    } else {
        q = "Bridge mode? [1] None  [2] obfs4 (ext 9150)  [3] External Tor (9150)";
    }

    attron(COLOR_PAIR(1) | A_BOLD);
    printClippedLine(y++, boxX + 3, innerW, q);
    attroff(COLOR_PAIR(1) | A_BOLD);

    y++;
    attron(COLOR_PAIR(2));
    if (webPromptStep == 3) {
        printClippedLine(y++, boxX + 3, innerW, "Bridge mode default is [1] None. [2]/[3] configure SynapseNet to use external Tor on 9150.");
        printClippedLine(y++, boxX + 3, innerW, "Tip: [3] lets Tor Browser + SynapseNet + curl share one Tor on SOCKS 127.0.0.1:9150.");
        printClippedLine(y++, boxX + 3, innerW, "Tip: press [P] to paste obfs4 bridges and save them to your data dir (recommended for [2]).");
    } else {
        printClippedLine(y++, boxX + 3, innerW, "Default is YES (Tor-first). You can change later in synapsenet.conf.");
    }
    attroff(COLOR_PAIR(2));
    attron(COLOR_PAIR(4));
    if (webPromptStep == 3) {
        printClippedLine(y++, boxX + 3, innerW, "[Enter] accepts the current bridge mode selection for this step.");
    } else {
        printClippedLine(y++, boxX + 3, innerW, "[Enter] accepts the secure default for this step.");
    }
    attroff(COLOR_PAIR(4));

    int controlsY = boxY + boxH - 2;
    if (webPromptStep == 3) {
        printClippedLine(controlsY, boxX + 3, innerW, "[1] None  [2] obfs4 ext9150  [3] Shared ext9150  [P] Paste bridges  [Enter] Continue  [Esc] Use defaults");
    } else {
        printClippedLine(controlsY, boxX + 3, innerW, "[Y] Yes  [N] No  [T] Tor-only defaults  [Esc] Use defaults");
    }

    ::refresh();
}

void TUI::Impl::setSecureWebDefaults() {
    std::lock_guard<std::mutex> lock(state.webMutex);
    state.webInjectEnabled = true;
    state.webOnionEnabled = true;
    state.webTorForClearnet = true;
}

void TUI::Impl::applyWebPromptAndContinue() {
    const bool torOnlyMode = synapse::utils::Config::instance().getBool("agent.tor.required", true);
    {
        std::lock_guard<std::mutex> lock(state.webMutex);
        if (torOnlyMode) {
            state.webTorForClearnet = true;
        }
        auto& cfg = synapse::utils::Config::instance();
        cfg.set("web.inject.enabled", state.webInjectEnabled);
        cfg.set("web.inject.onion", state.webOnionEnabled);
        cfg.set("web.inject.tor_clearnet", state.webTorForClearnet);
        cfg.set("agent.tor.required", true);
        if (webPromptBridgeMode == 0) {
            cfg.set("agent.tor.mode", "auto");
            cfg.set("agent.tor.socks_host", "127.0.0.1");
            cfg.set("agent.tor.socks_port", 9050);
            cfg.set("tor.socks.host", "127.0.0.1");
            cfg.set("tor.socks.port", 9050);
            cfg.set("tor.control.port", 9051);
        } else {
            cfg.set("agent.tor.mode", "external");
            cfg.set("agent.tor.socks_host", "127.0.0.1");
            cfg.set("agent.tor.socks_port", 9150);
            cfg.set("tor.socks.host", "127.0.0.1");
            cfg.set("tor.socks.port", 9150);
            cfg.set("tor.control.port", 9151);
        }
        cfg.set("agent.routing.allow_clearnet_fallback", false);
        cfg.set("agent.routing.allow_p2p_clearnet_fallback", false);
        cfg.set("web.prompt_done", true);
    }

    std::string cfgPath = synapse::utils::Config::instance().getDataDir() + "/synapsenet.conf";
    synapse::utils::Config::instance().save(cfgPath);

    if (webSearch) {
        synapse::web::SearchConfig cfg = webSearch->getConfig();
        {
            std::lock_guard<std::mutex> lock(state.webMutex);
            cfg.enableClearnet = state.webInjectEnabled;
            cfg.enableDarknet = state.webOnionEnabled;
            cfg.routeClearnetThroughTor = state.webTorForClearnet;
        }
        webSearch->setConfig(cfg);
    }

    webPromptDone = true;
    screen = state.isFirstRun ? Screen::WELCOME : Screen::DASHBOARD;
}

void TUI::Impl::beginBridgePaste(bool continueAfter, Screen returnScreen) {
    bridgePasteContinueAfter = continueAfter;
    bridgePasteReturnScreen = returnScreen;
    bridgePasteInvalid = 0;
    bridgePasteLines.clear();
    bridgePasteLine.clear();
    bridgePasteStatus.clear();
    screen = Screen::BRIDGE_PASTE;
}

std::string TUI::Impl::naanWebConfigPath() const {
    return synapse::utils::Config::instance().getDataDir() + "/naan_agent_web.conf";
}

bool TUI::Impl::loadSiteAllowlistFromConfig(std::string* errorOut) {
    const std::string cfgPath = naanWebConfigPath();
    if (!ensureNaanWebConfigFileExists(cfgPath)) {
        if (errorOut) *errorOut = "could not create " + cfgPath;
        return false;
    }

    synapse::web::SearchConfig cfg = synapse::web::defaultSearchConfig();
    synapse::web::SearchConfigValidationStats validation{};
    (void)synapse::web::loadSearchConfigOverlay(cfgPath, cfg, &validation);
    synapse::web::sanitizeSearchConfig(cfg);

    siteAllowlistClearnet = cfg.clearnetSiteAllowlist;
    siteAllowlistOnion = cfg.onionSiteAllowlist;
    dedupeStringsInPlace(siteAllowlistClearnet);
    dedupeStringsInPlace(siteAllowlistOnion);

    if (errorOut) {
        if (validation.invalidLines > 0 || validation.unknownKeys > 0) {
            *errorOut = "loaded with warnings: invalid=" + std::to_string(validation.invalidLines) +
                        " unknown=" + std::to_string(validation.unknownKeys);
        } else {
            *errorOut = "";
        }
    }
    return true;
}

bool TUI::Impl::saveSiteAllowlistToConfig(std::string* errorOut) {
    const std::string cfgPath = naanWebConfigPath();
    if (!ensureNaanWebConfigFileExists(cfgPath)) {
        if (errorOut) *errorOut = "could not create " + cfgPath;
        return false;
    }

    dedupeStringsInPlace(siteAllowlistClearnet);
    dedupeStringsInPlace(siteAllowlistOnion);

    std::ifstream in(cfgPath);
    if (!in.is_open()) {
        if (errorOut) *errorOut = "could not read " + cfgPath;
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }

    auto isClearnetKey = [](const std::string& key) {
        return key == "clearnet_site_allowlist" || key == "clearnet_sites" || key == "naan_clearnet_sites";
    };
    auto isOnionKey = [](const std::string& key) {
        return key == "onion_site_allowlist" || key == "onion_sites" || key == "naan_onion_sites";
    };

    const std::string clearnetLine = "clearnet_site_allowlist=" + joinCsvList(siteAllowlistClearnet);
    const std::string onionLine = "onion_site_allowlist=" + joinCsvList(siteAllowlistOnion);

    bool wroteClearnet = false;
    bool wroteOnion = false;
    std::vector<std::string> outLines;
    outLines.reserve(lines.size() + 2);

    for (const auto& raw : lines) {
        const std::string trimmed = trimAsciiCopy(raw);
        if (trimmed.empty() || trimmed[0] == '#') {
            outLines.push_back(raw);
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            outLines.push_back(raw);
            continue;
        }
        const std::string key = toLowerAsciiCopy(trimAsciiCopy(trimmed.substr(0, eq)));
        if (isClearnetKey(key)) {
            if (!wroteClearnet) {
                outLines.push_back(clearnetLine);
                wroteClearnet = true;
            }
            continue;
        }
        if (isOnionKey(key)) {
            if (!wroteOnion) {
                outLines.push_back(onionLine);
                wroteOnion = true;
            }
            continue;
        }
        outLines.push_back(raw);
    }

    if (!wroteClearnet) outLines.push_back(clearnetLine);
    if (!wroteOnion) outLines.push_back(onionLine);

    std::ofstream out(cfgPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        if (errorOut) *errorOut = "could not write " + cfgPath;
        return false;
    }
    for (const auto& row : outLines) {
        out << row << "\n";
    }
    if (!out.good()) {
        if (errorOut) *errorOut = "write failed for " + cfgPath;
        return false;
    }

    if (errorOut) *errorOut = "";
    return true;
}

void TUI::Impl::beginSiteAllowlistPaste(Screen returnScreen) {
    siteAllowlistReturnScreen = returnScreen;
    siteAllowlistTarget = 0;
    siteAllowlistInvalid = 0;
    siteAllowlistLine.clear();
    siteAllowlistStatus.clear();

    std::string loadMessage;
    if (!loadSiteAllowlistFromConfig(&loadMessage)) {
        siteAllowlistStatus = "Load failed: " + loadMessage;
    } else if (!loadMessage.empty()) {
        siteAllowlistStatus = loadMessage;
    } else {
        siteAllowlistStatus = "Loaded allowlist from config.";
    }

    screen = Screen::SITE_ALLOWLIST_PASTE;
}

void TUI::Impl::drawBridgePaste() {
    clear();

    int boxW = 76;
    int boxH = 23;
    int boxX = (COLS - boxW) / 2;
    int boxY = (LINES - boxH) / 2;
    if (boxY < 1) boxY = 1;

    const int innerW = boxW - 6;
    const std::string bridgeFilePath = synapse::utils::Config::instance().getDataDir() + "/tor/bridges.obfs4.txt";

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(boxY - 1 < 0 ? 0 : boxY - 1, "OBFS4 BRIDGES");
    attroff(COLOR_PAIR(4) | A_BOLD);

    drawBox(boxY, boxX, boxH, boxW, "Paste bridges");

    int y = boxY + 2;
    printClippedLine(y++, boxX + 3, innerW, "Get fresh obfs4 bridges from:");
    printClippedLine(y++, boxX + 3, innerW, "https://bridges.torproject.org/bridges?transport=obfs4");
    y++;
    printClippedLine(y++, boxX + 3, innerW, "Paste one bridge per line: 'obfs4 ...' or 'Bridge obfs4 ...'");
    printClippedLine(y++, boxX + 3, innerW, "Press [Enter] to accept a line. Press [Enter] on an empty line to finish.");
    if (bridgePasteContinueAfter) {
        printClippedLine(y++, boxX + 3, innerW, "Press [Esc] to skip (no bridges saved) and continue.");
    } else {
        printClippedLine(y++, boxX + 3, innerW, "Press [Esc] to cancel and return.");
    }
    y++;
    printClippedLine(y++, boxX + 3, innerW, "Will write to:");
    printClippedLine(y++, boxX + 3, innerW, truncStart(bridgeFilePath, innerW));
    y++;

    attron(COLOR_PAIR(2));
    printClippedLine(y++, boxX + 3, innerW,
                     "Accepted lines: " + std::to_string(bridgePasteLines.size()) +
                         "    Invalid lines: " + std::to_string(bridgePasteInvalid));
    if (!bridgePasteStatus.empty()) {
        printClippedLine(y++, boxX + 3, innerW, truncEnd(bridgePasteStatus, innerW));
    }
    attroff(COLOR_PAIR(2));

    y += 2;
    attron(COLOR_PAIR(1) | A_BOLD);
    printClippedLine(y++, boxX + 3, innerW, "Current line:");
    attroff(COLOR_PAIR(1) | A_BOLD);
    printClippedLine(y++, boxX + 3, innerW, truncStart(bridgePasteLine, innerW));

    int controlsY = boxY + boxH - 2;
    attron(COLOR_PAIR(4));
    printClippedLine(controlsY, boxX + 3, innerW, "[Enter] Add line  [Enter empty] Finish  [Backspace] Edit  [Esc] Exit");
    attroff(COLOR_PAIR(4));

    ::refresh();
}

void TUI::Impl::drawSiteAllowlistPaste() {
    clear();

    int boxW = 76;
    int boxH = 23;
    int boxX = (COLS - boxW) / 2;
    int boxY = (LINES - boxH) / 2;
    if (boxY < 1) boxY = 1;

    const int innerW = boxW - 6;
    const std::string cfgPath = naanWebConfigPath();
    const std::string activeLabel = (siteAllowlistTarget == 0) ? "CLEARNET" : "ONION";
    const auto& activeList = (siteAllowlistTarget == 0) ? siteAllowlistClearnet : siteAllowlistOnion;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(boxY - 1 < 0 ? 0 : boxY - 1, "NAAN SITE ALLOWLIST");
    attroff(COLOR_PAIR(4) | A_BOLD);

    drawBox(boxY, boxX, boxH, boxW, "Clearnet + onion site rules");

    int y = boxY + 2;
    printClippedLine(y++, boxX + 3, innerW, "Set which hosts NAAN is allowed to fetch.");
    printClippedLine(y++, boxX + 3, innerW, "Config path: " + truncStart(cfgPath, innerW - 13));
    printClippedLine(y++, boxX + 3, innerW, "Keys: clearnet_site_allowlist=...  onion_site_allowlist=...");
    y++;
    printClippedLine(y++, boxX + 3, innerW, "Examples: nasa.gov, *.nasa.gov, host:science.nasa.gov, suffix:nasa.gov");
    printClippedLine(y++, boxX + 3, innerW, "Onion examples: examplexxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.onion");
    printClippedLine(y++, boxX + 3, innerW, "Prefix rule with '-' to remove it. Enter empty line to save and exit.");
    y++;

    attron(COLOR_PAIR(1) | A_BOLD);
    printClippedLine(y++, boxX + 3, innerW,
                     "Active list: " + activeLabel + "  (entries: " + std::to_string(activeList.size()) + ")");
    attroff(COLOR_PAIR(1) | A_BOLD);
    printClippedLine(y++, boxX + 3, innerW,
                     "Clearnet entries: " + std::to_string(siteAllowlistClearnet.size()) +
                     "  Onion entries: " + std::to_string(siteAllowlistOnion.size()) +
                     "  Invalid: " + std::to_string(siteAllowlistInvalid));

    if (!activeList.empty()) {
        printClippedLine(y++, boxX + 3, innerW,
                         "Recent " + toLowerAsciiCopy(activeLabel) + " rules:");
        const size_t preview = std::min<size_t>(2, activeList.size());
        for (size_t i = 0; i < preview; ++i) {
            const auto& rule = activeList[activeList.size() - preview + i];
            printClippedLine(y++, boxX + 5, innerW - 2, "- " + truncEnd(rule, innerW - 4));
        }
    } else {
        printClippedLine(y++, boxX + 3, innerW, "No rules in the active list yet.");
    }

    if (!siteAllowlistStatus.empty()) {
        attron(COLOR_PAIR(2));
        printClippedLine(y++, boxX + 3, innerW, truncEnd(siteAllowlistStatus, innerW));
        attroff(COLOR_PAIR(2));
    }

    attron(COLOR_PAIR(3));
    printClippedLine(y++, boxX + 3, innerW,
                     "Security warning: you are responsible for added sites and must evaluate risk.");
    printClippedLine(y++, boxX + 3, innerW,
                     "AI/IDE tools help triage logs/events but do NOT replace AV/EDR hard security controls.");
    attroff(COLOR_PAIR(3));

    attron(COLOR_PAIR(1) | A_BOLD);
    printClippedLine(y++, boxX + 3, innerW, "Current line:");
    attroff(COLOR_PAIR(1) | A_BOLD);
    printClippedLine(y++, boxX + 3, innerW, truncStart(siteAllowlistLine, innerW));

    int controlsY = boxY + boxH - 2;
    attron(COLOR_PAIR(4));
    printClippedLine(controlsY, boxX + 3, innerW,
                     "[C] Clearnet  [O] Onion  [R] Reload  [Enter] Add/Save  [Backspace] Edit  [Esc/B] Cancel");
    attroff(COLOR_PAIR(4));

    ::refresh();
}

void TUI::Impl::drawWelcome() {
    clear();
    int boxW = 76;
    int boxH = 20;
    int boxX = (COLS - boxW) / 2;
    int boxY = (LINES - boxH) / 2;
    
    attron(COLOR_PAIR(4));
    for (int i = boxX; i < boxX + boxW; i++) {
        mvprintw(boxY, i, "=");
        mvprintw(boxY + boxH - 1, i, "=");
    }
    for (int i = boxY; i < boxY + boxH; i++) {
        mvprintw(i, boxX, "|");
        mvprintw(i, boxX + boxW - 1, "|");
    }
    mvprintw(boxY, boxX, "+");
    mvprintw(boxY, boxX + boxW - 1, "+");
    mvprintw(boxY + boxH - 1, boxX, "+");
    mvprintw(boxY + boxH - 1, boxX + boxW - 1, "+");
    attroff(COLOR_PAIR(4));
    
    int row = boxY + 2;
    attron(COLOR_PAIR(1) | A_BOLD);
    centerText(row++, "WELCOME TO SYNAPSENET");
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    row += 1;
    if (state.isFirstRun) {
        centerText(row++, "First run detected. Create or import a wallet.");
    } else {
        centerText(row++, "Wallet found. You can continue or create a new one.");
    }
    row += 1;
    
    int menuX = boxX + 5;
    drawBox(row, menuX, 10, boxW - 10, "");
    
    int menuRow = row + 2;
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(menuRow++, menuX + 3, "[1] Create New Wallet");
    attroff(COLOR_PAIR(4) | A_BOLD);
    mvprintw(menuRow++, menuX + 3, "    Generate a new quantum-safe wallet and seed phrase");
    menuRow++;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(menuRow++, menuX + 3, "[2] Import Existing Wallet");
    attroff(COLOR_PAIR(4) | A_BOLD);
    mvprintw(menuRow++, menuX + 3, "    Restore from seed phrase or private key");
    menuRow++;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(menuRow++, menuX + 3, "[3] Continue");
    attroff(COLOR_PAIR(4) | A_BOLD);
    mvprintw(menuRow++, menuX + 3, "    Go to dashboard");
    
    row = boxY + boxH - 3;
    mvprintw(row, menuX + 3, "Select option [1-3]: _");
    
    ::refresh();
}

void TUI::Impl::drawWalletCreate() {
    clear();
    int row = 2;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    
    if (state.generatedSeedWords.empty()) {
        std::random_device rd;
        
        auto now = std::chrono::high_resolution_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        
        std::array<uint32_t, 16> seed_data;
        for (size_t i = 0; i < seed_data.size(); i++) {
            seed_data[i] = rd() ^ static_cast<uint32_t>(nanos >> (i * 2));
        }
        seed_data[0] ^= static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        seed_data[1] ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&state));
        
        std::seed_seq seq(seed_data.begin(), seed_data.end());
        std::mt19937_64 gen(seq);
        std::uniform_int_distribution<> dis(0, BIP39_WORDLIST_SIZE - 1);
        
        state.generatedSeedWords.clear();
        for (int i = 0; i < 24; i++) {
            state.generatedSeedWords.push_back(BIP39_WORDLIST[dis(gen)]);
        }
    }
    
    attron(COLOR_PAIR(1) | A_BOLD);
    centerText(row++, "CREATING NEW WALLET");
    mvprintw(row++, boxX, "===================");
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    row += 1;
    centerText(row++, "Generating quantum-resistant keys...");
    row += 1;
    
    drawBox(row, boxX, 18, boxW, "");
    
    int innerRow = row + 2;
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(innerRow++, boxX + 3, "!!! IMPORTANT: SAVE YOUR SEED PHRASE !!!");
    attroff(COLOR_PAIR(3) | A_BOLD);
    innerRow++;
    
    mvprintw(innerRow++, boxX + 3, "Your 24-word seed phrase is the ONLY way to recover your wallet.");
    mvprintw(innerRow++, boxX + 3, "Write it down on paper. Do NOT store digitally. Do NOT share.");
    innerRow++;
    
    int seedBoxX = boxX + 5;
    drawBox(innerRow, seedBoxX, 8, boxW - 10, "");
    
    int seedRow = innerRow + 2;
    attron(COLOR_PAIR(4));
    for (int i = 0; i < 6; i++) {
        mvprintw(seedRow, seedBoxX + 3, "%2d. %-12s", i + 1, state.generatedSeedWords[i].c_str());
        mvprintw(seedRow, seedBoxX + 19, "%2d. %-12s", i + 7, state.generatedSeedWords[i + 6].c_str());
        mvprintw(seedRow, seedBoxX + 35, "%2d. %-12s", i + 13, state.generatedSeedWords[i + 12].c_str());
        mvprintw(seedRow, seedBoxX + 51, "%2d. %-12s", i + 19, state.generatedSeedWords[i + 18].c_str());
        seedRow++;
    }
    attroff(COLOR_PAIR(4));
    
    innerRow += 10;
    mvprintw(innerRow, boxX + 3, "Have you written down your seed phrase? [y/n]: _");
    
    ::refresh();
}


void TUI::Impl::drawWalletCreated() {
    clear();
    int row = 2;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    
    attron(COLOR_PAIR(1) | A_BOLD);
    centerText(row++, "WALLET CREATED SUCCESSFULLY");
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    row += 1;
    drawBox(row, boxX, 18, boxW, "");
    
    int innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Your Wallet Address:");
    mvprintw(innerRow++, boxX + 3, "============================================");
    innerRow++;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    std::string addr = state.wallet.address.empty() ? "not created" : state.wallet.address;
    auto addrLines = wrapLines(addr, boxW - 6, 2);
    for (const auto& l : addrLines) {
        printClippedLine(innerRow++, boxX + 3, boxW - 6, l);
    }
    attroff(COLOR_PAIR(4) | A_BOLD);
    innerRow++;
    
    const std::string walletBalance = utils::Formatter::formatCurrency(state.wallet.balance);
    mvprintw(innerRow++, boxX + 3, "Balance:           %s", walletBalance.c_str());
    mvprintw(innerRow++, boxX + 3, "Status:            Active");
    mvprintw(innerRow++, boxX + 3, "Protection:        %s", shortPqcProtectionLabel(status).c_str());
    innerRow++;
    
    for (int i = 0; i < boxW - 6; i++) {
        mvaddch(innerRow, boxX + 3 + i, '-');
    }
    innerRow += 2;
    
    mvprintw(innerRow++, boxX + 3, "Start earning NGT by:");
    attron(COLOR_PAIR(1));
    mvprintw(innerRow++, boxX + 5, "* Contributing knowledge to the network");
    mvprintw(innerRow++, boxX + 5, "* Validating others' contributions");
    mvprintw(innerRow++, boxX + 5, "* Keeping your node online");
    attroff(COLOR_PAIR(1));
    
    row += 20;
    attron(A_BLINK);
    centerText(row, "Press [ENTER] to continue to dashboard...");
    attroff(A_BLINK);
    
    ::refresh();
}

void TUI::Impl::drawWalletImport() {
    clear();
    int row = 2;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;

    attron(COLOR_PAIR(1) | A_BOLD);
    centerText(row++, "IMPORT WALLET");
    attroff(COLOR_PAIR(1) | A_BOLD);

    row += 1;
    drawBox(row, boxX, 10, boxW, "");

    int innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Paste your 12/24-word seed phrase:");
    mvprintw(innerRow++, boxX + 3, "(words separated by spaces)");
    innerRow++;

    std::string display = inputBuffer;
    if (display.size() > static_cast<size_t>(boxW - 8)) {
        display = display.substr(display.size() - (boxW - 8));
    }
    attron(A_REVERSE);
    mvprintw(innerRow++, boxX + 3, "%-*s", boxW - 6, "");
    {
        std::string line = display + "_";
        int sw = safeScreenWidth(boxX + 3, boxW - 6);
        if (sw > 0) mvaddnstr(innerRow - 1, boxX + 3, line.c_str(), sw);
    }
    attroff(A_REVERSE);

    innerRow += 2;
    mvprintw(innerRow++, boxX + 3, "[Enter] Import (overwrites wallet.dat)   [Esc] Back");

    ::refresh();
}

void TUI::Impl::drawConnected() {
    clear();
    int row = 2;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    
    attron(COLOR_PAIR(1) | A_BOLD);
    centerText(row++, "SYNAPSENET CONNECTED");
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    row += 1;
    drawBox(row, boxX, 18, boxW, "");
    
    int innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Node ID:           %s", state.nodeId.c_str());
    
    attron(COLOR_PAIR(1));
    mvprintw(innerRow++, boxX + 3, "Status:            * ONLINE");
    attroff(COLOR_PAIR(1));
    
    std::string shortAddr = state.wallet.address.empty() ? "not set" : 
        state.wallet.address.substr(0, 10) + "..." + state.wallet.address.substr(state.wallet.address.length() - 4);
    mvprintw(innerRow++, boxX + 3, "Wallet:            %s", shortAddr.c_str());
    const std::string walletBalance = utils::Formatter::formatCurrency(state.wallet.balance);
    mvprintw(innerRow++, boxX + 3, "Balance:           %s", walletBalance.c_str());
    innerRow++;
    
    for (int i = 0; i < boxW - 6; i++) {
        mvaddch(innerRow, boxX + 3 + i, '-');
    }
    innerRow += 2;
    
    mvprintw(innerRow++, boxX + 3, "Network:           %lu nodes online", state.network.totalNodes);
    mvprintw(innerRow++, boxX + 3, "Knowledge Chain:   %.1f GB (%s)", 
             state.network.networkSize, state.network.synced ? "synced" : "syncing");
    mvprintw(innerRow++, boxX + 3, "Your Contribution: %lu entries", state.network.knowledgeEntries);
    mvprintw(innerRow++, boxX + 3, "Quality Score:     %.1f%%", static_cast<double>(state.qualityScore));
    innerRow++;
    
    for (int i = 0; i < boxW - 6; i++) {
        mvaddch(innerRow, boxX + 3 + i, '-');
    }
    innerRow += 2;
    
    mvprintw(innerRow++, boxX + 3, "Last Session:      First time");
    attron(COLOR_PAIR(1));
    mvprintw(innerRow++, boxX + 3, "Welcome to SynapseNet!");
    attroff(COLOR_PAIR(1));
    
    row += 20;
    centerText(row, "Loading dashboard...");
    row += 2;
    centerText(row, "Press [ENTER] to continue");
    
    ::refresh();
}

void TUI::Impl::drawDashboard() {
    clear();
    int row = 1;
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "SYNAPSENET PRIMARY DASHBOARD");
    attroff(COLOR_PAIR(4) | A_BOLD);

    AttachedAgentStatusInfo info;
    {
        std::lock_guard<std::mutex> lock(state.naanMutex);
        info = state.attachedAgent;
    }

    int boxW = 78;
    int boxX = (COLS - boxW) / 2;
    if (boxX < 0) boxX = 0;

    row += 1;
    drawBox(row, boxX, 15, boxW, "Node Summary");
    int innerRow = row + 1;
    const bool nodeOnline = state.networkOnline || state.listeningPort != 0;
    std::string torLabel;
    if (info.torRequired) {
        if (info.torReadyForWeb) torLabel = "WEB READY";
        else if (info.torReachable) torLabel = "SOCKS UP (bootstrapping)";
        else torLabel = "BOOTSTRAP (auto-retry)";
    } else {
        if (info.torReadyForWeb) torLabel = "OPTIONAL (WEB READY)";
        else torLabel = info.torReachable ? "OPTIONAL (SOCKS)" : "DISABLED";
    }
    std::string naanLabel = info.runtimeState.empty() ? "BOOTSTRAP" : info.runtimeState;
    if (!info.runtimeInitialized && naanLabel == "BOOTSTRAP") {
        naanLabel = "BOOTSTRAP";
    }
    const bool externalTorInUse = (info.torReachable || info.torReadyForWeb) && !info.torManaged;
    const bool sharedExternal9150 = externalTorInUse && info.torSocksPort == 9150;
    const std::string torSocketHost = info.torSocksHost.empty() ? std::string("127.0.0.1") : info.torSocksHost;

    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        std::string("Node: ") + (nodeOnline ? "ONLINE" : "BOOTSTRAP") +
        "  Port: " + std::to_string(state.listeningPort) +
        "  Peers: " + std::to_string(state.peers.size()));
    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        "Tor: " + torLabel +
        "  Route: " + (info.routeMode.empty() ? "n/a" : info.routeMode));
    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        "Tor socket: " + (info.torSocksPort == 0 ? std::string("n/a") : (torSocketHost + ":" + std::to_string(info.torSocksPort))) +
        "  Circuit ready: " + std::string(info.torReachable ? "yes" : "no"));
    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        "Tor bootstrap: " + std::to_string(info.torBootstrapPercent) + "%  Web ready: " +
        std::string(info.torReadyForWeb ? "yes" : "no") +
        "  Onion ready: " + std::string(info.torReadyForOnion ? "yes" : "no") +
        "  Onion svc: " + std::string(info.torReadyForOnionService ? "yes" : "no"));
    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        "Tor source/mode: " + (externalTorInUse ? std::string("EXTERNAL") : (info.torManaged ? std::string("MANAGED") : std::string("NONE"))) +
        " / " + (info.torRuntimeMode.empty() ? std::string("n/a") : info.torRuntimeMode) +
        "  SOCKS port: " + (info.torSocksPort == 0 ? std::string("n/a") : std::to_string(info.torSocksPort)));
    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        "Tor seed: " + truncEnd(info.torSeedAddress.empty() ? std::string("n/a") : info.torSeedAddress, 42) +
        "  Onion: " + truncEnd(info.onionAddress.empty() ? std::string("n/a") : info.onionAddress, 20));
    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        "NAAN: " + naanLabel +
        "  Auto-start: ON  Manual knowledge add: [K] then [C]");
    {
        const std::string bal = utils::Formatter::formatCurrency(state.wallet.balance);
        printClippedLine(innerRow++, boxX + 2, boxW - 4,
            "Wallet: " + truncEnd(state.wallet.address.empty() ? "not created" : state.wallet.address, 24) +
            "  Balance: " + bal);
    }
    {
        std::string lastQuery = info.webLastQuery.empty() ? "n/a" : truncEnd(info.webLastQuery, 28);
        printClippedLine(innerRow++, boxX + 2, boxW - 4,
            "NAAN web: total=" + std::to_string(info.webTotalSearches) +
            " ok/fail=" + std::to_string(info.webSuccessfulFetches) + "/" + std::to_string(info.webFailedFetches) +
            " last q: " + lastQuery);
    }
    {
        std::string sites = info.webLastTopSites.empty() ? "n/a" : truncEnd(info.webLastTopSites, 28);
        printClippedLine(innerRow++, boxX + 2, boxW - 4,
            "Last web result c/o/t=" + std::to_string(info.webLastClearnetResults) + "/" +
            std::to_string(info.webLastOnionResults) + "/" + std::to_string(info.webLastResultCount) +
            "  saved: " + std::string(info.webLastPostSaved ? "yes" : "no") +
            "  sites: " + sites);
    }
    printClippedLine(innerRow++, boxX + 2, boxW - 4,
        "P2P in/out/total: " + std::to_string(info.p2pInbound) + "/" + std::to_string(info.p2pOutbound) + "/" +
        std::to_string(info.p2pConnected) + "  Sync: " + std::to_string(static_cast<int>(info.p2pSyncProgress * 100.0)) + "%");
    printClippedLine(innerRow++, boxX + 2, boxW - 4, "Agent score: " + std::to_string(info.agentScore) +
        "  Quarantine: " + std::string(info.quarantined ? "yes" : "no") +
        "  Conn c/t/o: " + info.connectorClearnetState + "/" + info.connectorTorState + "/" + info.connectorOnionState);
    printClippedLine(innerRow++, boxX + 2, boxW - 4, "Config: " + truncStart(info.configPath.empty() ? "n/a" : info.configPath, boxW - 12));

    row += 16;
    drawBox(row, boxX, 8, boxW, "Main Menu & Hotkeys");
    innerRow = row + 2;
    attron(A_BOLD | COLOR_PAIR(1));
    printClippedLine(innerRow++, boxX + 3, boxW - 6, "[A] AI Mining (NAAN)   [3] AI Query (Chat)   [Q] Quit");
    printClippedLine(innerRow++, boxX + 3, boxW - 6, "[K] Knowledge Network (manual add inside: [C])");
    printClippedLine(innerRow++, boxX + 3, boxW - 6, "[N] Network   [P] Peers   [M] Mining page   [D] Tor Diagnostics");
    attroff(A_BOLD | COLOR_PAIR(1));
    innerRow++;
    if (sharedExternal9150) {
        printClippedLine(innerRow++, boxX + 3, boxW - 6,
                         "Shared Tor 9150: Tor Browser may use SOCKS5 127.0.0.1:9150; do not start managed Tor on 9150.");
    } else if (info.torConflictHint9050) {
        printClippedLine(innerRow++, boxX + 3, boxW - 6,
                         "Hint: Tor may already be on 9150. Set external mode + SOCKS 9150 to avoid 9050 conflicts.");
    } else {
        printClippedLine(innerRow++, boxX + 3, boxW - 6,
                         "Typing safety: global quick keys are disabled in Chat and submit forms.");
    }

    ::refresh();
}


void TUI::Impl::drawStatusBar() {
    int row = LINES - 1;
    attron(A_REVERSE);
    int w = safeScreenWidth(0, COLS);
    mvhline(row, 0, ' ', w);
    std::string modelName;
    if (modelLoader && modelLoader->isLoaded()) {
        modelName = modelLoader->getInfo().name;
    } else {
        modelName = state.model.name;
    }
    std::string modelStatus = state.model.status.empty() ? "NOT LOADED" : state.model.status;
    if (modelLoader) {
        auto ms = modelLoader->getState();
        if (ms == synapse::model::ModelState::READY) modelStatus = "READY";
        else if (ms == synapse::model::ModelState::LOADING) modelStatus = "LOADING";
        else if (ms == synapse::model::ModelState::DOWNLOADING) modelStatus = "DOWNLOADING";
        else if (ms == synapse::model::ModelState::ERROR) modelStatus = "ERROR";
    }
    std::string modelSeg;
    if (!modelName.empty()) {
        modelSeg = " | Model: " + truncEnd(modelName, 18) + " " + modelStatus;
        if (modelStatus == "LOADING" || modelStatus == "DOWNLOADING") {
            modelSeg += " " + std::to_string(static_cast<int>(state.model.progress * 100)) + "%";
        }
    }
    
    // Add current operation status if available
    std::string operationStatus = "";
    {
        std::lock_guard<std::mutex> lock(state.operationMutex);
        if (!state.currentOperation.operation.empty() && !state.currentOperation.status.empty()) {
            std::string statusSymbol = "";
            if (state.currentOperation.status == "SUCCESS") statusSymbol = "✓";
            else if (state.currentOperation.status == "ERROR") statusSymbol = "✗";
            else if (state.currentOperation.status == "IN_PROGRESS") statusSymbol = "...";
            else if (state.currentOperation.status == "PENDING") statusSymbol = "⏳";
            
            operationStatus = " | " + state.currentOperation.operation + statusSymbol;
            if (!state.currentOperation.details.empty() && operationStatus.length() < 30) {
                operationStatus += " " + truncEnd(state.currentOperation.details, 20);
            }
        }
    }
    
    const std::string bal = utils::Formatter::formatCurrency(state.wallet.balance);
    AttachedAgentStatusInfo agentInfo;
    {
        std::lock_guard<std::mutex> lock(state.naanMutex);
        agentInfo = state.attachedAgent;
    }
    std::string torSeg;
    const bool externalTorInUse = (agentInfo.torReachable || agentInfo.torReadyForWeb) && !agentInfo.torManaged;
    if (agentInfo.torRequired) {
        if (externalTorInUse && agentInfo.torSocksPort != 0) {
            torSeg = " | Tor:EXT" + std::to_string(agentInfo.torSocksPort);
        } else if (agentInfo.torReadyForWeb) torSeg = " | Tor:ON";
        else if (agentInfo.torReachable) torSeg = " | Tor:SOCKS";
        else torSeg = " | Tor:BOOT";
    } else {
        if (externalTorInUse && agentInfo.torSocksPort != 0) {
            torSeg = " | Tor:EXT" + std::to_string(agentInfo.torSocksPort);
        } else {
            torSeg = (agentInfo.torReachable || agentInfo.torReadyForWeb) ? " | Tor:OPT" : " | Tor:OFF";
        }
    }
    std::string nodeSeg = std::string(" | Node:") + ((state.networkOnline || state.listeningPort != 0) ? "UP" : "BOOT");
    std::string line = " SynapseNet v0.1 | Peers: " + std::to_string(state.peers.size()) + " | Balance: " + bal +
                       nodeSeg + torSeg + modelSeg + operationStatus + " | " + (state.network.synced ? "SYNCED" : "GENESIS") + " ";
    int sw = safeScreenWidth(2, COLS - 2);
    if (sw > 0) mvaddnstr(row, 2, line.c_str(), sw);
    attroff(A_REVERSE);
}

void TUI::Impl::drawWallet() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "WALLET");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 70;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 12, boxW, "Balance");
    
    int innerRow = row + 2;
    std::string shortAddr = state.wallet.address.empty() ? "not created" :
        (state.wallet.address.size() > 18
            ? state.wallet.address.substr(0, 10) + "..." + state.wallet.address.substr(state.wallet.address.size() - 5)
            : state.wallet.address);
    mvprintw(innerRow++, boxX + 3, "Address:      %s", shortAddr.c_str());
    innerRow++;
    
    attron(COLOR_PAIR(1) | A_BOLD);
    const std::string balance = utils::Formatter::formatCurrency(state.wallet.balance);
    mvprintw(innerRow++, boxX + 3, "Balance:      %s", balance.c_str());
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    attron(COLOR_PAIR(2));
    const std::string pending = utils::Formatter::formatCurrency(state.wallet.pending);
    mvprintw(innerRow++, boxX + 3, "Pending:      %s", pending.c_str());
    attroff(COLOR_PAIR(2));
    
    const std::string totalEarned = utils::Formatter::formatCurrency(state.wallet.totalEarned);
    mvprintw(innerRow++, boxX + 3, "Total Earned: %s", totalEarned.c_str());
    innerRow++;
    mvprintw(innerRow++, boxX + 3, "Protection:   %s", shortPqcProtectionLabel(status).c_str());
    
    row += 14;
    drawBox(row, boxX, 8, boxW, "Actions");
    
    innerRow = row + 2;
    mvprintw(innerRow++, boxX + 5, "[1] Send NGT");
    mvprintw(innerRow++, boxX + 5, "[2] Receive (Show Address)");
    mvprintw(innerRow++, boxX + 5, "[3] Transaction History");
    mvprintw(innerRow++, boxX + 5, "[4] Export Keys");
    mvprintw(innerRow++, boxX + 5, "[B] Back to Dashboard");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawNetwork() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "NETWORK STATUS");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 70;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 8, boxW, "Connection");
    
    int innerRow = row + 2;
    if (state.network.totalNodes > 0) {
        attron(COLOR_PAIR(1));
        mvprintw(innerRow++, boxX + 3, "Status:       * ONLINE");
        attroff(COLOR_PAIR(1));
    } else {
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "Status:       * GENESIS (waiting for peers)");
        attroff(COLOR_PAIR(2));
    }
    mvprintw(innerRow++, boxX + 3, "Total Nodes:  %lu", state.network.totalNodes);
    mvprintw(innerRow++, boxX + 3, "Connected:    %zu peers", state.peers.size());
    mvprintw(innerRow++, boxX + 3, "Latency:      --ms");
    
    row += 10;
    drawBox(row, boxX, 10, boxW, "Connected Peers");
    
    innerRow = row + 2;
    if (state.peers.empty()) {
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "No peers connected yet.");
        mvprintw(innerRow++, boxX + 3, "Waiting for network bootstrap...");
        attroff(COLOR_PAIR(2));
    } else {
        for (const auto& peer : state.peers) {
            attron(COLOR_PAIR(1));
            printClippedLine(innerRow, boxX + 3, 21, "* " + truncEnd(peer.id, 18));
            attroff(COLOR_PAIR(1));
            std::string loc = truncEnd(peer.location, 15);
            mvprintw(innerRow, boxX + 25, "%-15s", loc.c_str());
            mvprintw(innerRow, boxX + 45, "%dms", peer.ping);
            innerRow++;
        }
    }
    
    row += 12;
    mvprintw(row++, boxX + 3, "[R] Refresh    [A] Add Peer    [B] Back");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawPeers() {
    clear();
    int row = 1;
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "PEERS");
    attroff(COLOR_PAIR(4) | A_BOLD);

    int boxW = COLS - 6;
    int boxX = 3;
    drawBox(row, boxX, 3, boxW, "Connected Nodes");
    int inner = row + 2;
    mvprintw(inner++, boxX + 2, "%-18s %-30s %-12s %-24s", "NODE ID", "PEER", "TRANSPORT", "RAW");
    mvwhline(stdscr, inner++, boxX + 1, ACS_HLINE, boxW - 2);

    int y = inner;
    int maxRows = LINES - y - 4;
    int shown = 0;
    for (const auto& p : state.peers) {
        if (shown >= maxRows) break;
        std::string id = truncEnd(p.id, 18);
        std::string displayAddr = truncEnd(p.displayAddress.empty() ? p.address : p.displayAddress, 30);
        std::string transport = truncEnd(p.transport.empty() ? std::string("clearnet") : p.transport, 12);
        std::string rawAddr = truncEnd(p.rawAddress.empty() ? (p.address + ":" + std::to_string(p.port)) : p.rawAddress, 24);
        mvprintw(y, boxX + 2, "%-18s %-30s %-12s %-24s", id.c_str(), displayAddr.c_str(), transport.c_str(), rawAddr.c_str());
        y++; shown++;
    }

    mvprintw(LINES - 3, 2, "[B] Back  [D] Disconnect  [X] Ban  [R] Refresh");
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawAttachedAgent() {
    clear();
    int row = 1;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "AI MINING (NAAN)");
    attroff(COLOR_PAIR(4) | A_BOLD);

    AttachedAgentStatusInfo info;
    std::vector<ObservatoryArtifactInfo> observatoryItems;
    {
        std::lock_guard<std::mutex> lock(state.naanMutex);
        info = state.attachedAgent;
        observatoryItems = state.observatoryFeed;
    }

    std::vector<AgentEventInfo> events;
    {
        std::lock_guard<std::mutex> lock(state.agentEventMutex);
        events = state.agentEvents;
    }

    std::vector<LocalAppState::RewardNotification> rewards;
    {
        std::lock_guard<std::mutex> lock(state.rewardMutex);
        rewards = state.rewardHistory;
    }

    std::vector<KnowledgeEntrySummary> entries;
    {
        std::lock_guard<std::mutex> lock(state.knowledgeMutex);
        entries = state.knowledgeEntries;
    }

    const std::array<std::string, 9> pageNames = {
        "Overview",
        "Network & Mining",
        "Wallet & Earnings",
        "Contributions Pipeline",
        "Security & Policy",
        "Logs & Diagnostics",
        "Agent Network",
        "IDE / Code",
        "Storage & Recovery"
    };
    int attachedAgentPageIndex = static_cast<int>(attachedAgentPage);
    if (attachedAgentPageIndex < 0) attachedAgentPageIndex = 0;
    if (attachedAgentPageIndex >= static_cast<int>(pageNames.size())) {
        attachedAgentPageIndex = static_cast<int>(pageNames.size()) - 1;
    }
    attachedAgentPage = static_cast<primary_ui::MiningPage>(attachedAgentPageIndex);

    int boxW = std::min(112, COLS - 4);
    if (boxW < 60) boxW = COLS - 2;
    int boxX = (COLS - boxW) / 2;
    if (boxX < 0) boxX = 0;

    row += 1;
    if (!info.available) {
        drawBox(row, boxX, 8, boxW, "Runtime");
        int innerRow = row + 2;
        attron(COLOR_PAIR(2));
        printClippedLine(innerRow++, boxX + 2, boxW - 4, "Attached agent runtime data is not available yet.");
        printClippedLine(innerRow++, boxX + 2, boxW - 4, "Wait for node initialization and open this panel again.");
        attroff(COLOR_PAIR(2));
        innerRow++;
        printClippedLine(innerRow, boxX + 2, boxW - 4, "[B] Back");
        drawStatusBar();
        ::refresh();
        return;
    }

    std::string nav;
    for (size_t i = 0; i < pageNames.size(); ++i) {
        if (!nav.empty()) nav += " | ";
        if (static_cast<int>(i) == attachedAgentPageIndex) nav += "[";
        nav += std::to_string(i + 1);
        nav += ".";
        nav += pageNames[i];
        if (static_cast<int>(i) == attachedAgentPageIndex) nav += "]";
    }
    printClippedLine(row++, boxX, boxW, nav);

    int contentH = LINES - row - 3;
    if (contentH < 12) contentH = 12;
    drawBox(row, boxX, contentH, boxW, pageNames[static_cast<size_t>(attachedAgentPageIndex)].c_str());
    int y = row + 1;
    int innerW = boxW - 4;

    auto line = [&](const std::string& s) {
        if (y < row + contentH - 1) {
            printClippedLine(y++, boxX + 2, innerW, s);
        }
    };

    if (attachedAgentPageIndex == 0) {
        line("Runtime state: " + info.runtimeState + " | Scheduler: " + info.schedulerState);
        line("Initialized: " + std::string(info.runtimeInitialized ? "yes" : "no") +
             " | Crash total/consecutive: " + std::to_string(info.runtimeCrashCount) +
             " / " + std::to_string(info.runtimeConsecutiveCrashes));
        line("Tick/Epoch: " + std::to_string(info.schedulerTick) + " / " + std::to_string(info.schedulerEpochIndex) +
             " | Last action: " + formatUnixTime(info.lastActionAt));
        line("Score/Band: " + std::to_string(info.agentScore) + " / " + info.agentScoreBand +
             " | Batch limit: " + std::to_string(info.batchLimit));
        line("Throttled: " + std::string(info.throttled ? "yes" : "no") +
             " | Review-only: " + (info.reviewOnly ? std::string("yes") : std::string("no")) +
             " | Local-draft-only: " + (info.localDraftOnly ? std::string("yes") : std::string("no")));
        line("Quarantined: " + std::string(info.quarantined ? "yes" : "no") +
             " | Reason: " + info.quarantineReason +
             " | Since: " + formatUnixTime(info.quarantineReasonSince));
        line("Budget cpu/ram/net: " + std::to_string(info.schedulerBudgetCpu) + " / " +
             std::to_string(info.schedulerBudgetRam) + " / " + std::to_string(info.schedulerBudgetNetwork));
        line("Config path: " + truncStart(info.configPath, innerW - 13));
        line("Web config path: " + truncStart(info.webConfigPath, innerW - 17));
    } else if (attachedAgentPageIndex == 1) {
        line("Route mode: " + info.routeMode +
             " | Tor required/socks/web: " + (info.torRequired ? std::string("yes") : std::string("no")) +
             " / " + (info.torReachable ? std::string("yes") : std::string("no")) +
             " / " + (info.torReadyForWeb ? std::string("yes") : std::string("no")));
        line("Tor managed/degraded: " + std::string(info.torManaged ? "yes" : "no") +
             " / " + (info.torDegraded ? std::string("yes") : std::string("no")) +
             " | Onion service: " + (info.onionServiceActive ? std::string("active") : std::string("inactive")) +
             " | State: " + (info.onionServiceState.empty() ? std::string("n/a") : info.onionServiceState));
        line("Tor runtime mode/port: " + (info.torRuntimeMode.empty() ? std::string("n/a") : info.torRuntimeMode) +
             " / " + (info.torSocksPort == 0 ? std::string("n/a") : std::to_string(info.torSocksPort)) +
             (info.torSocksPort == 9150 && !info.torManaged && (info.torReachable || info.torReadyForWeb)
                 ? std::string(" | shared external Tor (9150)")
                 : std::string("")));
        line("Tor control reachable/port: " + std::string(info.torControlReachable ? "yes" : "no") +
             " / " + (info.torControlPort == 0 ? std::string("n/a") : std::to_string(info.torControlPort)));
        if (info.torSocksPort == 9150 && !info.torManaged && (info.torReachable || info.torReadyForWeb)) {
            line("Shared mode hint: Tor Browser can use SOCKS5 127.0.0.1:9150 | Do not start managed Tor on 9150");
        } else if (info.torConflictHint9050) {
            line("Conflict hint: SynapseNet is using 9050, but external Tor may already be on 9150 (switch to external + 9150)");
        }
        line("Clearnet fallback allowed: " + std::string(info.clearnetFallbackAllowed ? "yes" : "no") +
             " | P2P fallback allowed: " + (info.p2pFallbackAllowed ? std::string("yes") : std::string("no")));
        line("P2P peers connected: " + std::to_string(info.p2pConnected) +
             " (in " + std::to_string(info.p2pInbound) + ", out " + std::to_string(info.p2pOutbound) + ")");
        std::ostringstream sync;
        sync << std::fixed << std::setprecision(2) << (info.p2pSyncProgress * 100.0);
        line("P2P port: " + std::to_string(info.p2pPort) + " | Sync progress: " + sync.str() + "%");
        line("Pressure peer/in/out: " + std::to_string(info.networkPeerPressurePercent) + "% / " +
             std::to_string(info.networkInboundPressurePercent) + "% / " +
             std::to_string(info.networkOutboundPressurePercent) + "%");
        line("Overload: " + std::string(info.networkOverloadMode ? "yes" : "no") +
             " | Buffered RX bytes: " + std::to_string(info.networkBufferedRxBytes));
        line("Reject/Evict/Bans: " + std::to_string(info.networkRejectedConnections) + " / " +
             std::to_string(info.networkEvictedPeers) + " / " + std::to_string(info.networkTempBans));
        line("Malformed/Rate/Transitions: " + std::to_string(info.networkMalformedMessages) + " / " +
             std::to_string(info.networkRateLimitedEvents) + " / " + std::to_string(info.networkOverloadTransitions));
        line("Backpressure inv/getdata: " + std::to_string(info.networkInvBackpressureDrops) + " / " +
             std::to_string(info.networkGetDataBackpressureDrops));
        line("Gossip suppressed/subset: " + std::to_string(info.networkGossipSuppressed) + " / " +
             std::to_string(info.networkGossipSubsetRouted) +
             " | Consensus lag: " + std::to_string(info.networkConsensusLag));
        line("Ledger height: " + std::to_string(info.ledgerHeight));
        line("Ledger tip: " + truncEnd(info.ledgerTipHash, innerW - 12));
        line("Last block time: " + formatUnixTime(info.ledgerLastBlockTime));
        line("Hashing active: " + std::string(info.miningActive ? "yes" : "no") +
             " | Work target: " + (info.miningWorkTarget.empty() ? std::string("n/a") : info.miningWorkTarget));
        line("Hash attempts last/total: " + std::to_string(info.miningHashAttemptsLast) +
             " / " + std::to_string(info.miningHashAttemptsTotal));
        line("Last solved at: " + formatUnixTime(info.miningLastSolvedAt));
        line("Candidate hash: " + truncEnd(info.miningCandidateHash, innerW - 17));
    } else if (attachedAgentPageIndex == 2) {
        const std::string bal = utils::Formatter::formatCurrency(state.wallet.balance);
        const std::string pending = utils::Formatter::formatCurrency(state.wallet.pending);
        line("Address: " + (state.wallet.address.empty() ? std::string("not available") : state.wallet.address));
        line("Balance: " + bal + " | Pending: " + pending);
        const std::string earned = utils::Formatter::formatCurrency(state.wallet.totalEarned);
        line("Total earned: " + earned);
        line("Key material is never displayed automatically in this panel.");
        line("Recent reward events:");
        int printed = 0;
        for (int i = static_cast<int>(rewards.size()) - 1; i >= 0; --i) {
            if (printed >= 5) break;
            std::ostringstream rewardLine;
            rewardLine << formatUnixTime(rewards[static_cast<size_t>(i)].timestamp) << "  +"
                       << std::fixed << std::setprecision(8) << (static_cast<double>(rewards[static_cast<size_t>(i)].amountAtoms) / 100000000.0)
                       << " NGT  " << truncEnd(rewards[static_cast<size_t>(i)].reason, 28);
            line(rewardLine.str());
            printed++;
        }
        if (printed == 0) line("No reward events yet.");
        line("[E] Export wallet public info (address only)");
    } else if (attachedAgentPageIndex == 3) {
        line("Queue size: " + std::to_string(info.draftQueueSize));
        line("Queued/Review/Approved/Rejected: " + std::to_string(info.queuedDrafts) + " / " +
             std::to_string(info.reviewDrafts) + " / " + std::to_string(info.approvedDrafts) + " / " +
             std::to_string(info.rejectedDrafts));
        line("Submitted drafts: " + std::to_string(info.submittedDrafts));
        line("Pipeline runs: " + std::to_string(info.pipelineRuns));
        line("Pipeline approved/rejected/submitted: " + std::to_string(info.pipelineApproved) + " / " +
             std::to_string(info.pipelineRejected) + " / " + std::to_string(info.pipelineSubmitted));
        line("Task runs research/verify/review/draft/submit: " +
             std::to_string(info.taskRunsResearch) + " / " +
             std::to_string(info.taskRunsVerify) + " / " +
             std::to_string(info.taskRunsReview) + " / " +
             std::to_string(info.taskRunsDraft) + " / " +
             std::to_string(info.taskRunsSubmit));
        line("Last review: " + formatUnixTime(info.lastReviewAt) + " | Last draft: " + formatUnixTime(info.lastDraftAt));
        line("Submission decisions are deterministic and policy-gated.");
    } else if (attachedAgentPageIndex == 4) {
        line("Policy hash: " + info.policyHash);
        line("Redaction counter: " + std::to_string(info.redactionCount));
        line("Connector health clearnet/tor/onion: " + info.connectorClearnetState + " / " +
             info.connectorTorState + " / " + info.connectorOnionState);
        line("Connector policy blocks/failures: " + std::to_string(info.connectorPolicyBlocks) + " / " +
             std::to_string(info.connectorFailures));
        line("Quarantine: " + std::string(info.quarantined ? "yes" : "no") +
             " | Reason: " + info.quarantineReason + " | Since: " + formatUnixTime(info.quarantineReasonSince));
        line("Runtime paths:");
        line("Data dir: " + truncStart(info.dataDir, innerW - 10));
        line("Config: " + truncStart(info.configPath, innerW - 8));
        line("[R] Request runtime self-check (read-only)");
    } else if (attachedAgentPageIndex == 5) {
        const std::array<std::string, 6> filters = {"all", "tor", "pipeline", "security", "reward", "runtime"};
        if (attachedAgentLogFilter < 0) attachedAgentLogFilter = 0;
        if (attachedAgentLogFilter >= static_cast<int>(filters.size())) {
            attachedAgentLogFilter = static_cast<int>(filters.size()) - 1;
        }
        line("Filter: " + filters[static_cast<size_t>(attachedAgentLogFilter)]);

        auto matchesFilter = [&](const AgentEventInfo& ev) {
            if (attachedAgentLogFilter == 0) return true;
            const std::string cat = ev.category;
            if (attachedAgentLogFilter == 1) return cat == "tor";
            if (attachedAgentLogFilter == 2) return cat == "pipeline";
            if (attachedAgentLogFilter == 3) return cat == "security";
            if (attachedAgentLogFilter == 4) return cat == "reward";
            return cat == "runtime";
        };

        std::vector<AgentEventInfo> filtered;
        filtered.reserve(events.size());
        for (const auto& ev : events) {
            if (matchesFilter(ev)) filtered.push_back(ev);
        }

        int rowsAvail = std::max(1, contentH - 4);
        int maxScroll = 0;
        if (static_cast<int>(filtered.size()) > rowsAvail) {
            maxScroll = static_cast<int>(filtered.size()) - rowsAvail;
        }
        if (attachedAgentLogScroll < 0) attachedAgentLogScroll = 0;
        if (attachedAgentLogScroll > maxScroll) attachedAgentLogScroll = maxScroll;

        int start = std::max(0, static_cast<int>(filtered.size()) - rowsAvail - attachedAgentLogScroll);
        int end = std::min(static_cast<int>(filtered.size()), start + rowsAvail);
        for (int i = start; i < end; ++i) {
            const auto& ev = filtered[static_cast<size_t>(i)];
            line(formatUnixTime(ev.timestamp) + " [" + ev.category + "] " + truncEnd(ev.message, innerW - 28));
        }
        if (filtered.empty()) line("No events for this filter.");
    } else if (attachedAgentPageIndex == 6) {
        line("Observatory artifacts: " + std::to_string(observatoryItems.size()));
        line("Feed is read-only and signed.");
        int printed = 0;
        for (int i = static_cast<int>(observatoryItems.size()) - 1; i >= 0; --i) {
            if (printed >= 8) break;
            const auto& item = observatoryItems[static_cast<size_t>(i)];
            line(formatUnixTime(item.timestamp) + "  " + truncEnd(item.type, 10) + "  " + truncEnd(item.hash, 16));
            printed++;
        }
        if (printed == 0) line("No observatory artifacts yet.");
        line("[O] Open detailed observatory feed");
    } else if (attachedAgentPageIndex == 7) {
        size_t codeCount = 0;
        for (const auto& e : entries) {
            if (e.contentType == 2) codeCount++;
        }
        std::string modelName = state.model.name.empty() ? "none" : state.model.name;
        if (modelLoader && modelLoader->isLoaded()) {
            modelName = modelLoader->getInfo().name;
        }
        line("IDE/RPC readiness: " + std::string(modelLoader && modelLoader->isLoaded() ? "model loaded" : "model not loaded"));
        line("Model: " + truncEnd(modelName, innerW - 8));
        line("Recent CODE entries tracked in node: " + std::to_string(codeCount));
        line("Operator typing is not used for submit path in AI Mining mode.");
        line("Code submissions are produced by the deterministic agent pipeline.");
    } else if (attachedAgentPageIndex == 8) {
        line("Data dir: " + truncStart(info.dataDir, innerW - 10));
        line("synapsenet.conf: " + truncStart(info.configPath, innerW - 16));
        line("web_search.conf: " + truncStart(info.webConfigPath, innerW - 16));
        line("Audit segments: " + std::to_string(info.storageAuditSegments) +
             " | Retained events: " + std::to_string(info.storageAuditRetainedEvents));
        line("Recovered lines: " + std::to_string(info.storageAuditRecoveredLines) +
             " | Dropped segments: " + std::to_string(info.storageAuditDroppedSegments));
        line("Index recovery runs: " + std::to_string(info.storageIndexRecoveryRuns) +
             " | Last: " + formatUnixTime(info.storageIndexRecoveryLastAt));
        line("Consistency checks/repairs: " + std::to_string(info.storageConsistencyChecks) +
             " / " + std::to_string(info.storageConsistencyRepairs));
        line("Last consistency check: " + formatUnixTime(info.storageConsistencyLastAt));
    }

    int controlsY = std::min(row + contentH, LINES - 2);
    std::string controls = "[1-9] Page  [H/L] Prev/Next  [D] Tor Diag  [B] Back";
    if (attachedAgentPageIndex == 2) controls += "  [E] Export Address";
    if (attachedAgentPageIndex == 4) controls += "  [R] Runtime Self-check";
    if (attachedAgentPageIndex == 5) controls += "  [F] Filter  [Up/Down/PgUp/PgDn] Scroll";
    if (attachedAgentPageIndex == 6) controls += "  [O] Observatory";
    printClippedLine(controlsY, boxX + 2, boxW - 4, controls);

    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawObservatory() {
    clear();
    int row = 1;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "OBSERVATORY (READ-ONLY)");
    attroff(COLOR_PAIR(4) | A_BOLD);

    std::vector<ObservatoryArtifactInfo> items;
    {
        std::lock_guard<std::mutex> lock(state.naanMutex);
        items = state.observatoryFeed;
    }

    int boxW = 78;
    int boxX = (COLS - boxW) / 2;
    if (boxX < 0) boxX = 0;
    row += 1;

    int usableH = LINES - row - 3;
    if (usableH < 15) usableH = 15;
    int listH = std::min(12, std::max(8, usableH / 2));
    int detailsH = usableH - listH;
    if (detailsH < 7) {
        detailsH = 7;
        listH = std::max(8, usableH - detailsH);
    }
    drawBox(row, boxX, listH, boxW, "Transcript");

    int dataRows = listH - 3;
    if (dataRows < 1) dataRows = 1;
    int maxScroll = 0;
    if (static_cast<int>(items.size()) > dataRows) {
        maxScroll = static_cast<int>(items.size()) - dataRows;
    }
    if (observatoryScroll < 0) observatoryScroll = 0;
    if (observatoryScroll > maxScroll) observatoryScroll = maxScroll;
    if (items.empty()) observatorySelection = 0;
    if (observatorySelection < 0) observatorySelection = 0;
    if (!items.empty() && observatorySelection >= static_cast<int>(items.size())) {
        observatorySelection = static_cast<int>(items.size()) - 1;
    }

    int innerW = boxW - 6;
    printClippedLine(row + 1, boxX + 3, innerW, "TS                 ROOM            TYPE      HASH        PAYLOAD");
    for (int i = 0; i < dataRows; ++i) {
        int idx = observatoryScroll + i;
        int y = row + 2 + i;
        if (idx >= static_cast<int>(items.size())) {
            mvhline(y, boxX + 3, ' ', innerW);
            continue;
        }

        const auto& it = items[idx];
        std::string ts = formatUnixTime(it.timestamp);
        std::string line = truncEnd(ts, 19);
        while (line.size() < 20) line.push_back(' ');
        line += truncEnd(it.roomId, 14);
        while (line.size() < 36) line.push_back(' ');
        line += truncEnd(it.type, 8);
        while (line.size() < 46) line.push_back(' ');
        line += truncEnd(it.hash, 10);
        while (line.size() < 58) line.push_back(' ');
        line += truncEnd(it.payloadPreview, innerW - 58);

        if (idx == observatorySelection) {
            attron(A_REVERSE);
            printClippedLine(y, boxX + 3, innerW, line);
            attroff(A_REVERSE);
        } else {
            printClippedLine(y, boxX + 3, innerW, line);
        }
    }

    if (observatoryScroll > 0) mvaddch(row + 1, boxX + boxW - 2, '^');
    if (observatoryScroll < maxScroll) mvaddch(row + listH - 2, boxX + boxW - 2, 'v');

    int detailsY = row + listH + 1;
    drawBox(detailsY, boxX, detailsH, boxW, "Selected Artifact Metadata");

    int detailsRow = detailsY + 1;
    if (items.empty()) {
        attron(COLOR_PAIR(2));
        printClippedLine(detailsRow++, boxX + 3, innerW, "No observatory artifacts yet.");
        if (detailsRow < detailsY + detailsH - 1) {
            printClippedLine(detailsRow++, boxX + 3, innerW, "The panel is read-only and updates as signed room artifacts arrive.");
        }
        attroff(COLOR_PAIR(2));
    } else {
        const auto& selected = items[observatorySelection];
        printClippedLine(detailsRow++, boxX + 3, innerW, "Hash: " + selected.hash);
        if (detailsRow < detailsY + detailsH - 1) {
            printClippedLine(detailsRow++, boxX + 3, innerW, "Room/Type: " + selected.roomId + " / " + selected.type);
        }
        if (detailsRow < detailsY + detailsH - 1) {
            printClippedLine(detailsRow++, boxX + 3, innerW, "Author: " + selected.author);
        }
        if (detailsRow < detailsY + detailsH - 1) {
            printClippedLine(detailsRow++, boxX + 3, innerW, "Timestamp: " + formatUnixTime(selected.timestamp));
        }
        if (detailsRow < detailsY + detailsH - 1) {
            printClippedLine(detailsRow++, boxX + 3, innerW, "Payload preview:");
        }
        if (detailsRow < detailsY + detailsH - 1) {
            printClippedLine(detailsRow++, boxX + 3, innerW, truncEnd(selected.payloadPreview, innerW));
        }
    }

    int controlsY = std::min(detailsY + detailsH, LINES - 2);
    printClippedLine(controlsY, boxX + 3, innerW, "[Up/Down] Select  [PgUp/PgDn] Scroll  [A] Agent status  [B] Back");

    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawKnowledge() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "KNOWLEDGE NETWORK");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 10, boxW, "PoE v1 Status");

    int innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Total entries:     %lu", state.network.knowledgeEntries);
    mvprintw(innerRow++, boxX + 3, "Finalized entries: %lu", state.network.knowledgeFinalized);
    mvprintw(innerRow++, boxX + 3, "Pending entries:   %lu", state.network.knowledgePending);

    std::vector<KnowledgeEntrySummary> entries;
    {
        std::lock_guard<std::mutex> lock(state.knowledgeMutex);
        entries = state.knowledgeEntries;
    }
    std::vector<KnowledgeEntrySummary> nonCodeEntries;
    nonCodeEntries.reserve(entries.size());
    size_t codeCount = 0;
    size_t rewardedCount = 0;
    for (const auto& e : entries) {
        if (e.contentType == 2) {
            codeCount++;
            continue;
        }
        nonCodeEntries.push_back(e);
        if (e.acceptanceRewardCredited) rewardedCount++;
    }
    mvprintw(innerRow++, boxX + 3, "Code entries:      %zu", codeCount);
    mvprintw(innerRow++, boxX + 3, "Acceptance-paid entries: %zu", rewardedCount);

    if (state.network.lastRewardEpochId > 0 && state.network.lastReward > 0) {
        std::ostringstream rewardLine;
        rewardLine << "Last epoch payout: +" << std::fixed << std::setprecision(8)
                   << (static_cast<double>(state.network.lastReward) / 100000000.0)
                   << " NGT (epoch #" << state.network.lastRewardEpochId
                   << ", " << state.network.lastRewardEntries << " entries)";
        printClippedLine(innerRow++, boxX + 3, boxW - 6, rewardLine.str());
    } else {
        printClippedLine(innerRow++, boxX + 3, boxW - 6, "Last epoch payout: n/a");
    }
    printClippedLine(innerRow++, boxX + 3, boxW - 6, "Epoch payouts run when newly finalized entries are available.");

    int listY = row + 12;
    int listH = LINES - listY - 6;
    if (listH < 8) listH = 8;
    if (listH > 16) listH = 16;
    drawBox(listY, boxX, listH, boxW, "Entries (scroll for full history)");

    int innerW = boxW - 6;
    int headerY = listY + 1;
    printClippedLine(headerY, boxX + 3, innerW, "ID        STATUS     VOTES   REWARD     TITLE");

    int dataRows = listH - 3;
    if (dataRows < 1) dataRows = 1;

    int maxScroll = 0;
    if (static_cast<int>(nonCodeEntries.size()) > dataRows) {
        maxScroll = static_cast<int>(nonCodeEntries.size()) - dataRows;
    }
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;

    int printed = 0;
    for (int i = 0; i < dataRows; ++i) {
        int idx = scrollOffset + i;
        int y = listY + 2 + i;
        if (idx >= static_cast<int>(nonCodeEntries.size())) {
            mvhline(y, boxX + 3, ' ', innerW);
            continue;
        }
        const auto& e = nonCodeEntries[idx];
        std::string id = e.submitId.size() > 8 ? e.submitId.substr(0, 8) : e.submitId;
        std::string st = e.finalized ? "FINAL" : "PENDING";
        std::string votes = std::to_string(e.votes) + "/" + std::to_string(e.requiredVotes);
        std::ostringstream r;
        r << "+" << std::fixed << std::setprecision(4) << (static_cast<double>(e.acceptanceRewardAtoms) / 100000000.0);
        std::string reward = r.str();
        if (e.acceptanceRewardCredited) reward += " PAID";
        std::string title = truncEnd(e.title, innerW - 38);
        std::string line = id + "  " + st + "   " + votes;
        while (line.size() < 24) line.push_back(' ');
        line += reward;
        while (line.size() < 35) line.push_back(' ');
        line += " " + title;

        if (e.finalized) attron(COLOR_PAIR(1));
        else attron(COLOR_PAIR(2));
        printClippedLine(y, boxX + 3, innerW, line);
        if (e.finalized) attroff(COLOR_PAIR(1));
        else attroff(COLOR_PAIR(2));
        printed++;
    }

    if (scrollOffset > 0) {
        mvaddch(listY + 1, boxX + boxW - 2, '^');
    }
    if (maxScroll > 0 && scrollOffset < maxScroll) {
        mvaddch(listY + listH - 2, boxX + boxW - 2, 'v');
    }

    int controlsY = listY + listH + 1;
    printClippedLine(controlsY, boxX + 3, innerW, "[C] Contribute (manual add)  [Up/Down] Scroll  [B] Back  (Epoch rewards: automatic)");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawKnowledgeSubmit() {
    clear();
    int row = 1;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "CONTRIBUTE KNOWLEDGE");
    attroff(COLOR_PAIR(4) | A_BOLD);

    row++;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;

    drawBox(row, boxX, 16, boxW, "New Entry");
    int innerRow = row + 2;

    auto trunc = [](const std::string& s, int maxLen) -> std::string {
        if (maxLen <= 0) return {};
        if (static_cast<int>(s.size()) <= maxLen) return s;
        if (maxLen <= 3) return s.substr(0, static_cast<size_t>(maxLen));
        return s.substr(0, static_cast<size_t>(maxLen - 3)) + "...";
    };

    int fieldW = boxW - 8;
    mvprintw(innerRow++, boxX + 3, "Question:");
    if (state.knowledgeField == 0) attron(A_REVERSE);
    mvprintw(innerRow++, boxX + 3, "> %s%s", trunc(state.knowledgeQuestion, fieldW - 4).c_str(), state.knowledgeField == 0 ? "_" : "");
    if (state.knowledgeField == 0) attroff(A_REVERSE);
    innerRow++;

    mvprintw(innerRow++, boxX + 3, "Answer:");
    if (state.knowledgeField == 1) attron(A_REVERSE);
    mvprintw(innerRow++, boxX + 3, "> %s%s", trunc(state.knowledgeAnswer, fieldW - 4).c_str(), state.knowledgeField == 1 ? "_" : "");
    if (state.knowledgeField == 1) attroff(A_REVERSE);
    innerRow++;

    mvprintw(innerRow++, boxX + 3, "Source (optional):");
    if (state.knowledgeField == 2) attron(A_REVERSE);
    mvprintw(innerRow++, boxX + 3, "> %s%s", trunc(state.knowledgeSource, fieldW - 4).c_str(), state.knowledgeField == 2 ? "_" : "");
    if (state.knowledgeField == 2) attroff(A_REVERSE);
    innerRow += 2;

    mvprintw(innerRow++, boxX + 3, "[Tab] Switch Field    [Enter] Next/Submit    [Esc] Cancel");
    mvprintw(innerRow++, boxX + 3, "Earn NGT for contributing useful knowledge");

    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawCode() {
    clear();
    int row = 1;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "SYNAPSE IDE");
    attroff(COLOR_PAIR(4) | A_BOLD);

    row++;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;

    drawBox(row, boxX, 7, boxW, "Code Contributions (PoE v1)");

    std::vector<KnowledgeEntrySummary> entries;
    {
        std::lock_guard<std::mutex> lock(state.knowledgeMutex);
        entries = state.knowledgeEntries;
    }

    size_t codeCount = 0;
    for (const auto& e : entries) {
        if (e.contentType == 2) codeCount++;
    }

    int innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Recent code entries: %zu", codeCount);
    mvprintw(innerRow++, boxX + 3, "Submit patches as deterministic CODE entries (no LLM scoring in consensus)");
    mvprintw(innerRow++, boxX + 3, "Use unified diff in a file, then submit via this screen");

    std::vector<KnowledgeEntrySummary> codeEntries;
    codeEntries.reserve(entries.size());
    for (const auto& e : entries) {
        if (e.contentType == 2) codeEntries.push_back(e);
    }

    int listY = row + 9;
    int listH = LINES - listY - 6;
    if (listH < 8) listH = 8;
    if (listH > 16) listH = 16;
    drawBox(listY, boxX, listH, boxW, "Recent CODE Entries");

    int innerW = boxW - 6;
    int headerY = listY + 1;
    printClippedLine(headerY, boxX + 3, innerW, "ID        STATUS     VOTES   REWARD     TITLE");

    int dataRows = listH - 3;
    if (dataRows < 1) dataRows = 1;

    int maxScroll = 0;
    if (static_cast<int>(codeEntries.size()) > dataRows) {
        maxScroll = static_cast<int>(codeEntries.size()) - dataRows;
    }
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;

    for (int i = 0; i < dataRows; ++i) {
        int idx = scrollOffset + i;
        int y = listY + 2 + i;
        if (idx >= static_cast<int>(codeEntries.size())) {
            mvhline(y, boxX + 3, ' ', innerW);
            continue;
        }
        const auto& e = codeEntries[idx];
        std::string id = e.submitId.size() > 8 ? e.submitId.substr(0, 8) : e.submitId;
        std::string st = e.finalized ? "FINAL" : "PENDING";
        std::string votes = std::to_string(e.votes) + "/" + std::to_string(e.requiredVotes);
        std::ostringstream r;
        r << "+" << std::fixed << std::setprecision(4) << (static_cast<double>(e.acceptanceRewardAtoms) / 100000000.0);
        std::string reward = r.str();
        if (e.acceptanceRewardCredited) reward += " PAID";
        std::string title = truncEnd(e.title, innerW - 38);
        std::string line = id + "  " + st + "   " + votes;
        while (line.size() < 24) line.push_back(' ');
        line += reward;
        while (line.size() < 35) line.push_back(' ');
        line += " " + title;

        if (e.finalized) attron(COLOR_PAIR(1));
        else attron(COLOR_PAIR(2));
        printClippedLine(y, boxX + 3, innerW, line);
        if (e.finalized) attroff(COLOR_PAIR(1));
        else attroff(COLOR_PAIR(2));
    }

    if (scrollOffset > 0) {
        mvaddch(listY + 1, boxX + boxW - 2, '^');
    }
    if (maxScroll > 0 && scrollOffset < maxScroll) {
        mvaddch(listY + listH - 2, boxX + boxW - 2, 'v');
    }

    int controlsY = listY + listH + 1;
    printClippedLine(controlsY, boxX + 3, innerW, "[I] Launch IDE  [C] Submit Patch  [Up/Down] Scroll  [B] Back");

    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawCodeSubmit() {
    clear();
    int row = 1;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "SUBMIT CODE (PATCH)");
    attroff(COLOR_PAIR(4) | A_BOLD);

    row++;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;

    drawBox(row, boxX, 18, boxW, "New CODE Entry");
    int innerRow = row + 2;

    int fieldW = boxW - 8;
    mvprintw(innerRow++, boxX + 3, "Title:");
    if (state.codeField == 0) attron(A_REVERSE);
    mvprintw(innerRow++, boxX + 3, "> %s%s", truncEnd(state.codeTitle, fieldW - 4).c_str(), state.codeField == 0 ? "_" : "");
    if (state.codeField == 0) attroff(A_REVERSE);
    innerRow++;

    mvprintw(innerRow++, boxX + 3, "Patch file path:");
    if (state.codeField == 1) attron(A_REVERSE);
    mvprintw(innerRow++, boxX + 3, "> %s%s", truncStart(state.codePatchFile, fieldW - 4).c_str(), state.codeField == 1 ? "_" : "");
    if (state.codeField == 1) attroff(A_REVERSE);
    innerRow++;

    mvprintw(innerRow++, boxX + 3, "Citations (optional, comma-separated hex IDs):");
    if (state.codeField == 2) attron(A_REVERSE);
    mvprintw(innerRow++, boxX + 3, "> %s%s", truncEnd(state.codeCitations, fieldW - 4).c_str(), state.codeField == 2 ? "_" : "");
    if (state.codeField == 2) attroff(A_REVERSE);
    innerRow++;

    uintmax_t patchSize = 0;
    bool patchOk = false;
    if (!state.codePatchFile.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(state.codePatchFile, ec) && std::filesystem::is_regular_file(state.codePatchFile, ec)) {
            patchSize = std::filesystem::file_size(state.codePatchFile, ec);
            patchOk = !ec;
        }
    }

    if (patchOk) {
        mvprintw(innerRow++, boxX + 3, "Patch size: %ju bytes", static_cast<uintmax_t>(patchSize));
    } else {
        mvprintw(innerRow++, boxX + 3, "Patch size: --");
    }
    innerRow++;

    mvprintw(innerRow++, boxX + 3, "[Tab] Switch Field    [Ctrl+S] Submit    [Esc] Cancel");
    mvprintw(innerRow++, boxX + 3, "Tip: generate a unified diff and save it to a file");

    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawModel() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "LOCAL AI MODEL");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 70;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 9, boxW, "Model Status");
    
    int innerRow = row + 2;
    {
        std::string modelName = state.model.name.empty() ? "not loaded" : state.model.name;
        printClippedLine(innerRow++, boxX + 3, boxW - 6, "Model:    " + truncEnd(modelName, boxW - 15));
    }
    
    if (state.model.status == "ACTIVE") {
        attron(COLOR_PAIR(1));
        mvprintw(innerRow++, boxX + 3, "Status:   * %s", state.model.status.c_str());
        attroff(COLOR_PAIR(1));
    } else {
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "Status:   * %s", state.model.status.c_str());
        attroff(COLOR_PAIR(2));
    }
    
    mvprintw(innerRow, boxX + 3, "Progress: ");
    int barX = boxX + 13;
    mvaddch(innerRow, barX, '[');
    for (int i = 0; i < 40; i++) {
        if (i < static_cast<int>(state.model.progress * 40)) {
            attron(COLOR_PAIR(1));
            mvprintw(innerRow, barX + 1 + i, "#");
            attroff(COLOR_PAIR(1));
        } else {
            mvaddch(innerRow, barX + 1 + i, ' ');
        }
    }
    mvaddch(innerRow, barX + 41, ']');
    mvprintw(innerRow++, barX + 43, "%d%%", static_cast<int>(state.model.progress * 100));
    
    printClippedLine(innerRow++, boxX + 3, boxW - 6, "Mode:     " + truncEnd(state.model.mode, boxW - 15));
    mvprintw(innerRow++, boxX + 3, "Uptime:   0h 0m");
    
    row += 11;
    drawBox(row, boxX, 8, boxW, "Earnings");
    
    innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Today:      +%.1f NGT", static_cast<double>(state.model.earningsTodayAtoms) / 100000000.0);
    mvprintw(innerRow++, boxX + 3, "This Week:  +%.1f NGT", static_cast<double>(state.model.earningsWeekAtoms) / 100000000.0);
    mvprintw(innerRow++, boxX + 3, "Total:      +%.1f NGT", static_cast<double>(state.model.earningsTotalAtoms) / 100000000.0);
    
    row += 10;
    drawBox(row, boxX, 7, boxW, "Access Control");
    
    innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "[1] PRIVATE  - only you (default)");
    mvprintw(innerRow++, boxX + 3, "[2] SHARED   - invite nodes, set limit");
    mvprintw(innerRow++, boxX + 3, "[3] PUBLIC   - anyone can use");
    mvprintw(innerRow++, boxX + 3, "[4] PAID     - set price, earn NGT");
    
    row += 9;
    mvprintw(row++, boxX + 3, "[L] Load Model    [U] Unload    [C] Chat    [B] Back");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawAIChat() {
    clear();
    const int marginX = 2;
    int row = 1;

    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "AI QUERY (CHAT)");
    attroff(COLOR_PAIR(4) | A_BOLD);

    AttachedAgentStatusInfo agentInfo;
    {
        std::lock_guard<std::mutex> lock(state.naanMutex);
        agentInfo = state.attachedAgent;
    }

    primary_ui::ChatStatusStripInput stripInput;
    stripInput.torRequired = agentInfo.torRequired;
    stripInput.torReachable = agentInfo.torReachable;
    stripInput.torDegraded = agentInfo.torDegraded;
    stripInput.onionServiceActive = agentInfo.onionServiceActive;
    stripInput.clearnetFallbackAllowed = agentInfo.clearnetFallbackAllowed;
    stripInput.agentActivity = agentInfo.schedulerState.empty() ? "IDLE" : agentInfo.schedulerState;

    attron(COLOR_PAIR(4));
    printClippedLine(row++, marginX, COLS - marginX * 2, primary_ui::renderChatStatusStrip(stripInput, COLS - marginX * 2));
    attroff(COLOR_PAIR(4));

    auto scanModelCount = [&]() -> int {
        std::string homeDir = std::getenv("HOME") ? std::getenv("HOME") : ".";
        std::vector<std::string> scanDirs;
        scanDirs.push_back((std::filesystem::current_path() / "models").string());
        scanDirs.push_back(homeDir + "/.synapsenet/models");
        int count = 0;
        for (const auto& dir : scanDirs) {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec) || ec) continue;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (entry.is_directory(ec) || ec) continue;
                auto p = entry.path();
                if (p.extension() != ".gguf") continue;
                std::string name = p.filename().string();
                if (name.rfind("ggml-vocab-", 0) == 0) continue;
                if (name.size() >= 5 && name.substr(name.size() - 5) == ".part") continue;
                count++;
            }
        }
        return count;
    };

    std::string modelName = state.model.name;
    if (modelLoader && modelLoader->isLoaded()) {
        modelName = modelLoader->getInfo().name;
    }
    if (modelName.empty()) modelName = "none";
    std::string homeDir = std::getenv("HOME") ? std::getenv("HOME") : ".";
    std::string modelDir = homeDir + "/.synapsenet/models";
    int modelCount = scanModelCount();
    std::string modelLine = "Loaded: " + modelName + " | Model dir: " + modelDir + " | Local models: " + std::to_string(modelCount);
    printClippedLine(row++, marginX, COLS - marginX * 2, truncStart(modelLine, COLS - marginX * 2));

    const int contentY = row;
    const int inputBoxH = 3;
    const int controlsH = 1;
    int convH = LINES - contentY - inputBoxH - controlsH - 2;
    if (convH < 8) convH = 8;

    drawBox(contentY, marginX, convH, COLS - marginX * 2, "Conversation");
    const int inputY = contentY + convH + 1;
    drawBox(inputY, marginX, inputBoxH, COLS - marginX * 2, "Input");

    int innerW = COLS - marginX * 2 - 4;
    if (innerW < 10) innerW = 10;
    int maxChatLines = convH - 2;

    auto sanitizeText = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            if (c == '\r') continue;
            if (c == '\t') {
                out.append(4, ' ');
                continue;
            }
            if (c == '\n') {
                out.push_back('\n');
                continue;
            }
            if (c < 32 || c == 127) {
                out.push_back(' ');
                continue;
            }
            out.push_back(static_cast<char>(c));
        }
        return out;
    };

    auto wrapWithPrefix = [&](const std::string& text, int width, const std::string& prefix) -> std::vector<std::string> {
        std::vector<std::string> out;
        if (width <= 0) return out;
        std::string s = sanitizeText(text);
        int prefixLen = static_cast<int>(prefix.size());
        if (prefixLen >= width) prefixLen = width - 1;
        std::string lineText = prefix.substr(0, static_cast<size_t>(prefixLen));
        int col = prefixLen;

        auto flush = [&]() {
            out.push_back(lineText);
            lineText.assign(prefix.substr(0, static_cast<size_t>(prefixLen)));
            col = prefixLen;
        };

        for (char c : s) {
            if (c == '\n') {
                flush();
                continue;
            }
            if (col >= width) flush();
            lineText.push_back(c);
            col++;
        }
        if (!lineText.empty() && (lineText.size() > static_cast<size_t>(prefixLen) || out.empty())) out.push_back(lineText);
        return out;
    };

    struct RenderLine {
        std::string text;
        int color = 0;
        int attrs = 0;
    };

    std::vector<ChatMessage> history;
    {
        std::lock_guard<std::mutex> lock(state.chatMutex);
        history = state.chatHistory;
    }

    std::string streaming;
    {
        std::lock_guard<std::mutex> lock(state.aiResponseMutex);
        streaming = state.aiCurrentResponse;
    }

    std::vector<RenderLine> lines;
    lines.reserve(history.size() * 4 + 32);

    auto addMessage = [&](bool isUser, const std::string& content) {
        RenderLine header;
        header.text = isUser ? "You:" : "AI:";
        header.color = isUser ? 1 : 4;
        header.attrs = A_BOLD;
        lines.push_back(header);

        auto wrapped = wrapWithPrefix(content, innerW, "  ");
        for (auto& w : wrapped) {
            RenderLine l;
            l.text = std::move(w);
            lines.push_back(std::move(l));
        }
    };

    if (history.empty() && streaming.empty()) {
        bool hasLoaded = modelLoader && modelLoader->isLoaded();
        if (!hasLoaded) {
            attron(COLOR_PAIR(2));
            printClippedLine(contentY + 1, marginX + 2, innerW, "No local model loaded. Add .gguf to ~/.synapsenet/models.");
            printClippedLine(contentY + 2, marginX + 2, innerW, "Model loading is managed outside AI Query UI.");
            attroff(COLOR_PAIR(2));
        } else {
            attron(COLOR_PAIR(4));
            printClippedLine(contentY + 1, marginX + 2, innerW, "Model loaded. Type your message below.");
            attroff(COLOR_PAIR(4));
        }
    }

    if (!history.empty()) {
        for (const auto& msg : history) addMessage(msg.role == "user", msg.content);
    }
    if (state.aiGenerating && !streaming.empty()) addMessage(false, streaming);

    int maxScroll = 0;
    if (static_cast<int>(lines.size()) > maxChatLines) maxScroll = static_cast<int>(lines.size()) - maxChatLines;
    if (chatScrollOffset < 0) chatScrollOffset = 0;
    if (chatScrollOffset > maxScroll) chatScrollOffset = maxScroll;

    int start = std::max(0, static_cast<int>(lines.size()) - maxChatLines - chatScrollOffset);
    int printed = 0;
    for (int i = 0; i < maxChatLines; ++i) {
        int idx = start + i;
        if (idx >= static_cast<int>(lines.size())) break;
        const auto& l = lines[idx];
        if (l.color > 0) attron(COLOR_PAIR(l.color));
        if (l.attrs) attron(l.attrs);
        int w = safeScreenWidth(marginX + 2, innerW);
        if (w > 0) {
            mvhline(contentY + 1 + i, marginX + 2, ' ', w);
            mvaddnstr(contentY + 1 + i, marginX + 2, l.text.c_str(), w);
        }
        if (l.attrs) attroff(l.attrs);
        if (l.color > 0) attroff(COLOR_PAIR(l.color));
        printed++;
    }
    for (int i = printed; i < maxChatLines; ++i) {
        mvhline(contentY + 1 + i, marginX + 2, ' ', innerW);
    }

    if (chatScrollOffset > 0 && maxChatLines > 0) {
        mvaddch(contentY + 1, COLS - marginX - 2, '^');
    }
    if (maxScroll > 0 && chatScrollOffset < maxScroll && maxChatLines > 0) {
        mvaddch(contentY + convH - 2, COLS - marginX - 2, 'v');
    }

    if (inputCursor > inputBuffer.size()) inputCursor = inputBuffer.size();
    const int inputX = marginX + 2;
    const int inputW = safeScreenWidth(inputX, COLS - marginX * 2 - 4);
    if (inputW > 0) {
        mvhline(inputY + 1, inputX, ' ', inputW);
        mvaddnstr(inputY + 1, inputX, "> ", std::min(2, inputW));

        const int maxChars = std::max(0, inputW - 2);
        size_t startIdx = 0;
        if (maxChars > 0 && inputBuffer.size() > static_cast<size_t>(maxChars)) {
            if (inputCursor > static_cast<size_t>(maxChars)) {
                startIdx = inputCursor - static_cast<size_t>(maxChars);
            }
            if (startIdx + static_cast<size_t>(maxChars) > inputBuffer.size()) {
                startIdx = inputBuffer.size() - static_cast<size_t>(maxChars);
            }
        }

        std::string seg;
        if (maxChars > 0 && startIdx < inputBuffer.size()) {
            seg = inputBuffer.substr(startIdx, static_cast<size_t>(maxChars));
        }
        if (!seg.empty()) mvaddnstr(inputY + 1, inputX + 2, seg.c_str(), maxChars);

        size_t cursorInSeg = inputCursor >= startIdx ? (inputCursor - startIdx) : 0;
        int cx = inputX + 2 + static_cast<int>(cursorInSeg);
        if (cx >= inputX + inputW) cx = inputX + inputW - 1;
        if (cx < inputX + 2) cx = inputX + 2;

        char c = '_';
        if (inputCursor < inputBuffer.size() && cursorInSeg < seg.size()) {
            c = seg[cursorInSeg];
            if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
        }
        attron(A_REVERSE);
        mvaddch(inputY + 1, cx, c);
        attroff(A_REVERSE);
    }

    int controlsY = inputY + inputBoxH;
    std::string ctrl = "[Enter] Send  [C] Clear Chat  [X] Stop  [Up/Down/PgUp/PgDn] Scroll  [Esc] Back";
    if (state.aiGenerating) ctrl += "  (Generating...)";
    printClippedLine(controlsY, marginX + 2, COLS - marginX * 2 - 4, ctrl);

    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawSecurity() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "SECURITY STATUS");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    const auto lines = primary_ui::renderSecurityStatusSnapshot(status, boxW - 6);

    drawBox(row, boxX, static_cast<int>(lines.size()) + 5, boxW, "Quantum Security Runtime");

    int innerRow = row + 2;
    const bool allReal = status.quantumEnabled && status.kyberReal && status.dilithiumReal && status.sphincsReal;
    attron((allReal ? COLOR_PAIR(1) : COLOR_PAIR(3)) | A_BOLD);
    mvprintw(innerRow++, boxX + 3, "PQC RUNTIME STATUS: %s", shortPqcCapabilityLabel(status).c_str());
    attroff((allReal ? COLOR_PAIR(1) : COLOR_PAIR(3)) | A_BOLD);
    innerRow++;

    for (const auto& line : lines) {
        printClippedLine(innerRow++, boxX + 3, boxW - 6, line);
    }

    row += static_cast<int>(lines.size()) + 7;
    drawBox(row, boxX, 10, boxW, "Security Layers");
    
    innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Layer 0: CLASSIC       Ed25519 + X25519 + AES-256-GCM");
    mvprintw(innerRow++, boxX + 3, "Layer 1: LATTICE PQC   ML-KEM-768 (Kyber) + ML-DSA-65");
    mvprintw(innerRow++, boxX + 3, "Layer 2: HASH PQC      SLH-DSA-128s (SPHINCS+)");
    mvprintw(innerRow++, boxX + 3, "Layer 3: ONE-TIME PAD  Vernam Cipher (XOR)");
    mvprintw(innerRow++, boxX + 3, "Layer 4: QKD           BB84/E91 (future hardware)");
    
    row += 12;
    mvprintw(row++, boxX + 3, "[B] Back to Dashboard");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawSettings() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "SETTINGS");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 60;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 14, boxW, "Configuration");
    
    int innerRow = row + 2;
	    const char* settings[] = {
	        "[1] Network Settings",
	        "[2] Privacy Settings",
	        "[3] Model Settings",
	        "[4] Display Settings",
	        "[5] Security Settings",
	        "[6] Wallet Settings",
	        "[7] Export Configuration",
	        "[8] Import Configuration",
	        "[9] Reset to Defaults",
	        "[T] Tor Bridges (paste obfs4)",
	        "[W] NAAN Site Allowlist (clearnet/onion)"
	    };
	    
	    for (int i = 0; i < 11; i++) {
	        if (i == menuSelection) {
	            attron(A_REVERSE);
	        }
	        mvprintw(innerRow++, boxX + 5, "%s", settings[i]);
	        if (i == menuSelection) {
            attroff(A_REVERSE);
        }
    }
    
	    row += 16;
	    mvprintw(row++, boxX + 3, "[Up/Down] Navigate    [Enter] Select    [T] Bridges    [W] Site Allowlist    [B] Back");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawMining() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "MINING ACTIVITY");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 76;
    int boxX = (COLS - boxW) / 2;
    
    centerText(row++, "--------------------------------------------------------------------------------");
    
    row++;
    mvprintw(row, boxX + 3, "Progress: ");
    int barX = boxX + 13;
    for (int i = 0; i < 45; i++) {
        if (i < static_cast<int>(state.miningProgress * 45)) {
            attron(COLOR_PAIR(1));
            mvprintw(row, barX + i, "#");
            attroff(COLOR_PAIR(1));
        } else {
            mvprintw(row, barX + i, " ");
        }
    }
    mvprintw(row++, barX + 46, "%d%% Processing Knowledge", static_cast<int>(state.miningProgress * 100));
    
    row += 2;
    drawBox(row, boxX, 10, boxW, "Recent Contributions");
    
    int innerRow = row + 2;
    if (state.recentContributions.empty()) {
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "No contributions yet.");
        mvprintw(innerRow++, boxX + 3, "Start contributing knowledge to earn NGT!");
        attroff(COLOR_PAIR(2));
    } else {
        for (const auto& contrib : state.recentContributions) {
            attron(COLOR_PAIR(1));
            mvprintw(innerRow, boxX + 3, "[%s]", contrib.time.c_str());
            attroff(COLOR_PAIR(1));
            mvprintw(innerRow, boxX + 15, "%s: %s", contrib.type.c_str(), contrib.name.c_str());
            attron(COLOR_PAIR(1));
            mvprintw(innerRow, boxX + 55, "+%.1f NGT", static_cast<double>(contrib.rewardAtoms) / 100000000.0);
            attroff(COLOR_PAIR(1));
            innerRow++;
        }
    }
    
    row += 12;
    mvprintw(row++, boxX + 3, "[C] Contribute    [V] Validate    [B] Back");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawModelLoader() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "LOAD AI MODEL");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 70;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 6, boxW, "Model Directory");

    int innerRow = row + 2;
    std::string homeDir = std::getenv("HOME") ? std::getenv("HOME") : ".";
    std::string userPath = homeDir + "/.synapsenet/models";
    std::string projectPath = std::filesystem::current_path().string() + "/models";
    std::vector<std::string> thirdPartyCandidates;
    std::filesystem::path cur = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        std::filesystem::path p = cur / "third_party/llama.cpp/models";
        if (std::filesystem::exists(p)) thirdPartyCandidates.push_back(p.string());
        if (cur == cur.root_path()) break;
        cur = cur.parent_path();
    }
    printClippedLine(innerRow++, boxX + 3, boxW - 6, "Path: " + truncStart(userPath, boxW - 12));
    printClippedLine(innerRow++, boxX + 3, boxW - 6, "Also scanning: " + truncStart(projectPath, boxW - 18));
    for (const auto &tp : thirdPartyCandidates) {
        printClippedLine(innerRow++, boxX + 3, boxW - 6, "Also scanning: " + truncStart(tp, boxW - 18));
    }
    mvprintw(innerRow++, boxX + 3, "Supported: .gguf files (llama.cpp format)");

    row += 7;
    drawBox(row, boxX, 12, boxW, "Available Models");

    innerRow = row + 2;

    state.availableModels.clear();
    std::vector<std::string> scanDirs = {userPath, projectPath};
    for (const auto &tp : thirdPartyCandidates) scanDirs.push_back(tp);
    for (const auto &dir : scanDirs) {
        if (!std::filesystem::exists(dir)) continue;
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() != ".gguf") continue;
            std::string name = entry.path().filename().string();
            if (name.rfind("ggml-vocab-", 0) == 0) continue;
            state.availableModels.push_back(entry.path().string());
        }
    }

	    if (state.availableModels.empty()) {
	        attron(COLOR_PAIR(2));
	        mvprintw(innerRow++, boxX + 3, "No models found.");
	        printClippedLine(innerRow++, boxX + 3, boxW - 6, "UI downloads are disabled. Add model files manually.");
	        if (modelLoader && modelLoader->getState() == synapse::model::ModelState::ERROR) {
	            std::string err = modelLoader->getError();
	            if (err.size() > 60) err = err.substr(0, 60) + "...";
	            printClippedLine(innerRow++, boxX + 3, boxW - 6, "Error: " + truncEnd(err, boxW - 13));
        }
        mvprintw(innerRow++, boxX + 3, "");
        mvprintw(innerRow++, boxX + 3, "To add models:");
        mvprintw(innerRow++, boxX + 3, "1. mkdir -p ./models");
        mvprintw(innerRow++, boxX + 3, "2. Put .gguf in ./models/ (project root)");
        mvprintw(innerRow++, boxX + 3, "3. Or use ~/.synapsenet/models/");
        attroff(COLOR_PAIR(2));
    } else {
        for (size_t i = 0; i < state.availableModels.size() && i < 12; i++) {
            std::string name = std::filesystem::path(state.availableModels[i]).filename().string();
            if (static_cast<int>(i) == menuSelection) {
                attron(A_REVERSE | COLOR_PAIR(1));
            }
            mvprintw(innerRow++, boxX + 3, "[%zu] %s", i + 1, name.c_str());
            if (static_cast<int>(i) == menuSelection) {
                attroff(A_REVERSE | COLOR_PAIR(1));
            }
        }
    }

    row += 14;
    if (!state.availableModels.empty()) {
        printClippedLine(row++, boxX + 3, boxW - 6, "[1-9] Select Model  [Enter] Load  [R] Refresh  [B] Back");
    } else {
        printClippedLine(row++, boxX + 3, boxW - 6, "[R] Refresh  [B] Back");
    }
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawSendNGT() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "SEND NGT");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 70;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 8, boxW, "Your Balance");
    
    int innerRow = row + 2;
    std::string shortAddr = state.wallet.address.empty() ? "not created" :
        (state.wallet.address.size() > 18
            ? state.wallet.address.substr(0, 10) + "..." + state.wallet.address.substr(state.wallet.address.size() - 5)
            : state.wallet.address);
    mvprintw(innerRow++, boxX + 3, "Address:   %s", shortAddr.c_str());
    attron(COLOR_PAIR(1) | A_BOLD);
    const std::string balance = utils::Formatter::formatCurrency(state.wallet.balance);
    mvprintw(innerRow++, boxX + 3, "Available: %s", balance.c_str());
    attroff(COLOR_PAIR(1) | A_BOLD);
    const std::string pending = utils::Formatter::formatCurrency(state.wallet.pending);
    mvprintw(innerRow++, boxX + 3, "Pending:   %s", pending.c_str());
    
    row += 10;
    drawBox(row, boxX, 10, boxW, "Send Transaction");
    
    innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "To Address:");
    std::string toDisplay = state.sendToAddress;
    if (static_cast<int>(toDisplay.size()) > boxW - 8) {
        toDisplay = toDisplay.substr(toDisplay.size() - static_cast<size_t>(boxW - 8));
    }
    {
        std::string line = "> " + toDisplay + (state.walletScreen == 0 ? "_" : "");
        if (state.walletScreen == 0) attron(A_REVERSE);
        printClippedLine(innerRow++, boxX + 3, boxW - 6, line);
        if (state.walletScreen == 0) attroff(A_REVERSE);
    }
    innerRow++;
    mvprintw(innerRow++, boxX + 3, "Amount (NGT):");
    {
        std::string amt = state.sendAmountStr;
        std::string line = "> " + amt + (state.walletScreen == 1 ? "_" : "");
        if (state.walletScreen == 1) attron(A_REVERSE);
        printClippedLine(innerRow++, boxX + 3, boxW - 6, line);
        if (state.walletScreen == 1) attroff(A_REVERSE);
    }
    innerRow++;
    
    if (state.listeningPort == 0) {
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "Network offline - cannot send");
        attroff(COLOR_PAIR(2));
    } else if (state.peers.empty()) {
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "No peers connected - transaction will be local");
        attroff(COLOR_PAIR(2));
    }
    
    row += 12;
    mvprintw(row++, boxX + 3, "[Enter] Next/Send    [Tab] Switch Field    [Esc/B] Cancel");
    
    drawStatusBar();
    ::refresh();
}

void TUI::Impl::drawReceiveNGT() {
    clear();
    int row = 1;
    
    attron(COLOR_PAIR(4) | A_BOLD);
    centerText(row++, "RECEIVE NGT");
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    row++;
    int boxW = 70;
    int boxX = (COLS - boxW) / 2;
    
    drawBox(row, boxX, 12, boxW, "Your Wallet Address");
    
    int innerRow = row + 2;
    mvprintw(innerRow++, boxX + 3, "Share this address to receive NGT:");
    innerRow++;
    
    attron(COLOR_PAIR(1) | A_BOLD);
    std::string addr = state.wallet.address.empty() ? "not created" : state.wallet.address;
    auto addrLines = wrapLines(addr, boxW - 6, 3);
    for (const auto& l : addrLines) {
        printClippedLine(innerRow++, boxX + 3, boxW - 6, l);
    }
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    innerRow++;
    mvprintw(innerRow++, boxX + 3, "Network Status:");
    if (state.listeningPort > 0) {
        attron(COLOR_PAIR(1));
        mvprintw(innerRow++, boxX + 3, "  ONLINE - Port %d", state.listeningPort);
        mvprintw(innerRow++, boxX + 3, "  Ready to receive transactions");
        attroff(COLOR_PAIR(1));
    } else {
        attron(COLOR_PAIR(2));
        mvprintw(innerRow++, boxX + 3, "  OFFLINE - Cannot receive from network");
        attroff(COLOR_PAIR(2));
    }
    
    row += 14;
    mvprintw(row++, boxX + 3, "[B] Back to Wallet");
    
    drawStatusBar();
    ::refresh();
}


TUI::TUI() : impl_(std::make_unique<Impl>()) {
    impl_->initDefaultState();
}

TUI::~TUI() {
    shutdown();
}

bool TUI::init() {
    // Check if we're in a proper terminal
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return false;
    }
    
    WINDOW* w = initscr();
    if (!w) {
        return false;
    }
    
    // Check minimum terminal size
    if (LINES < 24 || COLS < 80) {
        endwin();
        return false;
    }
    
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);
        init_pair(2, COLOR_YELLOW, -1);
        init_pair(3, COLOR_RED, -1);
        init_pair(4, COLOR_CYAN, -1);
        init_pair(5, COLOR_MAGENTA, -1);
        init_pair(6, COLOR_BLUE, -1);
        init_pair(7, COLOR_WHITE, -1);
    }
    
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    clear();
    mvprintw(0, 0, "SynapseNet TUI starting... (press SPACE)");
    ::refresh();
    
    impl_->running = true;
    impl_->screen = Screen::BOOT;
    
    std::string dataDir = utils::Config::instance().getDataDir();
    if (!dataDir.empty()) {
        std::string walletPath = dataDir + "/wallet.dat";
        if (std::filesystem::exists(walletPath)) {
            impl_->state.isFirstRun = false;
        }
    }
    
    return true;
}

void TUI::run() {
    std::vector<std::string> availableModels;
    
    while (impl_->running) {
        std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
        switch (impl_->screen) {
            case Screen::BOOT:
                impl_->drawBoot();
                break;
            case Screen::KEY_BACKUP:
                impl_->drawKeyBackup();
                break;
            case Screen::INIT:
                impl_->drawInit();
                break;
            case Screen::NETWORK_DISCOVERY:
                impl_->drawNetworkDiscovery();
                break;
            case Screen::SYNCING:
                impl_->drawSyncing();
                break;
            case Screen::WEB_PROMPT:
                impl_->drawWebPrompt();
                break;
            case Screen::BRIDGE_PASTE:
                impl_->drawBridgePaste();
                break;
            case Screen::SITE_ALLOWLIST_PASTE:
                impl_->drawSiteAllowlistPaste();
                break;
            case Screen::WELCOME:
                impl_->drawWelcome();
                break;
            case Screen::WALLET_CREATE:
                impl_->drawWalletCreate();
                break;
            case Screen::WALLET_CREATED:
                impl_->drawWalletCreated();
                break;
            case Screen::WALLET_IMPORT:
                impl_->drawWalletImport();
                break;
            case Screen::CONNECTED:
                impl_->drawConnected();
                break;
            case Screen::DASHBOARD:
                impl_->drawDashboard();
                break;
            case Screen::WALLET:
                impl_->drawWallet();
                break;
            case Screen::WALLET_SEND:
                impl_->drawSendNGT();
                break;
            case Screen::WALLET_RECEIVE:
                impl_->drawReceiveNGT();
                break;
            case Screen::NETWORK:
                impl_->drawNetwork();
                break;
            case Screen::KNOWLEDGE:
                impl_->drawKnowledge();
                break;
            case Screen::PEERS:
                impl_->drawPeers();
                break;
	            case Screen::KNOWLEDGE_SUBMIT:
	                impl_->drawKnowledgeSubmit();
	                break;
	            case Screen::CODE:
	                impl_->drawCode();
	                break;
	            case Screen::CODE_SUBMIT:
	                impl_->drawCodeSubmit();
	                break;
	            case Screen::MODEL:
	                impl_->drawModelLoader();
	                break;
            case Screen::AI_CHAT:
                impl_->drawAIChat();
                break;
            case Screen::MINING:
                impl_->drawMining();
                break;
            case Screen::SETTINGS:
                impl_->drawSettings();
                break;
            case Screen::SECURITY:
                impl_->drawSecurity();
                break;
            case Screen::ATTACHED_AGENT:
                impl_->drawAttachedAgent();
                break;
            case Screen::OBSERVATORY:
                impl_->drawObservatory();
                break;
            default:
                impl_->drawDashboard();
                break;
        }
        
        int ch = getch();
        if (ch != ERR) {
            auto suppressGlobalQuickKeys = [this]() {
                switch (impl_->screen) {
                    case Screen::KEY_BACKUP:
                    case Screen::WEB_PROMPT:
                    case Screen::BRIDGE_PASTE:
                    case Screen::SITE_ALLOWLIST_PASTE:
                    case Screen::AI_CHAT:
                    case Screen::WALLET_IMPORT:
                    case Screen::WALLET_SEND:
                    case Screen::KNOWLEDGE_SUBMIT:
                    case Screen::CODE_SUBMIT:
                        return true;
                    default:
                        return false;
                }
            };
            // Global quick-screen keys are disabled on modal and text-entry screens.
            if (!suppressGlobalQuickKeys()) {
                if (ch == 'p' || ch == 'P') {
                    impl_->screen = Screen::PEERS;
                } else if (ch == 'k' || ch == 'K') {
                    impl_->screen = Screen::KNOWLEDGE;
                } else if (ch == 'm' || ch == 'M') {
                    impl_->screen = Screen::MINING;
                } else if (ch == 'n' || ch == 'N') {
                    impl_->screen = Screen::NETWORK;
                } else if (ch == 'd' || ch == 'D') {
                    impl_->screen = Screen::ATTACHED_AGENT;
                    impl_->attachedAgentPage = primary_ui::MiningPage::LOGS_DIAGNOSTICS;
                    impl_->attachedAgentLogFilter = 1;
                    impl_->attachedAgentLogScroll = 0;
                }
            }
            if (ch == 3) {
                impl_->running = false;
                break;
            }
            
            if (impl_->inputHandler) {
                impl_->inputHandler(ch);
            }
            
            if (impl_->screen == Screen::KEY_BACKUP) {
                if (impl_->state.startupKeyDataAvailable) {
                    if (impl_->state.startupKeyBackupSaved) {
                        if (ch == '\n' || ch == KEY_ENTER || ch == ' ' || ch == 27 || ch == 'y' || ch == 'Y' || ch == 'n' || ch == 'N') {
                            impl_->screen = Screen::INIT;
                            impl_->initStep = 0;
                        }
                    } else if (ch == 'y' || ch == 'Y') {
                        std::string backupPath;
                        if (!impl_->saveStartupKeyBackup(backupPath)) {
                            impl_->state.startupKeyPromptMessage = "Backup write failed. Check data directory permissions.";
                        }
                    } else if (ch == 'n' || ch == 'N' || ch == 27) {
                        impl_->screen = Screen::INIT;
                        impl_->initStep = 0;
                    }
                } else if (ch == '\n' || ch == KEY_ENTER || ch == ' ' || ch == 27 || ch == 'y' || ch == 'Y' || ch == 'n' || ch == 'N') {
                    impl_->screen = Screen::INIT;
                    impl_->initStep = 0;
                }
            } else if (impl_->screen == Screen::WEB_PROMPT) {
                const bool torOnlyMode = synapse::utils::Config::instance().getBool("agent.tor.required", true);

                if (ch == 27) {
                    impl_->setSecureWebDefaults();
                    impl_->webPromptBridgeMode = 0;
                    impl_->applyWebPromptAndContinue();
                } else if (ch == 't' || ch == 'T') {
                    impl_->setSecureWebDefaults();
                    impl_->webPromptBridgeMode = 0;
                    impl_->applyWebPromptAndContinue();
                } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
                    if (impl_->webPromptStep == 0) {
                        {
                            std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                            impl_->state.webInjectEnabled = true;
                            impl_->state.webOnionEnabled = false;
                            impl_->state.webTorForClearnet = false;
                            impl_->webPromptStep = 1;
                        }
                    } else if (impl_->webPromptStep == 1) {
                        std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                        impl_->state.webOnionEnabled = true;
                        impl_->webPromptStep = 2;
                    } else if (impl_->webPromptStep == 2) {
                        {
                            std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                            impl_->state.webTorForClearnet = true;
                        }
                        impl_->webPromptStep = 3;
                    } else {
                        if (impl_->webPromptBridgeMode == 1) {
                            impl_->beginBridgePaste(true, Screen::WEB_PROMPT);
                        } else {
                            impl_->applyWebPromptAndContinue();
                        }
                    }
                } else if (ch == 'y' || ch == 'Y') {
                    if (impl_->webPromptStep == 0) {
                        std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                        impl_->state.webInjectEnabled = true;
                        impl_->state.webOnionEnabled = false;
                        impl_->state.webTorForClearnet = false;
                        impl_->webPromptStep = 1;
                    } else if (impl_->webPromptStep == 1) {
                        std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                        impl_->state.webOnionEnabled = true;
                        impl_->webPromptStep = 2;
                    } else if (impl_->webPromptStep == 2) {
                        {
                            std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                            impl_->state.webTorForClearnet = true;
                        }
                        impl_->webPromptStep = 3;
                    } else {
                        if (impl_->webPromptBridgeMode == 1) {
                            impl_->beginBridgePaste(true, Screen::WEB_PROMPT);
                        } else {
                            impl_->applyWebPromptAndContinue();
                        }
                    }
                } else if (impl_->webPromptStep == 3 && (ch == '1' || ch == '2' || ch == '3')) {
                    if (ch == '1') impl_->webPromptBridgeMode = 0;
                    else if (ch == '2') impl_->webPromptBridgeMode = 1;
                    else impl_->webPromptBridgeMode = 2;
                } else if (impl_->webPromptStep == 3 && (ch == 'p' || ch == 'P')) {
                    impl_->beginBridgePaste(false, Screen::WEB_PROMPT);
                } else if (impl_->webPromptStep == 3 && (ch == 'b' || ch == 'B')) {
                    impl_->webPromptBridgeMode = (impl_->webPromptBridgeMode + 1) % 3;
                } else if (impl_->webPromptStep == 3 && (ch == 'o' || ch == 'O')) {
                    impl_->webPromptBridgeMode = 1;
                } else if (impl_->webPromptStep == 3 && (ch == 'e' || ch == 'E')) {
                    impl_->webPromptBridgeMode = 2;
                } else if (impl_->webPromptStep == 3 && (ch == 'd' || ch == 'D')) {
                    impl_->webPromptBridgeMode = 0;
                } else if (impl_->webPromptStep == 3 && (ch == KEY_LEFT || ch == KEY_RIGHT)) {
                    int delta = (ch == KEY_LEFT) ? -1 : 1;
                    int next = impl_->webPromptBridgeMode + delta;
                    if (next < 0) next = 2;
                    if (next > 2) next = 0;
                    impl_->webPromptBridgeMode = next;
                } else if (ch == 'n' || ch == 'N') {
                    if (impl_->webPromptStep == 0) {
                        {
                            std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                            impl_->state.webInjectEnabled = false;
                            impl_->state.webOnionEnabled = false;
                            impl_->state.webTorForClearnet = false;
                        }
                        impl_->applyWebPromptAndContinue();
                    } else if (impl_->webPromptStep == 1) {
                        std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                        impl_->state.webOnionEnabled = false;
                        impl_->webPromptStep = 2;
                    } else if (impl_->webPromptStep == 2) {
                        {
                            std::lock_guard<std::mutex> lock(impl_->state.webMutex);
                            impl_->state.webTorForClearnet = torOnlyMode ? true : false;
                        }
                        impl_->webPromptStep = 3;
                    } else {
                        impl_->webPromptBridgeMode = 0;
                        impl_->applyWebPromptAndContinue();
                    }
                }
	            } else if (impl_->screen == Screen::BRIDGE_PASTE) {
	                if (ch == 27) {
	                    const bool shouldContinue = impl_->bridgePasteContinueAfter;
	                    impl_->bridgePasteContinueAfter = false;
	                    impl_->bridgePasteInvalid = 0;
	                    impl_->bridgePasteLines.clear();
	                    impl_->bridgePasteLine.clear();
	                    impl_->bridgePasteStatus.clear();
	                    if (shouldContinue) {
	                        impl_->applyWebPromptAndContinue();
	                    } else {
	                        impl_->screen = impl_->bridgePasteReturnScreen;
	                    }
	                } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
	                    if (impl_->bridgePasteLine.empty()) {
	                        const auto bridges = synapse::core::sanitizeAndDedupeObfs4BridgeLines(impl_->bridgePasteLines);
	                        if (bridges.empty()) {
	                            if (impl_->bridgePasteContinueAfter) {
	                                impl_->applyWebPromptAndContinue();
	                            } else {
	                                impl_->screen = impl_->bridgePasteReturnScreen;
	                            }
	                        } else {
	                            const std::string dataDir = synapse::utils::Config::instance().getDataDir();
	                            const std::string outPath = dataDir + "/tor/bridges.obfs4.txt";
	                            try {
	                                std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());
	                                std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
	                                if (!f.is_open()) {
	                                    impl_->bridgePasteStatus = "Write failed: could not open bridges file";
	                                    updateOperationStatus("Tor bridges", "ERROR", "open_failed");
	                                } else {
	                                    for (const auto& line : bridges) {
	                                        f << line << "\n";
	                                    }
	                                    f.close();
	                                    updateOperationStatus("Tor bridges", "SUCCESS",
	                                                         "saved=" + std::to_string(bridges.size()));
	                                    impl_->bridgePasteStatus =
	                                        "Saved " + std::to_string(bridges.size()) + " obfs4 bridge(s).";
	                                    if (impl_->bridgePasteContinueAfter) {
	                                        impl_->applyWebPromptAndContinue();
	                                    } else {
	                                        impl_->screen = impl_->bridgePasteReturnScreen;
	                                    }
	                                }
	                            } catch (const std::exception& e) {
	                                impl_->bridgePasteStatus = std::string("Write failed: ") + e.what();
	                                updateOperationStatus("Tor bridges", "ERROR", "write_failed");
	                            }
	                        }
	                    } else {
	                        const auto parsed = synapse::core::normalizeObfs4BridgeLine(impl_->bridgePasteLine);
	                        if (parsed.status == synapse::core::TorBridgeLineParseStatus::Valid) {
                            impl_->bridgePasteLines.push_back(parsed.normalized);
                            impl_->bridgePasteStatus = "Accepted bridge line.";
                        } else if (parsed.status == synapse::core::TorBridgeLineParseStatus::Invalid) {
                            impl_->bridgePasteInvalid += 1;
                            impl_->bridgePasteStatus = "Invalid bridge line: " + parsed.error;
                        }
                        impl_->bridgePasteLine.clear();
                    }
                } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                    if (!impl_->bridgePasteLine.empty()) {
                        impl_->bridgePasteLine.pop_back();
                    }
	                } else if (ch >= 32 && ch < 127) {
	                    impl_->bridgePasteLine.push_back(static_cast<char>(ch));
	                }
            } else if (impl_->screen == Screen::SITE_ALLOWLIST_PASTE) {
                if (ch == 27 || ch == 'b' || ch == 'B') {
                    impl_->screen = impl_->siteAllowlistReturnScreen;
                } else if (ch == 'c' || ch == 'C') {
                    impl_->siteAllowlistTarget = 0;
                } else if (ch == 'o' || ch == 'O') {
                    impl_->siteAllowlistTarget = 1;
                } else if (ch == 'r' || ch == 'R') {
                    std::string loadMessage;
                    if (!impl_->loadSiteAllowlistFromConfig(&loadMessage)) {
                        impl_->siteAllowlistStatus = "Reload failed: " + loadMessage;
                        updateOperationStatus("NAAN site allowlist", "ERROR", "reload_failed");
                    } else {
                        impl_->siteAllowlistStatus = loadMessage.empty() ? "Reloaded from config." : loadMessage;
                        updateOperationStatus("NAAN site allowlist", "SUCCESS", "reload");
                    }
                } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                    if (impl_->siteAllowlistLine.empty()) {
                        std::string saveError;
                        if (!impl_->saveSiteAllowlistToConfig(&saveError)) {
                            impl_->siteAllowlistStatus = "Save failed: " + saveError;
                            updateOperationStatus("NAAN site allowlist", "ERROR", "save_failed");
                        } else {
                            impl_->siteAllowlistStatus =
                                "Saved clearnet=" + std::to_string(impl_->siteAllowlistClearnet.size()) +
                                " onion=" + std::to_string(impl_->siteAllowlistOnion.size());
                            updateOperationStatus("NAAN site allowlist", "SUCCESS",
                                                 "saved clearnet=" + std::to_string(impl_->siteAllowlistClearnet.size()) +
                                                 " onion=" + std::to_string(impl_->siteAllowlistOnion.size()));
                            impl_->screen = impl_->siteAllowlistReturnScreen;
                        }
                    } else {
                        auto& targetList = (impl_->siteAllowlistTarget == 0)
                            ? impl_->siteAllowlistClearnet
                            : impl_->siteAllowlistOnion;
                        std::string rawRule = impl_->siteAllowlistLine;
                        bool removeRule = false;
                        if (!rawRule.empty() && rawRule[0] == '-') {
                            removeRule = true;
                            rawRule = trimAsciiCopy(rawRule.substr(1));
                        }

                        std::string parseError;
                        const std::string normalized =
                            normalizeSiteRuleInput(rawRule, impl_->siteAllowlistTarget == 1, &parseError);
                        if (normalized.empty()) {
                            impl_->siteAllowlistInvalid += 1;
                            impl_->siteAllowlistStatus = "Invalid rule: " + parseError;
                        } else if (removeRule) {
                            auto it = std::find(targetList.begin(), targetList.end(), normalized);
                            if (it != targetList.end()) {
                                targetList.erase(it);
                                impl_->siteAllowlistStatus = "Removed rule: " + normalized;
                            } else {
                                impl_->siteAllowlistStatus = "Rule not found: " + normalized;
                            }
                        } else {
                            targetList.push_back(normalized);
                            dedupeStringsInPlace(targetList);
                            impl_->siteAllowlistStatus = "Accepted rule: " + normalized;
                        }
                        impl_->siteAllowlistLine.clear();
                    }
                } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                    if (!impl_->siteAllowlistLine.empty()) {
                        impl_->siteAllowlistLine.pop_back();
                    }
                } else if (ch >= 32 && ch < 127) {
                    impl_->siteAllowlistLine.push_back(static_cast<char>(ch));
                }
            } else if (impl_->screen == Screen::AI_CHAT) {
                auto resetInputHistoryBrowse = [&]() {
                    if (!impl_->inputHistoryBrowsing) return;
                    impl_->inputHistoryBrowsing = false;
                    impl_->inputHistoryDraft.clear();
                    impl_->inputHistoryIndex = impl_->inputHistory.size();
                };

                if (ch == 'x' || ch == 'X' || ch == 24) {
                    if (impl_->state.aiGenerating) {
                        impl_->aiCancelRequested.store(true);
                    }
                } else if (ch == 'c' || ch == 'C' || ch == 23) {
                    impl_->aiCancelRequested.store(true);
                    {
                        std::lock_guard<std::mutex> lock(impl_->state.chatMutex);
                        impl_->state.chatHistory.clear();
                    }
                    {
                        std::lock_guard<std::mutex> lock(impl_->state.aiResponseMutex);
                        impl_->state.aiCurrentResponse.clear();
                    }
                    impl_->state.aiGenerating = false;
                } else if (ch == KEY_PPAGE) {
                    impl_->chatScrollOffset += 10;
                    if (impl_->chatScrollOffset > 0) {
                        impl_->autoScrollEnabled = false;
                    }
                } else if (ch == KEY_NPAGE) {
                    impl_->chatScrollOffset -= 10;
                    if (impl_->chatScrollOffset < 0) impl_->chatScrollOffset = 0;
                    if (impl_->chatScrollOffset == 0) {
                        impl_->autoScrollEnabled = true;
                    }
                } else if (ch == KEY_UP) {
                    if (!impl_->inputHistory.empty() && (!impl_->inputBuffer.empty() || impl_->inputHistoryBrowsing)) {
                        if (!impl_->inputHistoryBrowsing) {
                            impl_->inputHistoryBrowsing = true;
                            impl_->inputHistoryDraft = impl_->inputBuffer;
                            impl_->inputHistoryIndex = impl_->inputHistory.size();
                        }
                        if (impl_->inputHistoryIndex > 0) {
                            impl_->inputHistoryIndex--;
                            impl_->inputBuffer = impl_->inputHistory[impl_->inputHistoryIndex];
                            impl_->inputCursor = impl_->inputBuffer.size();
                        }
                    } else if (impl_->inputBuffer.empty()) {
                        impl_->chatScrollOffset += 1;
                        if (impl_->chatScrollOffset > 0) {
                            impl_->autoScrollEnabled = false;
                        }
                    }
                } else if (ch == KEY_DOWN) {
                    if (impl_->inputHistoryBrowsing) {
                        if (impl_->inputHistoryIndex + 1 < impl_->inputHistory.size()) {
                            impl_->inputHistoryIndex++;
                            impl_->inputBuffer = impl_->inputHistory[impl_->inputHistoryIndex];
                            impl_->inputCursor = impl_->inputBuffer.size();
                        } else {
                            impl_->inputHistoryBrowsing = false;
                            impl_->inputBuffer = impl_->inputHistoryDraft;
                            impl_->inputCursor = impl_->inputBuffer.size();
                            impl_->inputHistoryDraft.clear();
                            impl_->inputHistoryIndex = impl_->inputHistory.size();
                        }
                    } else if (impl_->inputBuffer.empty() && impl_->chatScrollOffset > 0) {
                        impl_->chatScrollOffset -= 1;
                        if (impl_->chatScrollOffset == 0) {
                            impl_->autoScrollEnabled = true;
                        }
                    }
                } else if (ch == KEY_HOME) {
                    if (!impl_->inputBuffer.empty()) {
                        resetInputHistoryBrowse();
                        impl_->inputCursor = 0;
                    } else {
                        impl_->chatScrollOffset = 1000000;
                        impl_->autoScrollEnabled = false;
                    }
                } else if (ch == KEY_END) {
                    if (!impl_->inputBuffer.empty()) {
                        resetInputHistoryBrowse();
                        impl_->inputCursor = impl_->inputBuffer.size();
                    } else {
                        impl_->chatScrollOffset = 0;
                        impl_->autoScrollEnabled = true;
                    }
                } else if (ch == '\n' || ch == KEY_ENTER) {
                    if (!impl_->inputBuffer.empty()) {
                        resetInputHistoryBrowse();
                        impl_->chatScrollOffset = 0;
                        impl_->autoScrollEnabled = true;
                        impl_->aiCancelRequested.store(false);
                        if (impl_->modelLoader && impl_->modelLoader->isLoaded()) {
                            if (impl_->inputHistory.empty() || impl_->inputHistory.back() != impl_->inputBuffer) {
                                impl_->inputHistory.push_back(impl_->inputBuffer);
                                if (impl_->inputHistory.size() > 100) {
                                    impl_->inputHistory.erase(impl_->inputHistory.begin());
                                }
                            }
                            impl_->inputHistoryIndex = impl_->inputHistory.size();
                            ChatMessage userMsg;
                            userMsg.role = "user";
                            userMsg.content = impl_->inputBuffer;
                            {
                                std::lock_guard<std::mutex> lock(impl_->state.chatMutex);
                                impl_->state.chatHistory.push_back(userMsg);
                            }
                            impl_->autoScrollEnabled = true;
                            impl_->chatScrollOffset = 0;
                            impl_->state.aiGenerating = true;
                            impl_->drawAIChat();

                            std::string prompt = impl_->inputBuffer;
                            impl_->inputBuffer.clear();
                            impl_->inputCursor = 0;

                            auto pimpl = impl_.get();
                            std::thread([pimpl, prompt]() {
                                {
                                    std::lock_guard<std::mutex> lock(pimpl->state.webMutex);
                                    pimpl->state.webSearching = false;
                                    pimpl->state.webLastError.clear();
                                    pimpl->state.webLastResults = 0;
                                    pimpl->state.webLastClearnetResults = 0;
                                    pimpl->state.webLastDarknetResults = 0;
                                }

                                synapse::model::GenerationParams params;
                                params.temperature = 0.7f;
                                params.maxTokens = 2048;
                                std::string out;
                                try {
                                    pimpl->modelLoader->generateStream(prompt, [pimpl, &out](const std::string& token) {
                                        if (pimpl->aiCancelRequested.load()) return false;
                                        out += token;
                                        {
                                            std::lock_guard<std::mutex> lock(pimpl->state.aiResponseMutex);
                                            pimpl->state.aiCurrentResponse += token;
                                        }
                                        if (pimpl->autoScrollEnabled) {
                                            pimpl->chatScrollOffset = 0;
                                        }
                                        return !pimpl->aiCancelRequested.load();
                                    }, params);
                                } catch (...) {
                                    out = "[Error during model generation]";
                                }

                                bool cancelled = pimpl->aiCancelRequested.exchange(false);
                                if (cancelled) {
                                    if (out.empty()) out = "[Generation cancelled]";
                                    else out += "\n[Generation cancelled]";
                                }

                                ChatMessage aiMsg;
                                aiMsg.role = "assistant";
                                aiMsg.content = out;
                                if (aiMsg.content.empty()) aiMsg.content = "[No output from model]";
                                {
                                    std::lock_guard<std::mutex> lock(pimpl->state.chatMutex);
                                    pimpl->state.chatHistory.push_back(aiMsg);
                                }
                                {
                                    std::lock_guard<std::mutex> lock(pimpl->state.aiResponseMutex);
                                    pimpl->state.aiCurrentResponse.clear();
                                }
                                if (pimpl->autoScrollEnabled) {
                                    pimpl->chatScrollOffset = 0;
                                }
                                pimpl->state.aiGenerating = false;
                            }).detach();
                        } else {
                            std::string msg = "No model loaded. Add a .gguf under ~/.synapsenet/models or ./models.";
                            {
                                std::lock_guard<std::mutex> lock(impl_->state.chatMutex);
                                if (impl_->state.chatHistory.empty() ||
                                    impl_->state.chatHistory.back().role != "assistant" ||
                                    impl_->state.chatHistory.back().content != msg) {
                                    impl_->state.chatHistory.push_back(ChatMessage{"assistant", msg});
                                }
                            }
                        }
                    }
                } else if (ch == KEY_LEFT) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor > 0) impl_->inputCursor--;
                } else if (ch == KEY_RIGHT) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor < impl_->inputBuffer.size()) impl_->inputCursor++;
                } else if (ch == KEY_DC) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor < impl_->inputBuffer.size()) {
                        impl_->inputBuffer.erase(impl_->inputCursor, 1);
                    }
                } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor > 0 && !impl_->inputBuffer.empty()) {
                        impl_->inputBuffer.erase(impl_->inputCursor - 1, 1);
                        impl_->inputCursor--;
                    }
                } else if (ch == 1) {
                    resetInputHistoryBrowse();
                    impl_->inputCursor = 0;
                } else if (ch == 2) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor > 0) impl_->inputCursor--;
                } else if (ch == 5) {
                    resetInputHistoryBrowse();
                    impl_->inputCursor = impl_->inputBuffer.size();
                } else if (ch == 6) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor < impl_->inputBuffer.size()) impl_->inputCursor++;
                } else if (ch == 11) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor < impl_->inputBuffer.size()) {
                        impl_->inputBuffer.erase(impl_->inputCursor);
                    }
                } else if (ch == 12) {
                    resetInputHistoryBrowse();
                    impl_->inputBuffer.clear();
                    impl_->inputCursor = 0;
                } else if (ch == 21) {
                    resetInputHistoryBrowse();
                    if (impl_->inputCursor > 0) {
                        impl_->inputBuffer.erase(0, impl_->inputCursor);
                        impl_->inputCursor = 0;
                    }
                } else if (ch == 27) {
                    if (impl_->state.aiGenerating) {
                        impl_->aiCancelRequested.store(true);
                    }
                    resetInputHistoryBrowse();
                    impl_->screen = Screen::DASHBOARD;
                    impl_->inputBuffer.clear();
                    impl_->inputCursor = 0;
                } else if (ch >= 32 && ch < 127) {
                    resetInputHistoryBrowse();
                    char c = static_cast<char>(ch);
                    if (impl_->inputCursor > impl_->inputBuffer.size()) impl_->inputCursor = impl_->inputBuffer.size();
                    impl_->inputBuffer.insert(impl_->inputCursor, 1, c);
                    impl_->inputCursor++;
                }
            } else if (impl_->screen == Screen::MODEL) {
                if (ch == 'r' || ch == 'R') {
                    impl_->menuSelection = 0;
                }

                if (ch >= '1' && ch <= '9') {
                    int idx = ch - '1';
                    if (idx < static_cast<int>(impl_->state.availableModels.size())) {
                        impl_->menuSelection = idx;
                    }
                } else if (ch == '\n' || ch == KEY_ENTER) {
                    if (impl_->menuSelection < static_cast<int>(impl_->state.availableModels.size())) {
                            impl_->state.modelPath = impl_->state.availableModels[impl_->menuSelection];
                            impl_->state.model.name = std::filesystem::path(impl_->state.modelPath).filename().string();
                            impl_->state.model.status = "LOADING";
                            impl_->state.model.progress = 0.0;
                            // switch to chat screen while loading in background
                            impl_->screen = Screen::AI_CHAT;
                            impl_->chatScrollOffset = 0;

                            // capture impl_ safely for background loading
                            auto pimpl = impl_.get();
                            std::string pathCopy = impl_->state.modelPath;
                            std::thread([pimpl, pathCopy]() {
                                bool ok = false;
                                try {
                                    if (pimpl->modelLoader) {
                                        ok = pimpl->modelLoader->load(pathCopy);
                                    }
                                } catch (...) {
                                    ok = false;
                                }
                                if (ok) {
                                    pimpl->state.model.status = "ACTIVE";
                                    pimpl->state.model.progress = 1.0;
                                    synapse::utils::Config::instance().set("model.last_path", pathCopy);
                                    std::string cfgPath = synapse::utils::Config::instance().getDataDir() + "/synapsenet.conf";
                                    synapse::utils::Config::instance().save(cfgPath);
                                } else {
                                    pimpl->state.model.status = "ERROR";
                                    pimpl->state.model.progress = 0.0;
                                }
                            }).detach();
                        }
                } else if (ch == 'b' || ch == 'B' || ch == 27) {
                    impl_->screen = Screen::DASHBOARD;
                } else if (ch == KEY_UP) {
                    if (impl_->menuSelection > 0) impl_->menuSelection--;
                } else if (ch == KEY_DOWN) {
                    if (impl_->menuSelection < static_cast<int>(impl_->state.availableModels.size()) - 1) {
                        impl_->menuSelection++;
                    }
                }
            } else if (impl_->screen == Screen::WALLET_IMPORT) {
                if (ch == 27) {
                    impl_->inputBuffer.clear();
                    impl_->screen = Screen::WELCOME;
                } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                    if (!impl_->inputBuffer.empty()) impl_->inputBuffer.pop_back();
                } else if (ch == '\n' || ch == KEY_ENTER) {
                    std::string mnemonic = impl_->inputBuffer;
                    impl_->inputBuffer.clear();

                    std::string dataDir = utils::Config::instance().getDataDir();
                    if (dataDir.empty()) dataDir = ".";
                    std::filesystem::create_directories(dataDir);
                    std::string walletPath = dataDir + "/wallet.dat";

                    crypto::Keys keys;
                    bool ok = false;
                    try {
                        ok = keys.fromMnemonic(mnemonic);
                        if (ok) {
                            std::error_code ec;
                            std::filesystem::remove(walletPath, ec);
                            ok = keys.save(walletPath, "");
                        }
                    } catch (...) {
                        ok = false;
                    }

                    if (ok) {
                        impl_->state.wallet.address = keys.getAddress();
                        impl_->state.wallet.balance = 0;
                        impl_->state.nodeId = impl_->state.wallet.address.empty()
                            ? "node_00000000"
                            : "node_" + impl_->state.wallet.address.substr(0, 8);
                        impl_->state.isFirstRun = false;
                        impl_->screen = Screen::WALLET_CREATED;
                    } else {
                        impl_->state.wallet.address.clear();
                        impl_->screen = Screen::WELCOME;
                    }
                } else if (ch >= 32 && ch < 127) {
                    impl_->inputBuffer += static_cast<char>(ch);
                }
	            } else if (impl_->screen == Screen::SETTINGS) {
	                if (ch == KEY_UP) {
	                    if (impl_->menuSelection > 0) impl_->menuSelection--;
	                } else if (ch == KEY_DOWN) {
	                    impl_->menuSelection++;
	                    if (impl_->menuSelection > 10) impl_->menuSelection = 10;
	                } else if (ch == '\n' || ch == KEY_ENTER) {
	                    switch (impl_->menuSelection) {
	                        case 0: impl_->screen = Screen::NETWORK; break;
	                        case 1: impl_->screen = Screen::KNOWLEDGE; break;
	                        case 2: impl_->screen = Screen::MODEL; impl_->menuSelection = 0; break;
	                        case 3: impl_->screen = Screen::DASHBOARD; break;
	                        case 4: impl_->screen = Screen::SECURITY; break;
	                        case 5: impl_->screen = Screen::WALLET; break;
	                        case 9: impl_->beginBridgePaste(false, Screen::SETTINGS); break;
	                        case 10: impl_->beginSiteAllowlistPaste(Screen::SETTINGS); break;
	                        default: impl_->screen = Screen::DASHBOARD; break;
	                    }
	                } else if (ch == 't' || ch == 'T') {
	                    impl_->beginBridgePaste(false, Screen::SETTINGS);
	                } else if (ch == 'w' || ch == 'W') {
	                    impl_->beginSiteAllowlistPaste(Screen::SETTINGS);
	                } else if (ch == 'b' || ch == 'B' || ch == 27) {
	                    impl_->screen = Screen::DASHBOARD;
	                }
            } else if (impl_->screen == Screen::WALLET) {
                if (ch == '1') {
                    impl_->state.sendToAddress.clear();
                    impl_->state.sendAmount = 0.0;
                    impl_->state.sendAmountStr.clear();
                    impl_->state.walletScreen = 0;
                    impl_->screen = Screen::WALLET_SEND;
                } else if (ch == '2') {
                    impl_->screen = Screen::WALLET_RECEIVE;
                } else if (ch == 'b' || ch == 'B' || ch == 27) {
                    impl_->screen = Screen::DASHBOARD;
                }
            } else if (impl_->screen == Screen::WALLET_SEND) {
                if (ch == '\n' || ch == KEY_ENTER) {
                    if (impl_->state.walletScreen == 0) {
                        if (!impl_->state.sendToAddress.empty()) {
                            impl_->state.walletScreen = 1;
                        }
                    } else {
                        double amt = 0.0;
                        try { amt = std::stod(impl_->state.sendAmountStr); } catch (...) { amt = 0.0; }
                        impl_->state.sendAmount = amt;
                        if (!impl_->state.sendToAddress.empty() && impl_->state.sendAmount > 0.0) {
                            if (impl_->commandHandler) {
                                std::ostringstream oss;
                                oss << "send " << impl_->state.sendToAddress << " " << impl_->state.sendAmountStr;
                                impl_->commandHandler(oss.str());
                            }
                            impl_->state.sendToAddress.clear();
                            impl_->state.sendAmountStr.clear();
                            impl_->state.sendAmount = 0.0;
                            impl_->state.walletScreen = 0;
                            impl_->screen = Screen::WALLET;
                        }
                    }
                } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                    if (impl_->state.walletScreen == 0) {
                        if (!impl_->state.sendToAddress.empty()) {
                            impl_->state.sendToAddress.pop_back();
                        }
                    } else {
                        if (!impl_->state.sendAmountStr.empty()) {
                            impl_->state.sendAmountStr.pop_back();
                        }
                    }
                } else if (ch == 27) {
                    impl_->screen = Screen::WALLET;
                    impl_->state.walletScreen = 0;
                } else if (ch >= 32 && ch < 127) {
                    if (ch == '\t') {
                        impl_->state.walletScreen = impl_->state.walletScreen == 0 ? 1 : 0;
                    } else if (impl_->state.walletScreen == 0) {
                        if (impl_->state.sendToAddress.length() < 96) {
                            impl_->state.sendToAddress += static_cast<char>(ch);
                        }
                    } else {
                        char c = static_cast<char>(ch);
                        if ((c >= '0' && c <= '9') || c == '.' || c == ',') {
                            if (c == ',') c = '.';
                            if (impl_->state.sendAmountStr.length() < 32) {
                                impl_->state.sendAmountStr += c;
                            }
                        }
                    }
                }
            } else if (impl_->screen == Screen::WALLET_RECEIVE) {
                if (ch == 'b' || ch == 'B' || ch == 27) {
                    impl_->screen = Screen::WALLET;
                }
            } else if (impl_->screen == Screen::MINING) {
                if (ch == 'c' || ch == 'C') {
                    impl_->state.knowledgeQuestion.clear();
                    impl_->state.knowledgeAnswer.clear();
                    impl_->state.knowledgeSource.clear();
                    impl_->state.knowledgeField = 0;
                    impl_->screen = Screen::KNOWLEDGE_SUBMIT;
                } else if (ch == 'v' || ch == 'V') {
                    // Validation/review actions are surfaced on the knowledge page.
                    impl_->screen = Screen::KNOWLEDGE;
                } else if (ch == 'b' || ch == 'B' || ch == 27) {
                    impl_->screen = Screen::DASHBOARD;
                }
            } else if (impl_->screen == Screen::KNOWLEDGE) {
                if (ch == 'c' || ch == 'C') {
                    impl_->state.knowledgeQuestion.clear();
                    impl_->state.knowledgeAnswer.clear();
                    impl_->state.knowledgeSource.clear();
                    impl_->state.knowledgeField = 0;
                    impl_->screen = Screen::KNOWLEDGE_SUBMIT;
                } else if (ch == KEY_UP) {
                    impl_->scrollOffset--;
                } else if (ch == KEY_DOWN) {
                    impl_->scrollOffset++;
                } else if (ch == KEY_PPAGE) {
                    impl_->scrollOffset -= 10;
                } else if (ch == KEY_NPAGE) {
                    impl_->scrollOffset += 10;
                } else if (ch == 'b' || ch == 'B' || ch == 27) {
                    impl_->screen = Screen::DASHBOARD;
                }
            } else if (impl_->screen == Screen::KNOWLEDGE_SUBMIT) {
                auto& field = (impl_->state.knowledgeField == 0)
                    ? impl_->state.knowledgeQuestion
                    : (impl_->state.knowledgeField == 1 ? impl_->state.knowledgeAnswer : impl_->state.knowledgeSource);

                if (ch == 27) {
                    impl_->screen = Screen::DASHBOARD;
                } else if (ch == '\t') {
                    impl_->state.knowledgeField = (impl_->state.knowledgeField + 1) % 3;
                } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                    if (!field.empty()) field.pop_back();
                } else if (ch == '\n' || ch == KEY_ENTER) {
                    if (impl_->state.knowledgeField < 2) {
                        impl_->state.knowledgeField++;
                    } else {
                        if (impl_->state.knowledgeQuestion.empty()) {
                            impl_->state.knowledgeField = 0;
                        } else if (impl_->state.knowledgeAnswer.empty()) {
                            impl_->state.knowledgeField = 1;
	                        } else {
	                            auto b64 = [](const std::string& s) -> std::string {
	                                std::vector<uint8_t> in(s.begin(), s.end());
	                                auto out = crypto::base64Encode(in);
	                                return std::string(out.begin(), out.end());
	                            };
		                            if (impl_->commandHandler) {
		                                std::ostringstream oss;
		                                oss << "poe_submit " << b64(impl_->state.knowledgeQuestion) << " "
		                                    << b64(impl_->state.knowledgeAnswer) << " " << b64(impl_->state.knowledgeSource);
		                                impl_->commandHandler(oss.str());
		                            }
		                            impl_->state.knowledgeQuestion.clear();
		                            impl_->state.knowledgeAnswer.clear();
		                            impl_->state.knowledgeSource.clear();
		                            impl_->state.knowledgeField = 0;
		                            impl_->scrollOffset = 0;
		                            impl_->screen = Screen::KNOWLEDGE;
		                        }
                    }
                } else if (ch >= 32 && ch < 127) {
                    if (field.size() < 512) field.push_back(static_cast<char>(ch));
                }
            } else if (impl_->screen == Screen::CODE) {
                if (ch == 'c' || ch == 'C') {
                    impl_->state.codeTitle.clear();
                    impl_->state.codePatchFile.clear();
                    impl_->state.codeCitations.clear();
                    impl_->state.codeField = 0;
                    impl_->screen = Screen::CODE_SUBMIT;
                } else if (ch == 'i' || ch == 'I') {
                    auto exists = [](const std::string& p) -> bool {
                        std::error_code ec;
                        return std::filesystem::exists(p, ec);
                    };

                    std::string cmd;
                    if (exists("./synapseide")) cmd = "./synapseide";
                    else if (exists("./build/synapseide")) cmd = "./build/synapseide";
                    else if (exists("./KeplerSynapseNet/build/synapseide")) cmd = "./KeplerSynapseNet/build/synapseide";
                    else cmd = "synapseide";

                    def_prog_mode();
                    endwin();
                    (void)std::system(cmd.c_str());
                    reset_prog_mode();
                    refresh();
                    keypad(stdscr, TRUE);
                    nodelay(stdscr, TRUE);
                    noecho();
                    cbreak();
                    curs_set(0);
                } else if (ch == KEY_UP) {
                    impl_->scrollOffset--;
                } else if (ch == KEY_DOWN) {
                    impl_->scrollOffset++;
                } else if (ch == KEY_PPAGE) {
                    impl_->scrollOffset -= 10;
                } else if (ch == KEY_NPAGE) {
                    impl_->scrollOffset += 10;
                } else if (ch == 'b' || ch == 'B' || ch == 27) {
                    impl_->screen = Screen::DASHBOARD;
                }
            } else if (impl_->screen == Screen::CODE_SUBMIT) {
                auto& field = (impl_->state.codeField == 0)
                    ? impl_->state.codeTitle
                    : (impl_->state.codeField == 1 ? impl_->state.codePatchFile : impl_->state.codeCitations);

                auto submit = [&]() {
                    if (impl_->state.codeTitle.empty()) {
                        showError("Title required");
                        impl_->state.codeField = 0;
                        return;
                    }
                    if (impl_->state.codePatchFile.empty()) {
                        showError("Patch file path required");
                        impl_->state.codeField = 1;
                        return;
                    }

                    std::ifstream in(impl_->state.codePatchFile, std::ios::binary);
                    if (!in) {
                        showError("Failed to read patch file");
                        impl_->state.codeField = 1;
                        return;
                    }
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    std::string patch = ss.str();
                    if (patch.empty()) {
                        showError("Patch file is empty");
                        impl_->state.codeField = 1;
                        return;
                    }
                    if (patch.size() > 65536) {
                        showError("Patch too large (max 65536 bytes)");
                        impl_->state.codeField = 1;
                        return;
                    }

                    auto b64 = [](const std::string& s) -> std::string {
                        std::vector<uint8_t> in(s.begin(), s.end());
                        auto out = crypto::base64Encode(in);
                        return std::string(out.begin(), out.end());
                    };
                    if (impl_->commandHandler) {
                        std::ostringstream oss;
                        oss << "poe_submit_code " << b64(impl_->state.codeTitle) << " " << b64(patch);
                        if (!impl_->state.codeCitations.empty()) {
                            oss << " " << b64(impl_->state.codeCitations);
                        }
                        impl_->commandHandler(oss.str());
                    }
                    impl_->state.codeTitle.clear();
                    impl_->state.codePatchFile.clear();
                    impl_->state.codeCitations.clear();
                    impl_->state.codeField = 0;
                    impl_->scrollOffset = 0;
                    impl_->screen = Screen::CODE;
                };

                if (ch == 27) {
                    impl_->screen = Screen::CODE;
                } else if (ch == '\t') {
                    impl_->state.codeField = (impl_->state.codeField + 1) % 3;
                } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                    if (!field.empty()) field.pop_back();
                } else if (ch == 19) {
                    submit();
                } else if (ch == '\n' || ch == KEY_ENTER) {
                    if (impl_->state.codeField < 2) {
                        impl_->state.codeField++;
                    } else {
                        submit();
                    }
                } else if (ch >= 32 && ch < 127) {
                    if (impl_->state.codeField == 0) {
                        if (field.size() < 256) field.push_back(static_cast<char>(ch));
                    } else if (impl_->state.codeField == 1) {
                        if (field.size() < 512) field.push_back(static_cast<char>(ch));
                    } else {
                        if (field.size() < 512) field.push_back(static_cast<char>(ch));
                    }
                }
            } else if (impl_->screen == Screen::ATTACHED_AGENT) {
                auto setPage = [&](int idx) {
                    if (idx < 0) idx = 8;
                    if (idx > 8) idx = 0;
                    int current = static_cast<int>(impl_->attachedAgentPage);
                    if (current != idx) {
                        impl_->attachedAgentPage = static_cast<primary_ui::MiningPage>(idx);
                        impl_->attachedAgentLogScroll = 0;
                    }
                };

                int pageIndex = static_cast<int>(impl_->attachedAgentPage);
                if (pageIndex < 0) pageIndex = 0;
                if (pageIndex > 8) pageIndex = 8;

                if (ch == KEY_LEFT) {
                    setPage(pageIndex - 1);
                } else if (ch == KEY_RIGHT) {
                    setPage(pageIndex + 1);
                } else if ((ch == 'o' || ch == 'O') && pageIndex == 6) {
                    impl_->screen = Screen::OBSERVATORY;
                } else if (ch == 'd' || ch == 'D') {
                    impl_->attachedAgentPage = primary_ui::MiningPage::LOGS_DIAGNOSTICS;
                    impl_->attachedAgentLogFilter = 1;
                    impl_->attachedAgentLogScroll = 0;
                } else if ((ch == 'f' || ch == 'F') && pageIndex == 5) {
                    impl_->attachedAgentLogFilter = (impl_->attachedAgentLogFilter + 1) % 6;
                    impl_->attachedAgentLogScroll = 0;
                } else if (pageIndex == 5 && ch == KEY_UP) {
                    impl_->attachedAgentLogScroll += 1;
                } else if (pageIndex == 5 && ch == KEY_DOWN) {
                    impl_->attachedAgentLogScroll -= 1;
                    if (impl_->attachedAgentLogScroll < 0) impl_->attachedAgentLogScroll = 0;
                } else if (pageIndex == 5 && ch == KEY_PPAGE) {
                    impl_->attachedAgentLogScroll += 10;
                } else if (pageIndex == 5 && ch == KEY_NPAGE) {
                    impl_->attachedAgentLogScroll -= 10;
                    if (impl_->attachedAgentLogScroll < 0) impl_->attachedAgentLogScroll = 0;
                } else if ((ch == 'e' || ch == 'E') && pageIndex == 2) {
                    std::string dataDir;
                    {
                        std::lock_guard<std::mutex> lock(impl_->state.naanMutex);
                        dataDir = impl_->state.attachedAgent.dataDir;
                    }
                    if (dataDir.empty()) dataDir = ".";
                    std::filesystem::path outPath = std::filesystem::path(dataDir) / "wallet_public_info.txt";
                    std::ofstream out(outPath.string(), std::ios::out | std::ios::trunc);
                    if (out.is_open()) {
                        out << "Address: " << impl_->state.wallet.address << "\n";
                        out.close();
                        showMessage("Wallet public info exported: " + outPath.string(), Color::GREEN);
                        pushAgentEvent(impl_->state, static_cast<uint64_t>(std::time(nullptr)), "wallet",
                                       "public info exported to " + outPath.filename().string());
                    } else {
                        showError("Failed to export wallet public info");
                    }
                } else if ((ch == 'r' || ch == 'R') && pageIndex == 4) {
                    if (impl_->commandHandler) {
                        impl_->commandHandler("naan_status");
                    }
                    pushAgentEvent(impl_->state, static_cast<uint64_t>(std::time(nullptr)), "security",
                                   "runtime self-check requested");
                } else {
                    bool handled = false;
                    const auto routed = primary_ui::routeMiningPageKey(impl_->attachedAgentPage, ch, &handled);
                    if (handled) {
                        setPage(static_cast<int>(routed));
                    } else if (ch == 'b' || ch == 'B' || ch == 27) {
                        impl_->screen = Screen::DASHBOARD;
                    }
                }
            } else if (impl_->screen == Screen::OBSERVATORY) {
                int itemCount = 0;
                {
                    std::lock_guard<std::mutex> lock(impl_->state.naanMutex);
                    itemCount = static_cast<int>(impl_->state.observatoryFeed.size());
                }

                int usableH = LINES - 2 - 3;
                if (usableH < 15) usableH = 15;
                int listH = std::min(12, std::max(8, usableH / 2));
                int detailsH = usableH - listH;
                if (detailsH < 7) {
                    detailsH = 7;
                    listH = std::max(8, usableH - detailsH);
                }
                int page = std::max(1, listH - 3);

                if (ch == KEY_UP) {
                    impl_->observatorySelection--;
                } else if (ch == KEY_DOWN) {
                    impl_->observatorySelection++;
                } else if (ch == KEY_PPAGE) {
                    impl_->observatorySelection -= page;
                } else if (ch == KEY_NPAGE) {
                    impl_->observatorySelection += page;
                } else if (ch == 'a' || ch == 'A') {
                    impl_->screen = Screen::ATTACHED_AGENT;
                } else if (ch == 'b' || ch == 'B' || ch == 27) {
                    impl_->screen = Screen::DASHBOARD;
                }

                if (itemCount <= 0) {
                    impl_->observatorySelection = 0;
                    impl_->observatoryScroll = 0;
                } else {
                    if (impl_->observatorySelection < 0) impl_->observatorySelection = 0;
                    if (impl_->observatorySelection >= itemCount) impl_->observatorySelection = itemCount - 1;
                    int maxScroll = std::max(0, itemCount - page);
                    if (impl_->observatorySelection < impl_->observatoryScroll) {
                        impl_->observatoryScroll = impl_->observatorySelection;
                    } else if (impl_->observatorySelection >= impl_->observatoryScroll + page) {
                        impl_->observatoryScroll = impl_->observatorySelection - page + 1;
                    }
                    if (impl_->observatoryScroll < 0) impl_->observatoryScroll = 0;
                    if (impl_->observatoryScroll > maxScroll) impl_->observatoryScroll = maxScroll;
                }
            } else {
                primary_ui::DashboardRoute dashboardRoute = primary_ui::DashboardRoute::NONE;
                if (impl_->screen == Screen::DASHBOARD) {
                    dashboardRoute = primary_ui::routeDashboardKey(ch);
                }

                if (dashboardRoute != primary_ui::DashboardRoute::NONE) {
                    if (dashboardRoute == primary_ui::DashboardRoute::AI_MINING) {
                        impl_->screen = Screen::ATTACHED_AGENT;
                    } else if (dashboardRoute == primary_ui::DashboardRoute::AI_QUERY) {
                        impl_->screen = Screen::AI_CHAT;
                        impl_->chatScrollOffset = 0;
                    } else if (dashboardRoute == primary_ui::DashboardRoute::QUIT) {
                        impl_->running = false;
                    }
                } else switch (ch) {
                    case 'q':
                    case 'Q':
                        if (impl_->screen == Screen::DASHBOARD) {
                            impl_->running = false;
                        } else {
                            impl_->screen = Screen::DASHBOARD;
                        }
                        break;
                    case ' ':
                        if (impl_->screen == Screen::BOOT) {
                            impl_->state.startupKeyDataLoaded = false;
                            impl_->state.startupKeyDataAvailable = false;
                            impl_->state.startupKeyBackupSaved = false;
                            impl_->state.startupKeyPromptMessage.clear();
                            impl_->state.startupKeyBackupPath.clear();
                            impl_->screen = Screen::KEY_BACKUP;
                        }
                        break;
                    case '\n':
                    case KEY_ENTER:
                        if (impl_->screen == Screen::WALLET_CREATED || impl_->screen == Screen::CONNECTED) {
                            impl_->screen = Screen::DASHBOARD;
                        }
                        break;
                    case '1':
                        if (impl_->screen == Screen::WELCOME) {
                            impl_->state.generatedSeedWords.clear();
                            impl_->state.forceNewWallet = true;
                            impl_->screen = Screen::WALLET_CREATE;
                        }
                        break;
                    case '2':
                        if (impl_->screen == Screen::WELCOME) {
                            impl_->state.forceNewWallet = false;
                            impl_->inputBuffer.clear();
                            impl_->screen = Screen::WALLET_IMPORT;
                        }
                        break;
                    case '3':
                        if (impl_->screen == Screen::WELCOME) {
                            impl_->screen = Screen::DASHBOARD;
                        } else if (impl_->screen == Screen::DASHBOARD) {
                            impl_->screen = Screen::AI_CHAT;
                            impl_->chatScrollOffset = 0;
                        }
                        break;
                    case 'a':
                    case 'A':
                        if (impl_->screen == Screen::DASHBOARD) {
                            impl_->screen = Screen::ATTACHED_AGENT;
                        }
                        break;
                    case 'y':
                    case 'Y':
                        if (impl_->screen == Screen::WALLET_CREATE) {
                            std::string mnemonic;
                            for (size_t i = 0; i < impl_->state.generatedSeedWords.size(); i++) {
                                if (i > 0) mnemonic += " ";
                                mnemonic += impl_->state.generatedSeedWords[i];
                            }
                            
                            std::string dataDir = utils::Config::instance().getDataDir();
                            if (dataDir.empty()) dataDir = ".";
                            std::filesystem::create_directories(dataDir);
                            std::string walletPath = dataDir + "/wallet.dat";
                            
                            crypto::Keys keys;
                            bool ok = false;
                            
                            if (impl_->state.forceNewWallet && std::filesystem::exists(walletPath)) {
                                std::error_code ec;
                                std::filesystem::remove(walletPath, ec);
                            }

                            if (std::filesystem::exists(walletPath) && !impl_->state.forceNewWallet) {
                                ok = keys.load(walletPath, "");
                            } else {
                                if (!mnemonic.empty()) {
                                    ok = keys.fromMnemonic(mnemonic);
                                }
                                if (!ok) {
                                    ok = keys.generate();
                                }
                                if (ok) {
                                    ok = keys.save(walletPath, "");
                                }
                            }
                            
                            if (ok) {
                                impl_->state.wallet.address = keys.getAddress();
                                impl_->state.wallet.balance = 0;
                                impl_->state.nodeId = impl_->state.wallet.address.empty()
                                    ? "node_00000000"
                                    : "node_" + impl_->state.wallet.address.substr(0, 8);
                                impl_->state.isFirstRun = false;
                                impl_->state.forceNewWallet = false;
                                impl_->screen = Screen::WALLET_CREATED;
                            } else {
                                impl_->state.wallet.address.clear();
                                impl_->state.wallet.balance = 0;
                                impl_->state.nodeId.clear();
                                impl_->state.forceNewWallet = false;
                                impl_->screen = Screen::WALLET_CREATED;
                            }
                        }
                        break;
                    case 'n':
                    case 'N':
                        if (impl_->screen == Screen::WALLET_CREATE) {
                            impl_->state.generatedSeedWords.clear();
                            impl_->state.forceNewWallet = false;
                            impl_->screen = Screen::WELCOME;
                        }
                        break;
                    case 'b':
                    case 'B':
                        impl_->screen = Screen::DASHBOARD;
                        break;
                    case 27:
                        impl_->screen = Screen::DASHBOARD;
                        break;
                }
            }
        }
        
        impl_->frameCounter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void TUI::shutdown() {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    if (impl_->running) {
        impl_->running = false;
        endwin();
    }
}

bool TUI::isRunning() const {
    return impl_->running;
}

void TUI::switchScreen(Screen screen) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->screen = screen;
}

Screen TUI::currentScreen() const {
    return impl_->screen;
}

void TUI::refresh() {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    ::refresh();
}

void TUI::updateStatus(const StatusInfo& status) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->status = status;
}

void TUI::showMessage(const std::string& msg, Color color) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    int row = LINES - 3;
    attron(COLOR_PAIR(static_cast<int>(color)));
    printClippedLine(row, 2, COLS - 3, msg);
    attroff(COLOR_PAIR(static_cast<int>(color)));
    ::refresh();
}

void TUI::showError(const std::string& err) {
    showMessage(err, Color::RED);
}

void TUI::showProgress(const std::string& label, double progress) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    int row = LINES - 4;
    mvprintw(row, 2, "%s: ", label.c_str());
    impl_->drawProgressBar(row, 2 + label.length() + 2, 40, progress, 1);
    ::refresh();
}

void TUI::updateOperationStatus(const std::string& operation, const std::string& status, const std::string& details) {
    std::lock_guard<std::mutex> lock(impl_->state.operationMutex);
    impl_->state.currentOperation.operation = operation;
    impl_->state.currentOperation.status = status;
    impl_->state.currentOperation.details = details;
    impl_->state.currentOperation.timestamp = std::time(nullptr);
    impl_->state.operationHistory.push_back(impl_->state.currentOperation);
    if (impl_->state.operationHistory.size() > 20) {
        impl_->state.operationHistory.erase(impl_->state.operationHistory.begin());
    }
    pushAgentEvent(impl_->state,
                   impl_->state.currentOperation.timestamp,
                   "runtime",
                   operation + " " + status + (details.empty() ? std::string() : (" " + details)));
}

void TUI::showRewardNotification(uint64_t amountAtoms, const std::string& reason, const std::string& entryId, const std::string& details) {
    std::ostringstream oss;
    oss << "You earned +" << std::fixed << std::setprecision(8) << (static_cast<double>(amountAtoms) / 100000000.0) << " NGT";
    oss << " for " << reason;
    if (!entryId.empty()) {
        std::string shortId = entryId.size() > 8 ? entryId.substr(0, 8) + "..." : entryId;
        oss << " (" << shortId << ")";
    }
    if (!details.empty()) {
        oss << " - " << details;
    }
    std::string msg = oss.str();
    showMessage(msg, Color::GREEN);
    appendChatMessage("system", msg);
    {
        std::lock_guard<std::mutex> lock(impl_->state.rewardMutex);
        LocalAppState::RewardNotification notif;
        notif.amountAtoms = amountAtoms;
        notif.reason = reason;
        notif.entryId = entryId;
        notif.details = details;
        notif.timestamp = std::time(nullptr);
        impl_->state.rewardHistory.push_back(notif);
        // Keep last 50 rewards
        if (impl_->state.rewardHistory.size() > 50) {
            impl_->state.rewardHistory.erase(impl_->state.rewardHistory.begin());
        }
    }
    pushAgentEvent(impl_->state, static_cast<uint64_t>(std::time(nullptr)), "reward", msg);
}

void TUI::drawBox(int y, int x, int h, int w, const std::string& title) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->drawBox(y, x, h, w, title.c_str());
}

void TUI::drawText(int y, int x, const std::string& text, Color color) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    attron(COLOR_PAIR(static_cast<int>(color)));
    mvprintw(y, x, "%s", text.c_str());
    attroff(COLOR_PAIR(static_cast<int>(color)));
}

void TUI::drawProgressBar(int y, int x, int w, double progress, Color color) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->drawProgressBar(y, x, w, progress, static_cast<int>(color));
}

void TUI::onInput(std::function<void(int)> handler) {
    impl_->inputHandler = handler;
}

void TUI::onCommand(std::function<void(const std::string&)> handler) {
    impl_->commandHandler = handler;
}

void TUI::setNetworkPort(uint16_t port) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->state.listeningPort = port;
}

void TUI::setNetworkOnline(bool online) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->state.networkOnline = online;
}

void TUI::setPeerCount(size_t count) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->state.network.totalNodes = count;
}

void TUI::updateNetworkInfo(const NetworkInfo& info) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->state.network = info;
}

void TUI::updatePeers(const std::vector<NodeInfo>& peers) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->state.peers = peers;
}

void TUI::updateModelInfo(const AIModelInfo& info) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    AIModelInfo cur = impl_->state.model;
    cur.mode = info.mode;
    cur.slotsUsed = info.slotsUsed;
    cur.slotsMax = info.slotsMax;
    cur.uptime = info.uptime;
    cur.earningsTodayAtoms = info.earningsTodayAtoms;
    cur.earningsWeekAtoms = info.earningsWeekAtoms;
    cur.earningsTotalAtoms = info.earningsTotalAtoms;
    impl_->state.model = cur;
}

void TUI::updateWalletInfo(const WalletInfo& info) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    impl_->state.wallet = info;
}

void TUI::updateKnowledgeEntries(const std::vector<KnowledgeEntrySummary>& entries) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    std::lock_guard<std::mutex> lock(impl_->state.knowledgeMutex);
    impl_->state.knowledgeEntries = entries;
}

void TUI::updateAttachedAgentStatus(const AttachedAgentStatusInfo& info) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    AttachedAgentStatusInfo prev;
    bool hadPrev = false;
    {
        std::lock_guard<std::mutex> lock(impl_->state.naanMutex);
        prev = impl_->state.attachedAgent;
        hadPrev = prev.available;
        impl_->state.attachedAgent = info;
    }

    if (!info.available) return;

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    if (!hadPrev) {
        pushAgentEvent(impl_->state, now, "runtime", "agent runtime status available");
        return;
    }

    if (prev.runtimeState != info.runtimeState) {
        pushAgentEvent(impl_->state, now, "runtime", "runtime state " + prev.runtimeState + " -> " + info.runtimeState);
    }
    if (prev.schedulerState != info.schedulerState) {
        pushAgentEvent(impl_->state, now, "runtime", "scheduler state " + prev.schedulerState + " -> " + info.schedulerState);
    }
    if (prev.routeMode != info.routeMode ||
        prev.torReachable != info.torReachable ||
        prev.torReadyForWeb != info.torReadyForWeb ||
        prev.torReadyForOnion != info.torReadyForOnion ||
        prev.torReadyForOnionService != info.torReadyForOnionService ||
        prev.torDegraded != info.torDegraded ||
        prev.torManaged != info.torManaged ||
        prev.torControlReachable != info.torControlReachable ||
        prev.torControlPort != info.torControlPort ||
        prev.onionServiceState != info.onionServiceState ||
        prev.onionAddress != info.onionAddress ||
        prev.torSeedAddress != info.torSeedAddress ||
        prev.torBootstrapPercent != info.torBootstrapPercent ||
        prev.torSocksPort != info.torSocksPort ||
        prev.torRuntimeMode != info.torRuntimeMode) {
        pushAgentEvent(impl_->state, now, "tor",
                       "route=" + info.routeMode +
                       " mode=" + info.torRuntimeMode +
                       " bootstrap=" + std::to_string(info.torBootstrapPercent) + "%" +
                       " socks_port=" + std::to_string(info.torSocksPort) +
                       " managed=" + std::string(info.torManaged ? "yes" : "no") +
                       " tor_socks=" + std::string(info.torReachable ? "yes" : "no") +
                       " tor_web=" + std::string(info.torReadyForWeb ? "yes" : "no") +
                       " tor_onion=" + std::string(info.torReadyForOnion ? "yes" : "no") +
                       " onion_service=" + std::string(info.torReadyForOnionService ? "yes" : "no") +
                       " seed=" + (info.torSeedAddress.empty() ? std::string("n/a") : info.torSeedAddress) +
                       " control=" + std::string(info.torControlReachable ? "yes" : "no") +
                       " degraded=" + (info.torDegraded ? std::string("yes") : std::string("no")));
    }
    if (prev.quarantined != info.quarantined || prev.quarantineReason != info.quarantineReason) {
        pushAgentEvent(impl_->state, now, "security",
                       "quarantine=" + std::string(info.quarantined ? "yes" : "no") +
                       " reason=" + info.quarantineReason);
    }
    if (info.pipelineSubmitted > prev.pipelineSubmitted) {
        pushAgentEvent(impl_->state, now, "pipeline",
                       "submitted +" + std::to_string(info.pipelineSubmitted - prev.pipelineSubmitted));
    }
    if (info.pipelineApproved > prev.pipelineApproved) {
        pushAgentEvent(impl_->state, now, "pipeline",
                       "approved +" + std::to_string(info.pipelineApproved - prev.pipelineApproved));
    }
    if (info.pipelineRejected > prev.pipelineRejected) {
        pushAgentEvent(impl_->state, now, "pipeline",
                       "rejected +" + std::to_string(info.pipelineRejected - prev.pipelineRejected));
    }
    if (info.redactionCount > prev.redactionCount) {
        pushAgentEvent(impl_->state, now, "security",
                       "redaction counter +" + std::to_string(info.redactionCount - prev.redactionCount));
    }
    if (info.ledgerHeight != prev.ledgerHeight) {
        pushAgentEvent(impl_->state, now, "network",
                       "ledger height " + std::to_string(prev.ledgerHeight) + " -> " + std::to_string(info.ledgerHeight));
    }
    if (prev.connectorClearnetState != info.connectorClearnetState ||
        prev.connectorTorState != info.connectorTorState ||
        prev.connectorOnionState != info.connectorOnionState) {
        pushAgentEvent(impl_->state, now, "runtime",
                       "connectors c/t/o " + info.connectorClearnetState + "/" +
                       info.connectorTorState + "/" + info.connectorOnionState);
    }
}

void TUI::updateObservatoryFeed(const std::vector<ObservatoryArtifactInfo>& items) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    std::string previousHead;
    std::string currentHead;
    std::string currentType;
    {
        std::lock_guard<std::mutex> lock(impl_->state.naanMutex);
        if (!impl_->state.observatoryFeed.empty()) {
            previousHead = impl_->state.observatoryFeed.front().hash;
        }
        impl_->state.observatoryFeed = items;

        if (impl_->state.observatoryFeed.empty()) {
            impl_->observatorySelection = 0;
            impl_->observatoryScroll = 0;
        } else {
            if (impl_->observatorySelection < 0) impl_->observatorySelection = 0;
            if (impl_->observatorySelection >= static_cast<int>(impl_->state.observatoryFeed.size())) {
                impl_->observatorySelection = static_cast<int>(impl_->state.observatoryFeed.size()) - 1;
            }
            if (impl_->observatoryScroll < 0) impl_->observatoryScroll = 0;
            currentHead = impl_->state.observatoryFeed.front().hash;
            currentType = impl_->state.observatoryFeed.front().type;
        }
    }
    if (!currentHead.empty() && currentHead != previousHead) {
        pushAgentEvent(impl_->state,
                       static_cast<uint64_t>(std::time(nullptr)),
                       "observatory",
                       "artifact " + truncEnd(currentHead, 16) + " type=" + currentType);
    }
}

void TUI::updateAgentEvents(const std::vector<AgentEventInfo>& events) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    std::lock_guard<std::mutex> lock(impl_->state.agentEventMutex);
    impl_->state.agentEvents = events;
    if (impl_->state.agentEvents.size() > 512) {
        impl_->state.agentEvents.erase(
            impl_->state.agentEvents.begin(),
            impl_->state.agentEvents.end() - 512);
    }
}

void TUI::appendChatMessage(const std::string& role, const std::string& content) {
    std::lock_guard<std::recursive_mutex> uiLock(impl_->uiStateMtx);
    ChatMessage msg;
    msg.role = role;
    msg.content = content;
    {
        std::lock_guard<std::mutex> lock(impl_->state.chatMutex);
        impl_->state.chatHistory.push_back(msg);
    }
    if (impl_->autoScrollEnabled) {
        impl_->chatScrollOffset = 0;
    }
}

std::string TUI::prompt(const std::string& message) {
    return "";
}

bool TUI::confirm(const std::string& message) {
    return false;
}

int TUI::menu(const std::string& title, const std::vector<std::string>& options) {
    return 0;
}

}
}
