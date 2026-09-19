#pragma once

// SynapsedEngine is the in-process node used by the Tauri desktop app and TUI.
// Singleton. init() boots lib internals; rpcCall() is the JSON-RPC surface
// (wallet, blocks, naan.*, harvest.*, exploit.*).
// NAAN harvest loop (naanLoop), fetchWithRetry, harvest, and exploit chain
// are private methods on this class — see synapsed_engine.cpp.

#include "core/poe_v1_engine.h"
#include "core/naan_task_share.h"
#include "crypto/crypto.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace synapse {
namespace privacy {
class PrivacyManager;
}
namespace model {
class InferenceEngine;
}
namespace ide {

using EventCallback = std::function<void(const char* event_type, const char* payload_json)>;

struct NaanLogEntry {
    int64_t ts;
    std::string text;
};

struct NaanDraft {
    std::string title;
    std::string topic;
    std::string status;
    double ngt;
};

struct NaanAgentSlot {
    std::string id;
    std::atomic<bool> running{false};
    std::atomic<bool> stop{true};
    std::thread thread;
    std::string state = "off";
    std::string error;
    std::string currentTask;
    std::string currentTaskId;
    std::string modelMode = "primary";
    std::string modelPath;
    std::string modelName;
    std::string inferenceState = "idle";
    std::vector<NaanLogEntry> log;
    std::vector<NaanDraft> hist;
    int submissions = 0;
    int approved = 0;
    double spent = 0.0;
};

class SynapsedEngine {
public:
    static SynapsedEngine& instance();

    int init(const std::string& configPath);
    void shutdown();
    bool isInitialized() const;

    std::string rpcCall(const std::string& method, const std::string& paramsJson);
    int subscribe(const std::string& eventType, EventCallback callback);
    std::string getStatus() const;

    SynapsedEngine(const SynapsedEngine&) = delete;
    SynapsedEngine& operator=(const SynapsedEngine&) = delete;

private:
    SynapsedEngine();
    ~SynapsedEngine();

    struct TorInfo {
        std::string bootstrap;
        int circuits = 0;
        std::string version;
        std::string exitIp;
        bool connected = false;
        int64_t trafficRead = 0;
        int64_t trafficWritten = 0;
    };
    TorInfo queryTorControl() const;
    std::string fetchViaTor(const std::string& url) const;
    std::string fetchViaTor(const std::string& url, const std::string& cookieTag) const;
    bool isUrlSafe(const std::string& url) const;
    void generateTorrc() const;

    void startNaan();
    void stopNaan();
    void compactLocalPoeChain();
    void naanLoop();
    void naanLoopFor(const std::string& agentId);
    std::string naanStatus() const;
    std::string naanControl(const std::string& paramsJson);
    std::string startNaanAgent(const std::string& agentId);
    std::string stopNaanAgent(const std::string& agentId);
    void stopAllNaanAgents();
    bool isKnownNaanAgent(const std::string& agentId) const;
    void applyCrewModelSettings(const std::string& agentId);
    std::string formatNaanAgentJson(const std::string& agentId) const;
    std::vector<std::string> extractTitles(const std::string& html) const;
    std::string topicToUrl(const std::string& topic) const;
    std::string sha256Hex(const std::string& data) const;

    struct CaptchaResult {
        bool detected = false;
        bool solved = false;
        std::string type;
        std::string answer;
    };
    CaptchaResult detectCaptcha(const std::string& html) const;
    std::string solveMathCaptcha(const std::string& expr) const;
    std::string solveTextCaptcha(const std::string& imgUrl) const;
    std::string solveTextCaptchaCyrillic(const std::string& imgUrl) const;
    std::string solveOddOneOut(const std::string& html, const std::string& baseUrl) const;
    std::string solveClockCaptcha(const std::string& imgUrl) const;
    std::string solveHieroglyphCaptcha(const std::string& html) const;
    std::string solveMultiStepCaptcha(const std::string& html) const;
    std::string solveRotateCaptcha(const std::string& html) const;
    std::string solveSliderCaptcha(const std::string& html) const;
    std::string solvePairCaptcha(const std::string& html) const;
    std::string downloadCaptchaImage(const std::string& imgUrl) const;
    std::string classifyImage(const std::string& imgPath) const;
    double detectImageRotation(const std::string& imgPath) const;
    int detectSliderOffset(const std::string& bgPath, const std::string& piecePath) const;
    std::string submitCaptchaAndRefetch(const std::string& url,
        const std::string& formAction, const std::string& field,
        const std::string& answer) const;

    struct ClearnetBypass {
        bool cloudflare = false;
        bool recaptcha = false;
        bool hcaptcha = false;
        bool turnstile = false;
        bool rateLimit = false;
        std::string cfClearance;
        std::string siteKey;
    };
    ClearnetBypass detectClearnetProtection(const std::string& html,
        int httpCode) const;
    std::string fetchClearnet(const std::string& url) const;
    std::string bypassCloudflareChallenge(const std::string& url) const;
    std::string solveRecaptchaAudio(const std::string& siteKey,
        const std::string& pageUrl) const;
    std::string solveHCaptcha(const std::string& siteKey,
        const std::string& pageUrl) const;
    std::string randomUserAgent() const;
    std::string fetchWithRetry(const std::string& url, int maxRetries) const;
    std::string fetchWithRetry(const std::string& url, int maxRetries,
                               const std::atomic<bool>* stopFlag,
                               const std::string& cookieTag) const;

    struct EndGameV3Challenge {
        bool detected = false;
        std::string challenge;
        int difficulty = 0;
        std::string submitUrl;
        std::string extraFields;
    };
    EndGameV3Challenge detectEndGameV3(const std::string& html,
        const std::string& baseUrl) const;
    std::string solveEndGamePoW(const std::string& challenge, int difficulty) const;
    std::string submitEndGamePoW(const EndGameV3Challenge& ch,
        const std::string& nonce) const;

    struct VulnDetectionResult {
        std::string cveId;
        std::string protectionType;
        std::string bypassMethod;
        bool exploitable = false;
        double confidence = 0.0;
    };
    VulnDetectionResult detectVulnerability(const std::string& html,
        const std::string& url, int httpCode, double ttfbMs) const;
    std::string exploitCVE0001_PowCookieReplay(const std::string& url) const;
    std::string exploitCVE0002_QueueRace(const std::string& url) const;
    std::string exploitCVE0003_CssSelectorLeak(const std::string& html,
        const std::string& url) const;
    std::string exploitCVE0004_CfBmReplay(const std::string& url) const;
    std::string exploitCVE0005_SucuriXsrfReplay(const std::string& url) const;
    std::string exploitCVE0007_CfManagedBypass(const std::string& html,
        const std::string& url, int httpCode) const;
    std::string exploitCVE0008_TimingOracle(const std::string& url) const;
    std::string exploitCVE0009_CookieConfusion(const std::string& url) const;
    std::string exploitCVE0011_QueueRefreshBypass(const std::string& url) const;
    std::string exploitCVE0012_QueueCookieTTL(const std::string& url) const;
    std::string exploitCVE0013_QueueNewnym(const std::string& url) const;
    std::string exploitCVE0014_CaptchaTokenReplay(const std::string& html,
        const std::string& url) const;
    std::string solveCaptchaViaLLM(const std::string& html,
        const std::string& imgPath, const std::string& url) const;

    mutable std::unordered_map<std::string, std::string> captchaTokenCache_;

    struct ExploitIntel {
        std::string cveId;
        std::string protectionType;
        std::string bypassMethod;
        std::string transport;
        double confidence = 0.0;
        std::string discoveredBy;
        int64_t timestamp = 0;
        int successCount = 0;
        int failCount = 0;
        std::string signature;
    };
    mutable std::mutex exploitChainMtx_;
    mutable std::vector<ExploitIntel> exploitChain_;
    mutable std::unordered_map<std::string, int64_t> exploitChainIndex_;

    void publishExploit(const ExploitIntel& intel) const;
    void ingestExploit(const ExploitIntel& intel) const;
    std::string exploitChainList(int offset, int limit) const;
    std::string exploitChainStats() const;
    ExploitIntel bestExploitFor(const std::string& protectionType) const;
    void syncExploitChainFromPeers() const;
    void persistExploitChain() const;
    void loadExploitChain() const;

    struct CookiePool {
        std::unordered_map<std::string, std::string> powCookies;
        std::unordered_map<std::string, int64_t> powExpiry;
        std::unordered_map<std::string, std::string> cfBmCookies;
        std::unordered_map<std::string, int64_t> cfBmExpiry;
        std::unordered_map<std::string, std::string> sessionCookies;
    };
    mutable CookiePool cookiePool_;

    struct BypassReport {
        std::string cveId;
        std::string protectionType;
        std::string bypassMethod;
        std::string transport;
        double ttfbMs = 0.0;
        int httpCode = 0;
        size_t bytes = 0;
        int64_t ts = 0;
    };
    mutable std::mutex bypassMtx_;
    mutable BypassReport lastBypass_;
    mutable std::unordered_map<std::string, int> bypassCounters_;
    mutable std::atomic<bool> jarPrimed_{false};

    void recordBypass(const std::string& cveId, const std::string& protection,
        const std::string& method, const std::string& transport,
        double ttfbMs, int httpCode, size_t bytes) const;
    void primeCookieJar() const;
    void emitEvent(const std::string& eventType, const std::string& payloadJson) const;

    struct HarvestAsset {
        std::string localPath;
        std::string sha256;
        std::string mimeGuess;
        size_t bytes = 0;
        std::string vtVerdict;
    };
    struct HarvestPayload {
        std::string text;
        std::vector<HarvestAsset> assets;
    };

    HarvestPayload extractAssets(const std::string& html,
        const std::string& baseUrl) const;
    std::string downloadAsset(const std::string& url, bool isOnion,
        const std::string& assetsDir) const;
    std::string vtScanFile(const std::string& sha256,
        const std::string& filePath) const;
    void persistHarvest(const NaanDraft& d, const std::string& hash,
        const HarvestPayload& payload) const;
    std::string harvestList(int offset, int limit) const;
    std::string harvestGet(const std::string& sha256) const;
    std::string stripHtmlToText(const std::string& html) const;

    mutable std::string vtApiKey_;
    mutable bool vtApiKeyLoaded_ = false;

    std::string ed25519Sign(const std::string& data) const;
    void ensureSigningKey() const;
    void persistDraft(const NaanDraft& d, const std::string& hash) const;

    std::string modelLoad(const std::string& paramsJson);
    std::string modelUnloadRpc();
    std::string modelStatus() const;
    bool validateGguf(const std::string& path) const;
    std::string modelCatalogJson() const;
    std::string modelDownloadStart(const std::string& paramsJson);
    std::string modelDownloadStatusJson() const;
    std::string modelDownloadCancel();
    void modelDownloadLoop(std::string id, std::string url, std::string dest,
                           uint64_t expected, bool viaTor);
    std::string aiCompleteRpc(const std::string& paramsJson);
    std::string cryptoStatusJson() const;
    bool ensureLlamaLoaded() const;
    void applyDesktopConfig();

    mutable std::mutex mtx_;
    bool initialized_ = false;
    std::string configPath_;
    std::string dataDir_;
    std::string nodeId_;
    int64_t startTime_ = 0;
    mutable int peerCount_ = 0;
    mutable std::string connectionType_ = "disconnected";
    std::string walletAddress_;
    std::string walletMnemonic_;
    mutable std::string balance_ = "0.00";
    mutable std::string torBootstrap_;
    mutable int torCircuits_ = 0;
    mutable std::mutex torInfoMtx_;
    mutable TorInfo torInfoCache_;
    mutable int64_t torInfoCacheMs_ = 0;

    struct PeerEntry {
        std::string address;
        std::string transport;
        int latency_ms = 0;
        std::string role;
        bool alive = false;
        int64_t last_ok_ms = 0;
        std::string alias;
        std::string avatar;
    };
    mutable std::vector<PeerEntry> cachedPeers_;
    mutable std::vector<PeerEntry> cachedSeeds_;
    mutable std::mutex peerCacheMtx_;
    mutable int64_t lastPeerProbe_ = 0;
    mutable std::atomic<bool> peerProbeBusy_{false};
    mutable std::thread peerProbeThread_;
    void probeSeedNodes() const;

    mutable std::string ownOnion_;
    mutable std::string onionPrivKey_;
    mutable std::string onionServiceId_;
    mutable int listenFd_ = -1;
    mutable uint16_t listenPort_ = 0;
    mutable std::atomic<bool> hsReachable_{false};
    mutable std::atomic<int> hsLatencyMs_{-1};
    mutable int64_t lastTrafficRead_{0};
    mutable int64_t lastTrafficWritten_{0};
    mutable int64_t lastTrafficTs_{0};
    mutable int inboundKbps_{0};
    mutable int outboundKbps_{0};
    mutable int controlFd_ = -1;
    mutable std::thread listenerThread_;
    mutable std::atomic<bool> listenerStop_{false};
    mutable uint16_t sessionSocksPort_ = 0;
    mutable uint16_t sessionControlPort_ = 0;
    mutable int64_t sessionTorPid_ = -1;
    mutable std::string sessionTorDataDir_;
    mutable std::string sessionCookiePath_;
    mutable std::thread sessionBootThread_;
    mutable std::atomic<bool> sessionBootStop_{false};
    bool startSessionTor() const;
    void stopSessionTor() const;
    void bootTorMesh();
    void startOnionService() const;
    void stopListener() const;
    void p2pListenerLoop() const;
    void announceToSeed(const std::string& seedOnion, uint16_t port) const;
    void fetchBlocksFromSeed(const std::string& seedOnion);
    std::vector<std::string> fetchPeersFromSeed(const std::string& seedOnion);
    void announcePresenceToSeed(const std::string& seedOnion);

    struct KnownPeer {
        std::string onion;
        int64_t lastSeen = 0;
        int64_t firstSeen = 0;
        int latency_ms = 0;
        std::string source;
        bool connected = false;
        std::string alias;
        std::string avatar;
        std::string boxPk;
        std::string kemPk;
        std::string poePk;
    };
    mutable std::mutex knownPeersMtx_;
    mutable std::map<std::string, KnownPeer> knownPeers_;
    mutable std::array<unsigned char, 32> msgBoxPk_{};
    mutable std::array<unsigned char, 32> msgBoxSk_{};
    mutable bool msgBoxReady_ = false;
    mutable std::vector<uint8_t> msgKemPk_;
    mutable std::vector<uint8_t> msgKemSk_;
    mutable bool msgKemReady_ = false;
    void mergeKnownPeer(const std::string& onion, const std::string& source, bool connected) const;
    std::vector<std::string> dialPeer(const std::string& onion);
    void loadLocalProfile() const;
    void ensureMsgBoxKeys() const;
    void ensureKemKeys() const;
    void ingestNodeProfile(const std::string& jsonBody) const;
    void pushLocalProfile(const std::string& onion) const;
    std::string localProfileLine() const;
    std::string peerBoxPk(const std::string& onion) const;
    std::string peerKemPk(const std::string& onion) const;
    bool sealToPeer(const std::string& peerPkHex, const std::string& plaintext, std::string& sealedB64) const;
    bool openSealedMsg(const std::string& sealedB64, std::string& plaintext) const;
    bool wrapSealHybrid(const std::string& peerKemPkHex, const std::string& sealedB64,
                        std::string& kemCtB64, std::string& wrappedSealB64) const;
    bool unwrapSealHybrid(const std::string& kemCtB64, const std::string& wrappedSealB64,
                          std::string& sealedB64) const;
    void loadPeerCache() const;
    void savePeerCache() const;

    mutable std::atomic<uint64_t> lastBlockHeight_{0};
    uint64_t localChainHeight() const;
    void appendLocalChainBlock(const std::string& eventType, const std::string& eventHash);
    mutable std::atomic<uint32_t> seedPeerCount_{0};
    mutable std::thread blockFetchThread_;
    mutable std::atomic<bool> blockFetchStop_{false};

    bool modelLoaded_ = false;
    std::string modelName_;
    std::string modelPath_;
    size_t modelSizeMb_ = 0;
    mutable bool inferenceReady_ = false;
    mutable std::string profileAlias_;
    mutable std::string profileAvatarDataUrl_;
    mutable std::string profileAvatarMesh_;
    mutable std::unique_ptr<synapse::model::InferenceEngine> llamaEngine_;
    mutable std::mutex llamaMtx_;

    mutable std::mutex modelDlMtx_;
    std::thread modelDlThread_;
    std::atomic<bool> modelDlStop_{false};
    std::atomic<long> modelDlPid_{-1};
    struct ModelDlState {
        std::string id;
        std::string filename;
        std::string path;
        std::string error;
        uint64_t bytes = 0;
        uint64_t total = 0;
        bool running = false;
        bool done = false;
        bool failed = false;
        bool viaTor = false;
    };
    mutable ModelDlState modelDl_;

    std::atomic<bool> naanRunning_{false};
    std::atomic<bool> naanStop_{false};
    std::thread naanThread_;
    std::map<std::string, std::unique_ptr<NaanAgentSlot>> naanAgents_;
    std::mutex naanStartMtx_;
    synapse::core::NaanTaskShare naanShare_;
    std::string naanState_ = "off";
    mutable std::string naanCurrentTask_;
    int naanTickInterval_ = 45;
    double naanBudgetPerEpoch_ = 100.0;
    double naanSpentThisEpoch_ = 0.0;
    std::vector<std::string> cfgTopics_ = {
        "whistleblower", "zero-day", "darknet", "AI", "crypto"
    };
    // tor | both | clearnet. Default tor: no clearnet destinations unless the operator picks them.
    std::string cfgSources_ = "tor";
    void loadNaanWebConfig();
    void persistNaanSources() const;
    std::vector<NaanLogEntry> naanLog_;
    std::vector<NaanDraft> naanHist_;
    int naanSubmissions_ = 0;
    int naanApproved_ = 0;
    mutable double naanTotalNgt_ = 0.0;
    std::unique_ptr<synapse::privacy::PrivacyManager> privacy_;
    mutable std::mutex privateWalletMtx_;

    void ensureStealthWallet();
    void loadMigratePrivateWallet() const;
    std::string sendPrivateNgt(const std::string& recipient, double amt, const std::string& memo = "");
    void ingestPrivateTxJson(const std::string& jsonLine) const;
    void relayPrivateTxJson(const std::string& jsonLine) const;
    std::string stealthReceiveAddress() const;

    bool initPoeEngine();
    void refreshPoeValidators() const;
    std::string submitPoeKnowledge(const std::string& title, const std::string& body,
                                   synapse::core::poe_v1::ContentType type);
    void ingestPoeEntryHex(const std::string& hex) const;
    void ingestPoeVoteHex(const std::string& hex) const;
    void ingestPoeRecipeHex(const std::string& hex) const;
    void ingestPoeRecipeReplayHex(const std::string& hex) const;
    void maybePoeAutoVote(const synapse::crypto::Hash256& submitId) const;
    void gossipPoeLine(const std::string& line) const;
    std::vector<std::string> meshPoeDests() const;
    bool meshSend(const std::string& onion, const std::string& payload) const;
    void appendKnowledgeJsonl(const std::string& submitHex, const std::string& kind,
                              const std::string& title, bool finalized) const;
    void markKnowledgeFinalized(const std::string& submitHex, uint64_t creditedAtoms = 0) const;
    uint64_t maybeCreditPoeStealth(const synapse::crypto::Hash256& submitId) const;

    mutable std::mutex poeMtx_;
    mutable std::unique_ptr<synapse::core::PoeV1Engine> poeV1_;
    mutable synapse::crypto::PrivateKey poeSk_{};
    mutable synapse::crypto::PublicKey poePk_{};
    mutable std::atomic<bool> poeReady_{false};

    std::unordered_map<std::string, std::vector<EventCallback>> subscribers_;
};

}
}
