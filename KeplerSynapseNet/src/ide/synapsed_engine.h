#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace synapse {
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
    };
    TorInfo queryTorControl() const;
    std::string fetchViaTor(const std::string& url) const;
    bool isUrlSafe(const std::string& url) const;
    void generateTorrc() const;

    void startNaan();
    void stopNaan();
    void naanLoop();
    std::string naanStatus() const;
    std::string naanControl(const std::string& paramsJson);
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

    std::string ed25519Sign(const std::string& data) const;
    void ensureSigningKey() const;
    void persistDraft(const NaanDraft& d, const std::string& hash) const;

    std::string modelLoad(const std::string& paramsJson);
    std::string modelStatus() const;
    bool validateGguf(const std::string& path) const;

    mutable std::mutex mtx_;
    bool initialized_ = false;
    std::string configPath_;
    std::string dataDir_;
    std::string nodeId_;
    int64_t startTime_ = 0;
    int peerCount_ = 0;
    std::string connectionType_ = "disconnected";
    std::string walletAddress_;
    std::string balance_ = "0.00";

    bool modelLoaded_ = false;
    std::string modelName_;
    std::string modelPath_;
    size_t modelSizeMb_ = 0;

    std::atomic<bool> naanRunning_{false};
    std::atomic<bool> naanStop_{false};
    std::thread naanThread_;
    std::string naanState_ = "off";
    int naanTickInterval_ = 45;
    double naanBudgetPerEpoch_ = 100.0;
    double naanSpentThisEpoch_ = 0.0;
    std::vector<std::string> cfgTopics_ = {
        "whistleblower", "zero-day", "darknet", "AI", "crypto"
    };
    std::vector<NaanLogEntry> naanLog_;
    std::vector<NaanDraft> naanHist_;
    int naanSubmissions_ = 0;
    int naanApproved_ = 0;
    double naanTotalNgt_ = 0.0;

    std::unordered_map<std::string, std::vector<EventCallback>> subscribers_;
};

}
}
