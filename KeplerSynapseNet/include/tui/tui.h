#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

struct _win_st;
typedef struct _win_st WINDOW;

namespace synapse {
namespace tui {

enum class Screen {
    BOOT,
    KEY_BACKUP,
    INIT,
    NETWORK_DISCOVERY,
    SYNCING,
    WEB_PROMPT,
    BRIDGE_PASTE,
    SITE_ALLOWLIST_PASTE,
    WELCOME,
    WALLET_CREATE,
    WALLET_CREATED,
    WALLET_IMPORT,
    CONNECTED,
    DASHBOARD,
    WALLET,
    WALLET_SEND,
    WALLET_RECEIVE,
    NETWORK,
    PEERS,
    KNOWLEDGE,
    KNOWLEDGE_SUBMIT,
    CODE,
    CODE_SUBMIT,
    AI_CHAT,
    MODEL,
    MINING,
    SETTINGS,
    SECURITY,
    ATTACHED_AGENT,
    OBSERVATORY,
    HELP
};

enum class Color {
    DEFAULT = 0,
    GREEN = 1,
    YELLOW = 2,
    RED = 3,
    CYAN = 4,
    MAGENTA = 5,
    BLUE = 6,
    WHITE = 7
};

enum class StartupState {
    LOGO,
    INIT_PROGRESS,
    WALLET_CHOICE,
    SEED_DISPLAY,
    SEED_CONFIRM,
    SEED_IMPORT,
    PASSWORD_ENTRY,
    SYNCING,
    ERROR,
    COMPLETE
};

struct WalletInfo {
    std::string address;
    uint64_t balance = 0;
    uint64_t pending = 0;
    uint64_t staked = 0;
    uint64_t totalEarned = 0;
};

struct NetworkInfo {
    uint64_t totalNodes;
    uint64_t knowledgeEntries;
    double networkSize;
    double yourStorage;
    double syncProgress;
    bool synced;
    uint64_t knowledgeFinalized;
    uint64_t knowledgePending;
    uint64_t knownPeers = 0;
    uint64_t connectedPeers = 0;
    uint64_t bootstrapNodes = 0;
    uint64_t dnsSeeds = 0;
    uint64_t dnsQueries = 0;
    uint64_t peerExchanges = 0;
    uint64_t lastPeerRefresh = 0;
    uint64_t lastAnnounce = 0;
    double torHealth = 0.0;
    std::string proxyStatus;
    std::string routeMode;
    uint64_t consensusHeight = 0;
    uint64_t consensusLag = 0;
    double syncRate = 0.0;
    uint64_t poeTotal = 0;
    uint64_t poePending = 0;
    uint64_t poeFinalized = 0;
    uint64_t poeValidatorCount = 0;
    bool poeSelfBootstrapActive = false;
    std::string poeBootstrapMode;
    uint64_t lastReward = 0;
    uint64_t lastRewardEpochId = 0;
    uint64_t lastRewardEntries = 0;
    uint64_t lastRewardAt = 0;
    std::vector<uint64_t> rewardHistory;
};

struct AIModelInfo {
    std::string name;
    std::string status;
    double progress;
    std::string mode;
    int slotsUsed;
    int slotsMax;
    double uptime;
    uint64_t earningsTodayAtoms;
    uint64_t earningsWeekAtoms;
    uint64_t earningsTotalAtoms;
};

struct NodeInfo {
    std::string nodeId;
    std::string id;
    std::string address;
    std::string displayAddress;
    std::string rawAddress;
    std::string transport;
    std::string location;
    uint16_t port;
    uint64_t latency;
    int ping;
    std::string version;
    bool isInbound;
};

struct ContributionInfo {
    std::string type;
    std::string name;
    std::string description;
    std::string time;
    uint64_t rewardAtoms;
    uint64_t timestamp;
};

struct KnowledgeEntrySummary {
    std::string submitId;
    std::string title;
    uint8_t contentType = 0;
    bool finalized;
    uint32_t votes;
    uint32_t requiredVotes;
    uint64_t acceptanceRewardAtoms;
    bool acceptanceRewardCredited;
};

struct StatusInfo {
    uint64_t blockHeight = 0;
    uint64_t peerCount = 0;
    uint64_t knowledgeCount = 0;
    uint64_t balance = 0;
    std::string walletAddress;
    std::string modelName;
    std::string modelStatus;
    double syncProgress = 0.0;
    bool quantumEnabled = false;
    bool liboqsEnabled = false;
    std::string quantumCapabilityMode;
    bool kyberReal = false;
    bool dilithiumReal = false;
    bool sphincsReal = false;
    std::string kyberImplementation;
    std::string dilithiumImplementation;
    std::string sphincsImplementation;
};

struct AttachedAgentStatusInfo {
    bool available = false;
    std::string runtimeState;
    bool runtimeInitialized = false;
    std::string schedulerState;
    uint64_t runtimeCrashCount = 0;
    uint64_t runtimeConsecutiveCrashes = 0;
    uint64_t schedulerTick = 0;
    uint64_t schedulerEpochIndex = 0;
    uint64_t schedulerBudgetCpu = 0;
    uint64_t schedulerBudgetRam = 0;
    uint64_t schedulerBudgetNetwork = 0;
    int64_t agentScore = 0;
    std::string agentScoreBand;
    bool throttled = false;
    bool quarantined = false;
    bool reviewOnly = false;
    bool localDraftOnly = false;
    uint32_t batchLimit = 0;
    uint64_t draftQueueSize = 0;
    uint64_t queuedDrafts = 0;
    uint64_t reviewDrafts = 0;
    uint64_t approvedDrafts = 0;
    uint64_t rejectedDrafts = 0;
    uint64_t submittedDrafts = 0;
    uint64_t pipelineRuns = 0;
    uint64_t pipelineApproved = 0;
    uint64_t pipelineRejected = 0;
    uint64_t pipelineSubmitted = 0;
    uint64_t lastActionAt = 0;
    uint64_t lastReviewAt = 0;
    uint64_t lastDraftAt = 0;
    uint64_t lastHeartbeatAt = 0;
    bool connectorAvailable = false;
    std::string connectorClearnetState;
    std::string connectorTorState;
    std::string connectorOnionState;
    uint64_t connectorPolicyBlocks = 0;
    uint64_t connectorFailures = 0;
    std::string quarantineReason;
    uint64_t quarantineReasonSince = 0;
    uint64_t taskRunsResearch = 0;
    uint64_t taskRunsVerify = 0;
    uint64_t taskRunsReview = 0;
    uint64_t taskRunsDraft = 0;
    uint64_t taskRunsSubmit = 0;
    uint64_t tickCount = 0;
    bool torRequired = false;
    bool torReachable = false;
    bool torReadyForWeb = false;
    bool torReadyForOnion = false;
    bool torReadyForOnionService = false;
    bool torManaged = false;
    bool torDegraded = false;
    bool torControlReachable = false;
    uint32_t torBootstrapPercent = 0;
    std::string torSocksHost;
    uint16_t torSocksPort = 0;
    uint16_t torControlPort = 0;
    bool torConflictHint9050 = false;
    bool clearnetFallbackAllowed = false;
    bool p2pFallbackAllowed = false;
    bool onionServiceActive = false;
    std::string onionServiceState;
    std::string onionAddress;
    std::string torSeedAddress;
    std::string routeMode;
    std::string torRuntimeMode;
    std::string policyHash;
    uint64_t redactionCount = 0;
    std::string dataDir;
    std::string configPath;
    std::string webConfigPath;
    uint64_t p2pConnected = 0;
    uint64_t p2pInbound = 0;
    uint64_t p2pOutbound = 0;
    uint16_t p2pPort = 0;
    double p2pSyncProgress = 0.0;
    uint64_t networkPeerPressurePercent = 0;
    uint64_t networkInboundPressurePercent = 0;
    uint64_t networkOutboundPressurePercent = 0;
    bool networkOverloadMode = false;
    uint64_t networkBufferedRxBytes = 0;
    uint64_t networkRejectedConnections = 0;
    uint64_t networkEvictedPeers = 0;
    uint64_t networkTempBans = 0;
    uint64_t networkMalformedMessages = 0;
    uint64_t networkRateLimitedEvents = 0;
    uint64_t networkOverloadTransitions = 0;
    uint64_t networkConsensusLag = 0;
    uint64_t networkInvBackpressureDrops = 0;
    uint64_t networkGetDataBackpressureDrops = 0;
    uint64_t networkGossipSuppressed = 0;
    uint64_t networkGossipSubsetRouted = 0;
    uint64_t ledgerHeight = 0;
    std::string ledgerTipHash;
    uint64_t ledgerLastBlockTime = 0;
    bool miningActive = false;
    uint64_t miningHashAttemptsTotal = 0;
    uint64_t miningHashAttemptsLast = 0;
    uint64_t miningLastSolvedAt = 0;
    std::string miningWorkTarget;
    std::string miningCandidateHash;
    uint64_t storageAuditSegments = 0;
    uint64_t storageAuditRetainedEvents = 0;
    uint64_t storageAuditRecoveredLines = 0;
    uint64_t storageAuditDroppedSegments = 0;
    uint64_t storageIndexRecoveryRuns = 0;
    uint64_t storageIndexRecoveryLastAt = 0;
    uint64_t storageConsistencyChecks = 0;
    uint64_t storageConsistencyRepairs = 0;
    uint64_t storageConsistencyLastAt = 0;
    uint64_t webTotalSearches = 0;
    uint64_t webSuccessfulFetches = 0;
    uint64_t webFailedFetches = 0;
    uint64_t webPagesExtracted = 0;
    uint64_t webBytesDownloaded = 0;
    uint64_t webLastSearchAt = 0;
    std::string webLastQuery;
    std::string webLastQueryType;
    uint64_t webLastResultCount = 0;
    uint64_t webLastClearnetResults = 0;
    uint64_t webLastOnionResults = 0;
    std::string webLastTopSites;
    bool webLastPostSaved = false;
    std::string webLastSkipReason;
    std::string webLastError;
};

struct ObservatoryArtifactInfo {
    std::string hash;
    std::string roomId;
    std::string type;
    std::string author;
    std::string payloadPreview;
    uint64_t timestamp = 0;
};

struct AgentEventInfo {
    uint64_t timestamp = 0;
    std::string category;
    std::string message;
};

class StartupScreen {
public:
    StartupScreen();
    ~StartupScreen();
    
    void init(WINDOW* win, int width, int height);
    void draw();
    bool handleInput(int ch);
    
    void setState(StartupState state);
    StartupState getState() const;
    
    void setProgress(int percent, const std::string& message);
    void generateSeedWords();
    void setSeedWords(const std::vector<std::string>& words);
    std::vector<std::string> getSeedWords() const;
    void setWalletExists(bool exists);
    void setSyncStatus(int peers, int blocks, int totalBlocks);
    void setError(const std::string& error);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class DashboardScreen {
public:
    DashboardScreen();
    ~DashboardScreen();
    
    void init(WINDOW* win, int width, int height);
    void draw();
    bool handleInput(int ch);
    
    void setWalletInfo(const WalletInfo& info);
    void setNetworkInfo(const NetworkInfo& info);
    void setModelInfo(const AIModelInfo& info);
    void setPeers(const std::vector<NodeInfo>& peers);
    void setContributions(const std::vector<ContributionInfo>& contributions);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class TUI {
public:
    TUI();
    ~TUI();
    
    bool init();
    void run();
    void shutdown();
    bool isRunning() const;
    
    void switchScreen(Screen screen);
    Screen currentScreen() const;
    void refresh();
    
    void updateStatus(const StatusInfo& status);
    void showMessage(const std::string& msg, Color color = Color::GREEN);
    void showError(const std::string& err);
    void showProgress(const std::string& label, double progress);
    void updateOperationStatus(const std::string& operation, const std::string& status, const std::string& details = "");
    void showRewardNotification(uint64_t amountAtoms, const std::string& reason, const std::string& entryId, const std::string& details = "");
    
    void drawBox(int y, int x, int h, int w, const std::string& title = "");
    void drawText(int y, int x, const std::string& text, Color color = Color::DEFAULT);
    void drawProgressBar(int y, int x, int w, double progress, Color color = Color::GREEN);
    
    void onInput(std::function<void(int)> handler);
    void onCommand(std::function<void(const std::string&)> handler);
    
    void setNetworkPort(uint16_t port);
    void setNetworkOnline(bool online);
    void setPeerCount(size_t count);
    void updateNetworkInfo(const NetworkInfo& info);
    void updatePeers(const std::vector<NodeInfo>& peers);
    void updateModelInfo(const AIModelInfo& info);
    void updateWalletInfo(const WalletInfo& info);
    void updateKnowledgeEntries(const std::vector<KnowledgeEntrySummary>& entries);
    void updateAttachedAgentStatus(const AttachedAgentStatusInfo& info);
    void updateObservatoryFeed(const std::vector<ObservatoryArtifactInfo>& items);
    void updateAgentEvents(const std::vector<AgentEventInfo>& events);
    void appendChatMessage(const std::string& role, const std::string& content);
    
    std::string prompt(const std::string& message);
    bool confirm(const std::string& message);
    int menu(const std::string& title, const std::vector<std::string>& options);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}

namespace synapse {
namespace tui {
namespace mining {
    void setPoeStatus(uint64_t poePendingSubmissions, uint64_t poeActiveVotes, const std::string& poeLastFinalizedId, uint64_t poeLastFinalizedRewardNgt, uint32_t poePowDifficulty, uint64_t poeEstSecondsToFinalize);
}
}
}
