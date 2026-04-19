#include "model/model_loader.h"
#include "model/model_inference.h"
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <fstream>
#include <sstream>
#include <iomanip>
#ifdef USE_LLAMA_CPP
#include "llama.h"
#include "ggml-backend.h"
#endif

#ifdef USE_LLAMA_CPP
static std::once_flag g_llamaInitFlag;
static std::mutex g_llamaRefMutex;
static int g_llamaRefCount = 0;
#endif

namespace synapse {
namespace model {

struct InferenceEngine::Impl {
    std::map<std::string, LoadedModelState> models;
    std::map<std::string, InferenceResult> results;
    std::priority_queue<InferenceRequest, std::vector<InferenceRequest>,
                         std::function<bool(const InferenceRequest&, const InferenceRequest&)>> requestQueue;
    std::vector<std::thread> workers;
    mutable std::mutex mtx;
    mutable std::mutex resultMtx;
    std::condition_variable cv;
    std::atomic<bool> running{false};

    int maxBatchSize = 1;
    int maxQueueSize = 1000;
    uint64_t defaultTimeout = 30000;
    std::atomic<uint64_t> totalInferences{0};
    std::atomic<uint64_t> totalLatency{0};
    std::atomic<size_t> activeRequests{0};

    void workerLoop();
    InferenceResult processRequest(const InferenceRequest& request);
    std::vector<float> runForward(const LoadedModelState& model, const std::vector<float>& input);
    std::vector<float> softmax(const std::vector<float>& logits);
    std::vector<float> layerNorm(const std::vector<float>& input, float eps = 1e-5);
    std::vector<float> gelu(const std::vector<float>& input);
    std::vector<float> matmul(const std::vector<float>& a, const std::vector<std::vector<float>>& b);
    std::vector<float> attention(const std::vector<float>& q, const std::vector<float>& k,
                                 const std::vector<float>& v, int numHeads);
    std::string generateText(const LoadedModelState& model, const std::string& prompt,
                             const std::map<std::string, float>& params,
                             int* outTokensGenerated = nullptr, float* outConfidence = nullptr);
    std::string generateTextStream(const LoadedModelState& model, const std::string& prompt,
                                   std::function<void(const std::string&)> streamCallback,
                                   const std::map<std::string, float>& params,
                                   int* outTokensGenerated = nullptr, float* outConfidence = nullptr);
    std::vector<int> tokenize(const std::string& text);
    std::string detokenize(const std::vector<int>& tokens);
    std::string generateRequestId();
};

InferenceEngine::InferenceEngine() : impl_(std::make_unique<Impl>()) {
    impl_->running = false;
    impl_->maxBatchSize = 1;
    impl_->maxQueueSize = 1000;
    impl_->defaultTimeout = 30000;
    impl_->totalInferences = 0;
    impl_->totalLatency = 0;
    impl_->activeRequests = 0;
    
    impl_->requestQueue = std::priority_queue<InferenceRequest, std::vector<InferenceRequest>,
        std::function<bool(const InferenceRequest&, const InferenceRequest&)>>(
        [](const InferenceRequest& a, const InferenceRequest& b) {
            return a.priority < b.priority;
        });
}

InferenceEngine::~InferenceEngine() {
    shutdown();
}

bool InferenceEngine::initialize(int numWorkers) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->running) return false;
    
    if (numWorkers <= 0) {
        numWorkers = std::thread::hardware_concurrency();
        if (numWorkers < 1) numWorkers = 1;
    }
    
    impl_->running = true;
    
    for (int i = 0; i < numWorkers; i++) {
        impl_->workers.emplace_back(&Impl::workerLoop, impl_.get());
    }
    
    return true;
}

void InferenceEngine::shutdown() {
    impl_->running = false;
    impl_->cv.notify_all();
    
    for (auto& worker : impl_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    impl_->workers.clear();
}

std::string InferenceEngine::runSyncText(const std::string& modelId, const std::string& prompt,
                                         const std::map<std::string, float>& params) {
     InferenceRequest req;
     req.modelId = modelId;
     req.inputType = InputType::TEXT;
     req.inputText = prompt;
     req.parameters = params;

     std::string requestId = submitRequest(req);
     if (requestId.empty()) return "";

     uint64_t timeoutMs = req.timeout;
     if (timeoutMs == 0) timeoutMs = impl_->defaultTimeout;

     auto start = std::chrono::steady_clock::now();
     while (true) {
         InferenceResult r = getResult(requestId);
         if (r.status == InferenceStatus::COMPLETED) return r.outputText;
         if (r.status == InferenceStatus::FAILED) return "";
         if (r.status == InferenceStatus::CANCELLED) return "";

         if (timeoutMs > 0) {
             auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - start).count();
            if (static_cast<uint64_t>(elapsed) >= timeoutMs) {
                return "";
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string InferenceEngine::runStreamText(const std::string& modelId, const std::string& prompt,
                                           std::function<void(const std::string&)> streamCallback,
                                           const std::map<std::string, float>& params) {
    InferenceRequest req;
    req.modelId = modelId;
    req.inputType = InputType::TEXT;
    req.inputText = prompt;
    req.parameters = params;
    req.streamCallback = streamCallback;

    std::string requestId = submitRequest(req);
    if (requestId.empty()) return "";

    uint64_t timeoutMs = req.timeout;
    if (timeoutMs == 0) timeoutMs = impl_->defaultTimeout;

    auto start = std::chrono::steady_clock::now();
    while (true) {
        InferenceResult r = getResult(requestId);
        if (r.status == InferenceStatus::COMPLETED) return r.outputText;
        if (r.status == InferenceStatus::FAILED) return "";
        if (r.status == InferenceStatus::CANCELLED) return "";

        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (static_cast<uint64_t>(elapsed) >= timeoutMs) {
                 return "";
             }
         }

         std::this_thread::sleep_for(std::chrono::milliseconds(1));
     }
}

std::string InferenceEngine::submitRequest(const InferenceRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->requestQueue.size() >= static_cast<size_t>(impl_->maxQueueSize)) {
        return "";
    }
    
    InferenceRequest req = request;
    if (req.requestId.empty()) {
        req.requestId = impl_->generateRequestId();
    }
    if (req.timestamp == 0) {
        req.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    if (req.timeout == 0) {
        req.timeout = impl_->defaultTimeout;
    }
    
    impl_->requestQueue.push(req);
    impl_->cv.notify_one();
    
    return req.requestId;
}

bool InferenceEngine::cancelRequest(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(impl_->resultMtx);
    
    auto it = impl_->results.find(requestId);
    if (it != impl_->results.end() && it->second.status == InferenceStatus::QUEUED) {
        it->second.status = InferenceStatus::CANCELLED;
        return true;
    }
    
    return false;
}

InferenceResult InferenceEngine::getResult(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(impl_->resultMtx);
    
    auto it = impl_->results.find(requestId);
    if (it != impl_->results.end()) {
        return it->second;
    }
    
    InferenceResult empty;
    empty.requestId = requestId;
    empty.status = InferenceStatus::QUEUED;
    return empty;
}

InferenceStatus InferenceEngine::getStatus(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(impl_->resultMtx);
    
    auto it = impl_->results.find(requestId);
    if (it != impl_->results.end()) {
        return it->second.status;
    }
    
    return InferenceStatus::QUEUED;
}

bool InferenceEngine::loadModel(const std::string& modelId, const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
#ifdef USE_LLAMA_CPP
    auto acquire = []() {
        std::call_once(g_llamaInitFlag, []() {
            ggml_backend_load_all();
            llama_backend_init();
        });
        std::lock_guard<std::mutex> guard(g_llamaRefMutex);
        g_llamaRefCount++;
    };
    auto release = []() {
        std::lock_guard<std::mutex> guard(g_llamaRefMutex);
        if (g_llamaRefCount > 0) {
            g_llamaRefCount--;
            if (g_llamaRefCount == 0) {
                llama_backend_free();
            }
        }
    };
    acquire();
    llama_model_params modelParams = llama_model_default_params();
    llama_model* model = llama_model_load_from_file(path.c_str(), modelParams);
    if (!model) {
        release();
        return false;
    }
    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx = 0;
    llama_context* ctx = llama_init_from_model(model, ctxParams);
    if (!ctx) {
        llama_model_free(model);
        release();
        return false;
    }
    LoadedModelState state;
    state.modelId = modelId;
    state.modelPath = path;
    state.contextLength = 0;
    state.maxContextLength = ctxParams.n_ctx ? ctxParams.n_ctx : static_cast<uint64_t>(llama_model_n_ctx_train(model));
    state.isLoaded = true;
    state.llamaModel = model;
    state.llamaContext = ctx;
    impl_->models[modelId] = std::move(state);
    return true;
#else
    LoadedModelState state;
    state.modelId = modelId;
    state.contextLength = 0;
    state.maxContextLength = 4096;
    state.isLoaded = true;
    impl_->models[modelId] = state;
    return true;
#endif
}

bool InferenceEngine::unloadModel(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->models.find(modelId);
    if (it != impl_->models.end()) {
#ifdef USE_LLAMA_CPP
        if (it->second.llamaContext) {
            llama_free(it->second.llamaContext);
            it->second.llamaContext = nullptr;
        }
        if (it->second.llamaModel) {
            llama_model_free(it->second.llamaModel);
            it->second.llamaModel = nullptr;
        }
#endif
        it->second.weights.clear();
        it->second.biases.clear();
        it->second.kvCache.clear();
        it->second.isLoaded = false;
        impl_->models.erase(it);
        return true;
    }
    
    return false;
}

bool InferenceEngine::isModelLoaded(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->models.find(modelId);
    return it != impl_->models.end() && it->second.isLoaded;
}

void InferenceEngine::setMaxBatchSize(int size) {
    impl_->maxBatchSize = size;
}

void InferenceEngine::setMaxQueueSize(int size) {
    impl_->maxQueueSize = size;
}

void InferenceEngine::setDefaultTimeout(uint64_t ms) {
    impl_->defaultTimeout = ms;
}

size_t InferenceEngine::getQueueSize() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->requestQueue.size();
}

size_t InferenceEngine::getActiveRequests() const {
    return impl_->activeRequests;
}

uint64_t InferenceEngine::getTotalInferences() const {
    return impl_->totalInferences;
}

double InferenceEngine::getAverageLatency() const {
    uint64_t total = impl_->totalInferences;
    if (total == 0) return 0;
    return static_cast<double>(impl_->totalLatency) / total;
}

void InferenceEngine::Impl::workerLoop() {
    while (running) {
        InferenceRequest request;
        
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !requestQueue.empty() || !running;
            });
            
            if (!running && requestQueue.empty()) break;
            
            if (requestQueue.empty()) continue;
            
            request = requestQueue.top();
            requestQueue.pop();
        }
        
        activeRequests++;
        
        InferenceResult result = processRequest(request);
        
        {
            std::lock_guard<std::mutex> lock(resultMtx);
            results[request.requestId] = result;
        }
        
        if (request.callback) {
            request.callback(request.requestId);
        }
        
        activeRequests--;
        totalInferences++;
        totalLatency += (result.endTime - result.startTime);
    }
}

InferenceResult InferenceEngine::Impl::processRequest(const InferenceRequest& request) {
    InferenceResult result;
    result.requestId = request.requestId;
    result.modelId = request.modelId;
    result.startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    auto modelIt = models.find(request.modelId);
    if (modelIt == models.end() || !modelIt->second.isLoaded) {
        result.status = InferenceStatus::FAILED;
        result.errorMessage = "Model not loaded: " + request.modelId;
        result.endTime = result.startTime;
        return result;
    }
    
    const LoadedModelState& model = modelIt->second;
    
    try {
        if (request.inputType == InputType::TEXT) {
            float temperature = 0.7f;
            auto tempIt = request.parameters.find("temperature");
            if (tempIt != request.parameters.end()) {
                temperature = tempIt->second;
            }
            
            int maxTokens = 512;
            auto maxIt = request.parameters.find("max_tokens");
            if (maxIt != request.parameters.end()) {
                maxTokens = static_cast<int>(maxIt->second);
            }
            
            std::map<std::string, float> params;
            params["temperature"] = temperature;
            params["max_tokens"] = static_cast<float>(maxTokens);
            
            // Use streaming if callback is provided, otherwise use synchronous generation
            if (request.streamCallback) {
                int tg = 0; float conf = 0.0f;
                result.outputText = generateTextStream(model, request.inputText, request.streamCallback, params, &tg, &conf);
                result.tokensGenerated = static_cast<uint32_t>(tg);
                result.confidence = conf;
            } else {
                int tg = 0; float conf = 0.0f;
                result.outputText = generateText(model, request.inputText, params, &tg, &conf);
                result.tokensGenerated = static_cast<uint32_t>(tg);
                result.confidence = conf;
            }
            // If backend is not available the generators will return a specific error string and zero tokens
            if (result.tokensGenerated == 0 && result.outputText.rfind("[Model backend not enabled", 0) == 0) {
                result.status = InferenceStatus::FAILED;
                result.errorMessage = result.outputText;
            } else {
                result.status = InferenceStatus::COMPLETED;
            }
        } else if (request.inputType == InputType::EMBEDDING) {
            result.outputData = runForward(model, request.inputData);
            result.confidence = 1.0f;
            result.status = InferenceStatus::COMPLETED;
        } else if (request.inputType == InputType::TENSOR) {
            result.outputData = runForward(model, request.inputData);
            
            std::vector<float> probs = softmax(result.outputData);
            
            std::vector<std::pair<float, int>> indexed;
            for (size_t i = 0; i < probs.size(); i++) {
                indexed.push_back({probs[i], static_cast<int>(i)});
            }
            std::sort(indexed.begin(), indexed.end(), std::greater<std::pair<float, int>>());
            
            for (int i = 0; i < 5 && i < static_cast<int>(indexed.size()); i++) {
                result.topK.push_back({"class_" + std::to_string(indexed[i].second), indexed[i].first});
            }
            
            result.confidence = indexed.empty() ? 0.0f : indexed[0].first;
            result.status = InferenceStatus::COMPLETED;
        } else {
            result.status = InferenceStatus::FAILED;
            result.errorMessage = "Unsupported input type";
        }
    } catch (const std::exception& e) {
        result.status = InferenceStatus::FAILED;
        result.errorMessage = e.what();
    }
    
    result.endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return result;
}

std::vector<float> InferenceEngine::Impl::runForward(const LoadedModelState& model,
                                                      const std::vector<float>& input) {
    std::vector<float> hidden = input;
    
    for (size_t layer = 0; layer < model.weights.size(); layer++) {
        hidden = matmul(hidden, model.weights);
        
        if (layer < model.biases.size()) {
            for (size_t i = 0; i < hidden.size() && i < model.biases.size(); i++) {
                hidden[i] += model.biases[i];
            }
        }
        
        hidden = gelu(hidden);
        hidden = layerNorm(hidden);
    }
    
    return hidden;
}

std::vector<float> InferenceEngine::Impl::softmax(const std::vector<float>& logits) {
    if (logits.empty()) return {};
    
    float maxVal = *std::max_element(logits.begin(), logits.end());
    
    std::vector<float> exp_vals(logits.size());
    float sum = 0.0f;
    
    for (size_t i = 0; i < logits.size(); i++) {
        exp_vals[i] = std::exp(logits[i] - maxVal);
        sum += exp_vals[i];
    }
    
    for (size_t i = 0; i < exp_vals.size(); i++) {
        exp_vals[i] /= sum;
    }
    
    return exp_vals;
}

std::vector<float> InferenceEngine::Impl::layerNorm(const std::vector<float>& input, float eps) {
    if (input.empty()) return {};
    
    float mean = std::accumulate(input.begin(), input.end(), 0.0f) / input.size();
    
    float variance = 0.0f;
    for (float val : input) {
        variance += (val - mean) * (val - mean);
    }
    variance /= input.size();
    
    float stddev = std::sqrt(variance + eps);
    
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        output[i] = (input[i] - mean) / stddev;
    }
    
    return output;
}

std::vector<float> InferenceEngine::Impl::gelu(const std::vector<float>& input) {
    std::vector<float> output(input.size());
    
    const float sqrt2 = std::sqrt(2.0f);
    
    for (size_t i = 0; i < input.size(); i++) {
        float x = input[i];
        output[i] = 0.5f * x * (1.0f + std::tanh(sqrt2 / std::sqrt(M_PI) * (x + 0.044715f * x * x * x)));
    }
    
    return output;
}

std::vector<float> InferenceEngine::Impl::matmul(const std::vector<float>& a,
                                                  const std::vector<std::vector<float>>& b) {
    if (b.empty() || b[0].empty()) return a;
    
    size_t m = 1;
    size_t k = a.size();
    size_t n = b[0].size();
    
    std::vector<float> result(n, 0.0f);
    
    for (size_t j = 0; j < n; j++) {
        for (size_t i = 0; i < k && i < b.size(); i++) {
            if (j < b[i].size()) {
                result[j] += a[i] * b[i][j];
            }
        }
    }
    
    return result;
}

std::vector<float> InferenceEngine::Impl::attention(const std::vector<float>& q,
                                                     const std::vector<float>& k,
                                                     const std::vector<float>& v,
                                                     int numHeads) {
    size_t seqLen = q.size() / numHeads;
    size_t headDim = q.size() / (seqLen * numHeads);
    
    std::vector<float> output(q.size(), 0.0f);
    
    float scale = 1.0f / std::sqrt(static_cast<float>(headDim));
    
    for (int h = 0; h < numHeads; h++) {
        for (size_t i = 0; i < seqLen; i++) {
            std::vector<float> scores(seqLen, 0.0f);
            
            for (size_t j = 0; j < seqLen; j++) {
                float score = 0.0f;
                for (size_t d = 0; d < headDim; d++) {
                    size_t qIdx = h * seqLen * headDim + i * headDim + d;
                    size_t kIdx = h * seqLen * headDim + j * headDim + d;
                    if (qIdx < q.size() && kIdx < k.size()) {
                        score += q[qIdx] * k[kIdx];
                    }
                }
                scores[j] = score * scale;
            }
            
            scores = softmax(scores);
            
            for (size_t d = 0; d < headDim; d++) {
                float val = 0.0f;
                for (size_t j = 0; j < seqLen; j++) {
                    size_t vIdx = h * seqLen * headDim + j * headDim + d;
                    if (vIdx < v.size()) {
                        val += scores[j] * v[vIdx];
                    }
                }
                size_t outIdx = h * seqLen * headDim + i * headDim + d;
                if (outIdx < output.size()) {
                    output[outIdx] = val;
                }
            }
        }
    }
    
    return output;
}

std::string InferenceEngine::Impl::generateText(const LoadedModelState& model,
                                                 const std::string& prompt,
                                                 const std::map<std::string, float>& params,
                                                 int* outTokensGenerated,
                                                 float* outConfidence) {
    float temperature = 0.7f;
    int maxTokens = 512;
    
    auto tempIt = params.find("temperature");
    if (tempIt != params.end()) temperature = tempIt->second;
    
    auto maxIt = params.find("max_tokens");
    if (maxIt != params.end()) maxTokens = static_cast<int>(maxIt->second);

#ifdef USE_LLAMA_CPP
    if (model.llamaModel && model.llamaContext) {
        llama_context* ctx = model.llamaContext;
        const llama_model* mdl = model.llamaModel;
        const llama_vocab* vocab = llama_model_get_vocab(mdl);
        
        std::vector<llama_token> tokens(prompt.size() + 16);
        int nTokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                      tokens.data(), tokens.size(), true, true);
        if (nTokens < 0) {
            tokens.resize(-nTokens);
            nTokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                      tokens.data(), tokens.size(), true, true);
        }
        if (nTokens < 0) return "[Tokenization failed]";
        tokens.resize(nTokens);
        
        llama_memory_t mem = llama_get_memory(ctx);
        if (mem) {
            llama_memory_clear(mem, true);
        }
        
        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
        
        if (llama_decode(ctx, batch) != 0) {
            return "[Decode failed]";
        }
        
        std::string result;
        int nCur = tokens.size();
        int initial_count = nCur;
        
        llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(temperature));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));
        
        int lastSampled = -1;
        for (int i = 0; i < maxTokens; i++) {
            llama_token newToken = llama_sampler_sample(sampler, ctx, -1);
            
            if (llama_vocab_is_eog(vocab, newToken)) break;
            
            char buf[128];
            int len = llama_token_to_piece(vocab, newToken, buf, sizeof(buf), 0, true);
            if (len > 0) {
                result.append(buf, len);
            }
            
            llama_batch nextBatch = llama_batch_get_one(&newToken, 1);
            nCur++;
            lastSampled = newToken;
            
            if (llama_decode(ctx, nextBatch) != 0) break;
        }
        
        // compute tokensGenerated and confidence if requested
        if (outTokensGenerated) *outTokensGenerated = nCur - initial_count;
        float conf = 0.0f;
        if (lastSampled >= 0) {
            const float * logits = llama_get_logits_ith(ctx, nCur - 1);
            int n_vocab = llama_vocab_n_tokens(vocab);
            if (logits && n_vocab > 0) {
                float maxLogit = logits[0];
                for (int vi = 1; vi < n_vocab; ++vi) if (logits[vi] > maxLogit) maxLogit = logits[vi];
                double sum = 0.0;
                for (int vi = 0; vi < n_vocab; ++vi) sum += std::exp(static_cast<double>(logits[vi] - maxLogit));
                double p = std::exp(static_cast<double>(logits[lastSampled] - maxLogit)) / sum;
                conf = static_cast<float>(p);
            }
        }
        if (outConfidence) *outConfidence = conf;
        llama_sampler_free(sampler);
        
        return result;
    }
#endif
    // Non-llama.cpp fallback: report backend unavailable and do not fabricate tokens
    if (outTokensGenerated) *outTokensGenerated = 0;
    if (outConfidence) *outConfidence = 0.0f;
    return std::string("[Model backend not enabled: build with USE_LLAMA_CPP]");
}

std::string InferenceEngine::Impl::generateTextStream(const LoadedModelState& model,
                                                      const std::string& prompt,
                                                      std::function<void(const std::string&)> streamCallback,
                                                      const std::map<std::string, float>& params,
                                                      int* outTokensGenerated,
                                                      float* outConfidence) {
    float temperature = 0.7f;
    int maxTokens = 512;
    
    auto tempIt = params.find("temperature");
    if (tempIt != params.end()) temperature = tempIt->second;
    
    auto maxIt = params.find("max_tokens");
    if (maxIt != params.end()) maxTokens = static_cast<int>(maxIt->second);

#ifdef USE_LLAMA_CPP
    if (model.llamaModel && model.llamaContext && streamCallback) {
        llama_context* ctx = model.llamaContext;
        const llama_model* mdl = model.llamaModel;
        const llama_vocab* vocab = llama_model_get_vocab(mdl);
        
        std::vector<llama_token> tokens(prompt.size() + 16);
        int nTokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                      tokens.data(), tokens.size(), true, true);
        if (nTokens < 0) {
            tokens.resize(-nTokens);
            nTokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                      tokens.data(), tokens.size(), true, true);
        }
        if (nTokens < 0) return "[Tokenization failed]";
        tokens.resize(nTokens);
        
        llama_memory_t mem = llama_get_memory(ctx);
        if (mem) {
            llama_memory_clear(mem, true);
        }
        
        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
        
        if (llama_decode(ctx, batch) != 0) {
            return "[Decode failed]";
        }
        
        std::string result;
        int nCur = tokens.size();
        int initial_count = nCur;
        
        llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(temperature));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));
        
        int lastSampled = -1;
        for (int i = 0; i < maxTokens; i++) {
            llama_token newToken = llama_sampler_sample(sampler, ctx, -1);
            
            if (llama_vocab_is_eog(vocab, newToken)) break;
            
            char buf[128];
            int len = llama_token_to_piece(vocab, newToken, buf, sizeof(buf), 0, true);
            if (len > 0) {
                std::string tokenStr(buf, len);
                result.append(tokenStr);
                
                // Stream the token to the callback
                if (streamCallback) {
                    streamCallback(tokenStr);
                }
            }
            
            llama_batch nextBatch = llama_batch_get_one(&newToken, 1);
            nCur++;
            lastSampled = newToken;
            
            if (llama_decode(ctx, nextBatch) != 0) break;
        }
        
        // compute tokensGenerated and confidence if requested
        if (outTokensGenerated) *outTokensGenerated = nCur - initial_count;
        float conf = 0.0f;
        if (lastSampled >= 0) {
            const float * logits = llama_get_logits_ith(ctx, nCur - 1);
            int n_vocab = llama_vocab_n_tokens(vocab);
            if (logits && n_vocab > 0) {
                float maxLogit = logits[0];
                for (int vi = 1; vi < n_vocab; ++vi) if (logits[vi] > maxLogit) maxLogit = logits[vi];
                double sum = 0.0;
                for (int vi = 0; vi < n_vocab; ++vi) sum += std::exp(static_cast<double>(logits[vi] - maxLogit));
                double p = std::exp(static_cast<double>(logits[lastSampled] - maxLogit)) / sum;
                conf = static_cast<float>(p);
            }
        }
        if (outConfidence) *outConfidence = conf;
        llama_sampler_free(sampler);
        
        return result;
    }
#endif
    // Fallback: non-llama.cpp streaming generation - do not fabricate output
    if (outTokensGenerated) *outTokensGenerated = 0;
    if (outConfidence) *outConfidence = 0.0f;
    if (streamCallback) {
        // Optionally notify the caller of the failure via a stream token
        streamCallback(std::string("[Model backend not enabled: build with USE_LLAMA_CPP]"));
    }
    return std::string("[Model backend not enabled: build with USE_LLAMA_CPP]");
}

std::vector<int> InferenceEngine::Impl::tokenize(const std::string& text) {
    std::vector<int> tokens;
    
    for (size_t i = 0; i < text.length(); i++) {
        tokens.push_back(static_cast<int>(static_cast<unsigned char>(text[i])));
    }
    
    return tokens;
}

std::string InferenceEngine::Impl::detokenize(const std::vector<int>& tokens) {
    std::string text;
    
    for (int token : tokens) {
        if (token > 0 && token < 256) {
            text += static_cast<char>(token);
        }
    }
    
    return text;
}

std::string InferenceEngine::Impl::generateRequestId() {
    static std::atomic<uint64_t> counter{0};
    
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::stringstream ss;
    ss << std::hex << now << "-" << counter++;
    
    return ss.str();
}

}
}
