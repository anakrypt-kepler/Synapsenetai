#ifndef SYNAPSENET_MODEL_INFERENCE_H
#define SYNAPSENET_MODEL_INFERENCE_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <memory>
#ifdef USE_LLAMA_CPP
#include "llama.h"
#endif

namespace synapse {
namespace model {

enum class InferenceStatus {
    QUEUED = 0,
    PROCESSING = 1,
    COMPLETED = 2,
    FAILED = 3,
    CANCELLED = 4
};

enum class InputType {
    TEXT = 0,
    IMAGE = 1,
    AUDIO = 2,
    EMBEDDING = 3,
    TENSOR = 4
};

struct InferenceRequest {
    std::string requestId;
    std::string modelId;
    InputType inputType = InputType::TEXT;
    std::vector<float> inputData;
    std::string inputText;
    std::map<std::string, float> parameters;
    uint64_t timestamp = 0;
    uint64_t timeout = 0;
    int priority = 0;
    std::function<void(const std::string&)> callback;
    std::function<void(const std::string&)> streamCallback;  // Called on each generated token
};

struct InferenceResult {
    std::string requestId;
    std::string modelId;
    InferenceStatus status = InferenceStatus::QUEUED;
    std::vector<float> outputData;
    std::string outputText;
    std::vector<std::pair<std::string, float>> topK;
    float confidence = 0.0f;
    uint64_t startTime = 0;
    uint64_t endTime = 0;
    uint64_t tokensGenerated = 0;
    std::string errorMessage;
};

struct LoadedModelState {
    std::string modelId;
    std::string modelPath;
    std::vector<std::vector<float>> weights;
    std::vector<float> biases;
    std::map<std::string, std::vector<float>> kvCache;
    uint64_t contextLength = 0;
    uint64_t maxContextLength = 0;
    bool isLoaded = false;
#ifdef USE_LLAMA_CPP
    llama_model* llamaModel = nullptr;
    llama_context* llamaContext = nullptr;
#endif
};

class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    bool initialize(int numWorkers = 0);
    void shutdown();

    std::string runSyncText(const std::string& modelId, const std::string& prompt,
                            const std::map<std::string, float>& params = {});

    std::string runStreamText(const std::string& modelId, const std::string& prompt,
                              std::function<void(const std::string&)> streamCallback,
                              const std::map<std::string, float>& params = {});

    std::string submitRequest(const InferenceRequest& request);
    bool cancelRequest(const std::string& requestId);
    InferenceResult getResult(const std::string& requestId);
    InferenceStatus getStatus(const std::string& requestId);

    bool loadModel(const std::string& modelId, const std::string& path);
    bool unloadModel(const std::string& modelId);
    bool isModelLoaded(const std::string& modelId);

    void setMaxBatchSize(int size);
    void setMaxQueueSize(int size);
    void setDefaultTimeout(uint64_t ms);

    size_t getQueueSize() const;
    size_t getActiveRequests() const;
    uint64_t getTotalInferences() const;
    double getAverageLatency() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace model
} // namespace synapse

#endif // SYNAPSENET_MODEL_INFERENCE_H
