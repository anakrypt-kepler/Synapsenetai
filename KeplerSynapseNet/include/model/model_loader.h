#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace synapse {
namespace model {

enum class ModelFormat {
    GGUF,
    GGML,
    SAFETENSORS,
    PYTORCH,
    ONNX,
    UNKNOWN
};

enum class AccessMode {
    PRIVATE,
    SHARED,
    PAID,
    COMMUNITY
};

enum class ModelState {
    UNLOADED,
    LOADING,
    READY,
    GENERATING,
    ERROR,
    DOWNLOADING
};

enum class ModelType {
    TEXT_GENERATION,
    EMBEDDING,
    VISION,
    AUDIO,
    MULTIMODAL
};

struct ModelMetadata {
    std::string author;
    std::string license;
    std::string description;
    std::vector<std::string> tags;
    std::string homepage;
    std::string version;
    uint64_t createdAt;
    uint64_t updatedAt;
};

struct ModelInfo {
    std::string id;
    std::string name;
    std::string path;
    uint64_t sizeBytes;
    ModelFormat format;
    ModelType type;
    AccessMode access;
    uint32_t contextSize;
    uint32_t maxContextSize;
    uint32_t layers;
    uint32_t embeddingSize;
    uint32_t vocabSize;
    uint32_t headCount;
    uint32_t kvHeadCount;
    std::string architecture;
    std::string quantization;
    std::string baseModel;
    ModelMetadata metadata;
    bool supportsGpu;
    bool supportsMmap;
    uint64_t parameterCount;
};

struct GenerationParams {
    uint32_t maxTokens = 512;
    float temperature = 0.7f;
    float topP = 0.9f;
    uint32_t topK = 40;
    float repeatPenalty = 1.1f;
    float presencePenalty = 0.0f;
    float frequencyPenalty = 0.0f;
    uint32_t repeatLastN = 64;
    uint64_t seed = 0;
    std::vector<std::string> stopSequences;
    std::string grammar;
    bool jsonMode = false;
    float minP = 0.0f;
    float typicalP = 1.0f;
    int32_t mirostatMode = 0;
    float mirostatTau = 5.0f;
    float mirostatEta = 0.1f;
};

struct LoaderConfig {
    uint32_t contextSize = 2048;
    uint32_t batchSize = 512;
    uint32_t threads = 4;
    uint32_t gpuLayers = 0;
    bool useGpu = false;
    bool useMmap = true;
    bool useMlock = false;
    bool flashAttention = false;
    uint64_t maxMemory = 0;
    std::string cacheDir;
};

struct LoaderStats {
    uint64_t loadTimeMs;
    uint64_t memoryUsed;
    uint64_t tokensGenerated;
    uint64_t totalRequests;
    double avgTokensPerSec;
    double avgLatencyMs;
};

class ModelLoader {
public:
    ModelLoader();
    ~ModelLoader();
    
    bool load(const std::string& path);
    bool load(const std::string& path, const LoaderConfig& config);
    bool loadFromHub(const std::string& modelId);
    bool unload();
    bool reload();
    
    bool isLoaded() const;
    ModelState getState() const;
    ModelInfo getInfo() const;
    std::string getError() const;
    LoaderStats getStats() const;
    
    std::string generate(const std::string& prompt, const GenerationParams& params = {});
    std::string chat(const std::vector<std::pair<std::string, std::string>>& messages,
                     const GenerationParams& params = {});
    void generateStream(const std::string& prompt, 
                        std::function<bool(const std::string&)> callback,
                        const GenerationParams& params = {});
    void stopGeneration();
    bool isGenerating() const;
    
    std::vector<float> getEmbedding(const std::string& text);
    std::vector<std::vector<float>> getEmbeddings(const std::vector<std::string>& texts);
    float similarity(const std::string& text1, const std::string& text2);
    
    std::vector<uint32_t> tokenize(const std::string& text);
    std::string detokenize(const std::vector<uint32_t>& tokens);
    uint32_t tokenCount(const std::string& text) const;
    
    bool downloadModel(const std::string& modelName, 
                       std::function<void(double)> progressCallback = nullptr);
    bool cancelDownload();
    std::vector<std::string> listAvailable() const;
    std::vector<std::string> listDownloaded() const;
    std::vector<ModelInfo> listModels(const std::string& directory) const;
    bool deleteModel(const std::string& path);
    
    void setConfig(const LoaderConfig& config);
    LoaderConfig getConfig() const;
    void setContextSize(uint32_t size);
    void setThreads(uint32_t threads);
    void setGpuLayers(uint32_t layers);
    void setBatchSize(uint32_t size);
    
    void onStateChange(std::function<void(ModelState)> callback);
    void onProgress(std::function<void(double)> callback);
    void onError(std::function<void(const std::string&)> callback);
    
    static bool validateModel(const std::string& path);
    static ModelFormat detectFormat(const std::string& path);
    static uint64_t estimateMemory(const std::string& path, uint32_t contextSize);
    
    // Utility functions for error reporting
    static std::string formatBytes(uint64_t bytes);
    static uint64_t getFreeDiskSpace(const std::string& path);
    
    // Model persistence (Task 3)
    bool saveLastModel() const;
    bool loadLastModel();
    std::vector<std::string> getAvailableModels() const;
    
    // Model scan paths (Task 4)
    void setScanPaths(const std::vector<std::string>& paths);
    std::vector<std::string> getScanPaths() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> scanPaths_;
};

class ModelDownload {
public:
    ModelDownload();
    ~ModelDownload();
    
    void setModelsDirectory(const std::string& path);
    void setMaxConcurrent(int max);
    void setBandwidthLimit(uint64_t bytesPerSecond);
    
    std::string startDownload(const std::string& modelId, const std::string& url,
                               const std::string& filename);
    std::string startDownload(const std::string& modelId, const std::string& url,
                               const std::string& filename, const std::string& expectedHash,
                               int priority);
    
    bool cancelDownload(const std::string& modelId);
    bool pauseDownload(const std::string& modelId);
    bool resumeDownload(const std::string& modelId);
    
    int getProgress(const std::string& modelId) const;
    bool isDownloading(const std::string& modelId) const;
    bool isComplete(const std::string& modelId) const;
    bool isPaused(const std::string& modelId) const;
    bool hasFailed(const std::string& modelId) const;
    std::string getError(const std::string& modelId) const;
    
    uint64_t getDownloadedBytes(const std::string& modelId) const;
    uint64_t getTotalBytes(const std::string& modelId) const;
    
    std::vector<std::string> getActiveDownloads() const;
    std::vector<std::string> getCompletedDownloads() const;
    
    void setProgressCallback(std::function<void(const std::string&, int)> callback);
    void setCompleteCallback(std::function<void(const std::string&)> callback);
    void setErrorCallback(std::function<void(const std::string&, const std::string&)> callback);
    
    void stopAll();
    void clearCompleted();
    bool retryDownload(const std::string& modelId);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct ModelListingInfo {
    std::string modelId;
    std::string ownerId;
    std::string name;
    std::string description;
    uint64_t size;
    std::string format;
    uint64_t pricePerHourAtoms = 0;
    uint64_t pricePerRequestAtoms = 0;
    uint32_t maxSlots = 0;
    uint32_t usedSlots = 0;
    uint32_t availableSlots = 0;
    uint32_t ratingMilli = 0;   // 0..5000 (0.000..5.000)
    uint32_t ratingCount = 0;
    uint64_t totalRequests;
    uint64_t totalEarningsAtoms = 0;
    bool active;
    uint64_t createdAt;
    uint64_t lastActive;
};

struct MarketplaceStats {
    uint64_t totalListings;
    uint64_t activeListings;
    uint64_t totalRentals;
    uint64_t activeRentals;
    uint64_t totalSessions;
    uint64_t activeSessions;
    uint64_t totalRequests;
    uint64_t totalVolumeAtoms = 0;
    uint64_t totalEarningsAtoms = 0;
    uint64_t avgPricePerRequestAtoms = 0;
};

class ModelMarketplace {
public:
    ModelMarketplace();
    ~ModelMarketplace();
    
    // Create a new listing. Returns listingId.
    std::string listModel(const std::string& ownerId, const std::string& name,
                          const std::string& description, uint64_t size,
                          const std::string& format, uint64_t pricePerHourAtoms,
                          uint64_t pricePerRequestAtoms, uint32_t maxSlots);
    // Create/update a listing with a stable id (used for P2P offers).
    bool upsertModel(const std::string& listingId, const std::string& ownerId, const std::string& name,
                     const std::string& description, uint64_t size, const std::string& format,
                     uint64_t pricePerHourAtoms, uint64_t pricePerRequestAtoms, uint32_t maxSlots, bool active);
    bool unlistModel(const std::string& modelId, const std::string& ownerId);
    bool updatePrice(const std::string& modelId, const std::string& ownerId,
                     uint64_t pricePerHourAtoms, uint64_t pricePerRequestAtoms);
    bool updateSlots(const std::string& modelId, const std::string& ownerId, uint32_t maxSlots);
    
    std::string rentModel(const std::string& modelId, const std::string& renterId);
    bool endRental(const std::string& sessionId);
    bool recordRequest(const std::string& sessionId, uint32_t tokensUsed, uint64_t latencyMs);
    bool recordPayment(const std::string& sessionId, uint64_t amountAtoms);
    bool rateModel(const std::string& modelId, const std::string& renterId, uint32_t ratingMilli);
    
    std::vector<ModelListingInfo> searchModels(const std::string& query, 
                                                const std::string& sortBy,
                                                const std::string& format,
                                                uint64_t maxPricePerHourAtoms,
                                                int limit = 50);
    std::vector<ModelListingInfo> getOwnerListings(const std::string& ownerId);
    std::vector<ModelListingInfo> getAllListings(bool includeInactive = false) const;
    MarketplaceStats getStats() const;
    
    void onPaymentReceived(std::function<void(const std::string&, uint64_t)> callback);
    void onSessionStarted(std::function<void(const std::string&)> callback);
    void onSessionEnded(std::function<void(const std::string&)> callback);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
