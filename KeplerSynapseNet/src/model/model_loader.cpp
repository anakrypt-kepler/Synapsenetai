#include "model/model_loader.h"
#include "model/model_inference.h"
#include "utils/sleep_inhibitor.h"
#include "utils/logger.h"
#include "utils/config.h"
#include <fstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <sys/resource.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#ifdef USE_LLAMA_CPP
#include "llama.h"
#endif

namespace synapse {
namespace model {

static constexpr size_t FALLBACK_DIM = 256;

#ifdef USE_LLAMA_CPP
namespace {

std::once_flag gLlamaLogOnce;

void llamaLogCallback(ggml_log_level level, const char* text, void* user_data) {
    (void)user_data;
    if (!text || !*text) return;
    std::string s(text);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.empty()) return;

    utils::LogLevel outLevel = utils::LogLevel::INFO;
    if (level == GGML_LOG_LEVEL_ERROR) outLevel = utils::LogLevel::ERROR;
    else if (level == GGML_LOG_LEVEL_WARN) outLevel = utils::LogLevel::WARN;
    else if (level == GGML_LOG_LEVEL_DEBUG) outLevel = utils::LogLevel::DEBUG;
    utils::Logger::log(outLevel, "llama", s);
}

}
#endif

struct ModelLoader::Impl {
    ModelInfo info;
    ModelState state = ModelState::UNLOADED;
    LoaderConfig config;
    LoaderStats stats{};
    std::string error;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> generating{false};
    std::atomic<bool> downloading{false};
    mutable std::mutex mtx;
    uint64_t loadStartTime = 0;
    uint32_t contextSize = 2048;
    uint32_t threads = 4;
    std::unique_ptr<InferenceEngine> engine;
#ifdef USE_LLAMA_CPP
    llama_model* llamaModel = nullptr;
    llama_context* llamaCtx = nullptr;
#endif
    
    std::function<void(ModelState)> stateCallback;
    std::function<void(double)> progressCallback;
    std::function<void(const std::string&)> errorCallback;
    
    void setState(ModelState newState) {
        state = newState;
        if (stateCallback) stateCallback(newState);
    }
    
    void setError(const std::string& msg) {
        error = msg;
        setState(ModelState::ERROR);
        if (errorCallback) errorCallback(msg);
    }
};

// Helper: Get available system memory in bytes
static uint64_t getAvailableMemory() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        return static_cast<uint64_t>(ru.ru_maxrss) * 1024;  // maxrss in KB on Unix
    }
    return 0;
}

// Helper: Format bytes to human-readable string
static std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = bytes;
    int unit = 0;
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

// Helper: Get free disk space
static uint64_t getFreeDiskSpace(const std::string& path) {
    try {
        auto space = std::filesystem::space(std::filesystem::path(path).parent_path());
        return space.available;
    } catch (...) {
        return 0;
    }
}

static std::string shellEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out.append("'\\''");
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static std::string trimWS(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

static int decodeSystemExitCode(int rc) {
#ifdef _WIN32
    return rc;
#else
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    if (WIFSIGNALED(rc)) return 128 + WTERMSIG(rc);
    return rc;
#endif
}

static std::string readSmallFile(const std::string& path, size_t maxBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string out;
    out.resize(maxBytes);
    in.read(out.data(), static_cast<std::streamsize>(maxBytes));
    out.resize(static_cast<size_t>(in.gcount()));
    return out;
}

static std::string defaultModelsDir() {
    const char* home = std::getenv("HOME");
    if (home && *home) {
        try {
            std::filesystem::path homeModels = std::filesystem::path(home) / ".synapsenet" / "models";
            std::error_code ec;
            std::filesystem::create_directories(homeModels, ec);
            if (!ec) return homeModels.string();
        } catch (...) {
        }
    }
    try {
        std::filesystem::path cwdModels = std::filesystem::current_path() / "models";
        std::error_code ec;
        std::filesystem::create_directories(cwdModels, ec);
        if (!ec) return cwdModels.string();
    } catch (...) {
    }
    return "models";
}

static std::string modelUrlForName(const std::string& name) {
    if (name.rfind("http://", 0) == 0 || name.rfind("https://", 0) == 0) return name;
    if (name == "llama-2-7b-chat.Q2_K.gguf") {
        return "https://huggingface.co/TheBloke/Llama-2-7B-Chat-GGUF/resolve/main/llama-2-7b-chat.Q2_K.gguf";
    }
    if (name == "llama-2-7b-chat.Q4_K_M.gguf") {
        return "https://huggingface.co/TheBloke/Llama-2-7B-Chat-GGUF/resolve/main/llama-2-7b-chat.Q4_K_M.gguf";
    }
    if (name == "tinyllama-1.1b-chat-v1.0.Q2_K.gguf") {
        return "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q2_K.gguf";
    }
    if (name == "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf") {
        return "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf";
    }
    if (name == "deepseek-coder-6.7b-instruct.Q4_K_M.gguf") {
        return "https://huggingface.co/TheBloke/deepseek-coder-6.7B-instruct-GGUF/resolve/main/deepseek-coder-6.7b-instruct.Q4_K_M.gguf";
    }
    return {};
}

static uint64_t tryFetchContentLengthCurl(const std::string& url) {
#ifndef _WIN32
    std::string cmd = "curl -sI -L " + shellEscape(url);
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return 0;
    char buf[4096];
    uint64_t len = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        std::string lower;
        lower.reserve(line.size());
        for (char c : line) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        const std::string k = "content-length:";
        if (lower.rfind(k, 0) == 0) {
            std::string v = trimWS(line.substr(k.size()));
            try {
                len = static_cast<uint64_t>(std::stoull(v));
            } catch (...) {
                len = 0;
            }
        }
    }
    pclose(fp);
    return len;
#else
    (void)url;
    return 0;
#endif
}

ModelLoader::ModelLoader() : impl_(std::make_unique<Impl>()) {}

ModelLoader::~ModelLoader() { unload(); }

bool ModelLoader::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->state == ModelState::READY) unload();
    
    impl_->state = ModelState::LOADING;
    impl_->error.clear();
    
    // Check file existence
    if (!std::filesystem::exists(path)) {
        std::string msg = "Model file not found: " + path;
        impl_->error = msg;
        impl_->state = ModelState::ERROR;
        utils::Logger::error(msg);
        return false;
    }
    
    // Log and validate file size
    uint64_t fileSize = std::filesystem::file_size(path);
    if (fileSize < 1024 * 1024) {
        std::ostringstream oss;
        oss << "Model file too small: " << path << " (" << formatBytes(fileSize) << ")";
        impl_->error = oss.str();
        impl_->state = ModelState::ERROR;
        utils::Logger::error(impl_->error);
        return false;
    }
    {
        std::string filename = std::filesystem::path(path).filename().string();
        if (filename.rfind("ggml-vocab-", 0) == 0) {
            impl_->error = "Not a model (vocab-only GGUF): " + filename;
            impl_->state = ModelState::ERROR;
            utils::Logger::error(impl_->error);
            return false;
        }
    }
    utils::Logger::info("Loading model: " + path + " (" + formatBytes(fileSize) + ")");
    
    impl_->info.path = path;
    impl_->info.name = std::filesystem::path(path).filename().string();
    
    std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ".gguf") {
        impl_->info.format = ModelFormat::GGUF;
    } else if (ext == ".ggml" || ext == ".bin") {
        impl_->info.format = ModelFormat::GGML;
    } else {
        impl_->info.format = ModelFormat::UNKNOWN;
    }
    
    impl_->info.sizeBytes = fileSize;
    impl_->info.access = AccessMode::PRIVATE;
    impl_->info.contextSize = impl_->contextSize;

#ifdef USE_LLAMA_CPP
    std::call_once(gLlamaLogOnce, []() {
        llama_log_set(llamaLogCallback, nullptr);
    });

    ggml_backend_load_all();
    llama_backend_init();
    
    llama_model_params modelParams = llama_model_default_params();
    impl_->llamaModel = llama_model_load_from_file(path.c_str(), modelParams);
    if (!impl_->llamaModel) {
        std::ostringstream oss;
        oss << "Failed to load llama model: " << path 
            << "\n  File size: " << formatBytes(fileSize)
            << "\n  Context size: " << impl_->contextSize
            << "\n  Threads: " << impl_->threads;
        impl_->error = oss.str();
        impl_->state = ModelState::ERROR;
        utils::Logger::error(impl_->error);
        return false;
    }
    
    impl_->info.layers = llama_model_n_layer(impl_->llamaModel);
    impl_->info.embeddingSize = llama_model_n_embd(impl_->llamaModel);
    if (impl_->info.layers == 0) {
        llama_model_free(impl_->llamaModel);
        impl_->llamaModel = nullptr;
        impl_->error = "GGUF loaded but has 0 layers (not an LLM model): " + path;
        impl_->state = ModelState::ERROR;
        utils::Logger::error(impl_->error);
        return false;
    }
    
    const llama_vocab* vocab = llama_model_get_vocab(impl_->llamaModel);
    impl_->info.vocabSize = llama_vocab_n_tokens(vocab);
    impl_->info.maxContextSize = llama_model_n_ctx_train(impl_->llamaModel);
    
    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx = impl_->contextSize;
    ctxParams.n_threads = impl_->threads;
    ctxParams.embeddings = true;
    
    // Log progress
    if (impl_->progressCallback) {
        impl_->progressCallback(0.5);
    }
    utils::Logger::info("Creating context: n_ctx=" + std::to_string(impl_->contextSize) + 
                       ", threads=" + std::to_string(impl_->threads));
    
    impl_->llamaCtx = llama_init_from_model(impl_->llamaModel, ctxParams);
    if (!impl_->llamaCtx) {
        llama_model_free(impl_->llamaModel);
        impl_->llamaModel = nullptr;
        std::ostringstream oss;
        oss << "Failed to create llama context"
            << "\n  Context size: " << impl_->contextSize
            << "\n  Threads: " << impl_->threads
            << "\n  Model: " << path;
        impl_->error = oss.str();
        impl_->state = ModelState::ERROR;
        utils::Logger::error(impl_->error);
        return false;
    }
#else
    impl_->info.layers = 32;
    impl_->info.embeddingSize = static_cast<uint32_t>(FALLBACK_DIM);
    impl_->info.vocabSize = 32000;
    utils::Logger::warn("Compiled without USE_LLAMA_CPP: using fallback mode");
#endif
    
    impl_->info.architecture = "llama";
    impl_->info.quantization = "Q4_K_M";
    
    // Log progress
    if (impl_->progressCallback) {
        impl_->progressCallback(0.75);
    }
    utils::Logger::info("Initializing inference engine");
    impl_->engine.reset();

    // Log success
    if (impl_->progressCallback) {
        impl_->progressCallback(1.0);
    }
    utils::Logger::info("Model loaded successfully: " + impl_->info.name + 
                       " (" + formatBytes(fileSize) + "), " +
                       std::to_string(impl_->info.layers) + " layers");
    impl_->state = ModelState::READY;
    return true;
}

bool ModelLoader::load(const std::string& path, const LoaderConfig& config) {
    impl_->config = config;
    impl_->contextSize = config.contextSize;
    impl_->threads = config.threads;
    return load(path);
}

bool ModelLoader::loadFromHub(const std::string& modelId) {
    std::string localPath = impl_->config.cacheDir + "/" + modelId + ".gguf";
    if (!std::filesystem::exists(localPath)) {
        if (!downloadModel(modelId)) {
            return false;
        }
    }
    return load(localPath);
}

bool ModelLoader::unload() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->state == ModelState::UNLOADED) return true;
    
    impl_->stopRequested = true;
    utils::Logger::info("Unloading model: " + impl_->info.name);
    
    if (impl_->engine) {
        impl_->engine->unloadModel(impl_->info.name);
        impl_->engine.reset();
    }
#ifdef USE_LLAMA_CPP
    if (impl_->llamaCtx) {
        llama_free(impl_->llamaCtx);
        impl_->llamaCtx = nullptr;
    }
    if (impl_->llamaModel) {
        llama_model_free(impl_->llamaModel);
        impl_->llamaModel = nullptr;
    }
#endif
    impl_->state = ModelState::UNLOADED;
    impl_->info = ModelInfo{};
    return true;
}

bool ModelLoader::reload() {
    std::string path = impl_->info.path;
    unload();
    return load(path);
}

bool ModelLoader::isLoaded() const {
    return impl_->state == ModelState::READY;
}

ModelState ModelLoader::getState() const {
    return impl_->state;
}

ModelInfo ModelLoader::getInfo() const {
    return impl_->info;
}

std::string ModelLoader::getError() const {
    return impl_->error;
}

LoaderStats ModelLoader::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->stats;
}

std::string ModelLoader::generate(const std::string& prompt, const GenerationParams& params) {
    if (impl_->state != ModelState::READY) {
        std::string err = "[Error: Model not loaded. State: " + std::to_string(static_cast<int>(impl_->state)) + "]";
        utils::Logger::error(err);
        return err;
    }
    
    utils::Logger::debug("Generating text from prompt: " + prompt.substr(0, 50) + "...");
    
    std::string response;
    generateStream(prompt, [&response](const std::string& token) {
        response += token;
        return true;
    }, params);
    
    utils::Logger::debug("Generation complete: " + std::to_string(response.length()) + " characters");
    return response;
}

std::string ModelLoader::chat(const std::vector<std::pair<std::string, std::string>>& messages,
                               const GenerationParams& params) {
    std::string prompt;
    for (const auto& [role, content] : messages) {
        prompt += role + ": " + content + "\n";
    }
    return generate(prompt, params);
}

void ModelLoader::generateStream(const std::string& prompt, 
                                  std::function<bool(const std::string&)> callback,
                                  const GenerationParams& params) {
    if (impl_->state != ModelState::READY) {
        callback("[Error: Model not loaded]");
        return;
    }
    
    impl_->stopRequested = false;
    impl_->generating = true;

#ifdef USE_LLAMA_CPP
    {
        llama_context* ctx = nullptr;
        const llama_model* mdl = nullptr;
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            if (impl_->llamaModel && impl_->llamaCtx) {
                mdl = impl_->llamaModel;
                ctx = impl_->llamaCtx;
            }
        }

        if (mdl && ctx) {
            const llama_vocab* vocab = llama_model_get_vocab(mdl);

            std::vector<llama_token> tokens(prompt.size() + 16);
            int nTokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), true, true);
            if (nTokens < 0) {
                tokens.resize(static_cast<size_t>(-nTokens));
                nTokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), tokens.size(), true, true);
            }
            if (nTokens < 0) {
                callback("[Tokenization failed]");
                impl_->generating = false;
                return;
            }
            tokens.resize(static_cast<size_t>(nTokens));

            llama_memory_t mem = llama_get_memory(ctx);
            if (mem) llama_memory_clear(mem, true);

            uint32_t nCtx = llama_n_ctx(ctx);
            uint32_t nBatch = llama_n_batch(ctx);
            if (nBatch == 0) nBatch = 512;
            uint32_t maxPromptTokens = nCtx;
            uint32_t minPromptTokens = std::min<uint32_t>(256, nCtx > 1 ? (nCtx - 1) : 0);
            if (nCtx > params.maxTokens + 1) {
                maxPromptTokens = nCtx - params.maxTokens - 1;
            }
            if (maxPromptTokens < minPromptTokens) maxPromptTokens = minPromptTokens;
            if (maxPromptTokens > 0 && tokens.size() > maxPromptTokens) {
                tokens.erase(tokens.begin(), tokens.begin() + (tokens.size() - maxPromptTokens));
            }

            for (size_t i = 0; i < tokens.size();) {
                size_t n = std::min<size_t>(static_cast<size_t>(nBatch), tokens.size() - i);
                llama_batch batch = llama_batch_get_one(tokens.data() + i, static_cast<int32_t>(n));
                int32_t rc = llama_decode(ctx, batch);
                if (rc != 0) {
                    callback("[Decode failed]");
                    impl_->generating = false;
                    return;
                }
                i += n;
            }

            llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
            llama_sampler_chain_add(sampler, llama_sampler_init_temp(params.temperature));
            llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));

            for (uint32_t i = 0; i < params.maxTokens && !impl_->stopRequested; i++) {
                llama_token newToken = llama_sampler_sample(sampler, ctx, -1);
                if (llama_vocab_is_eog(vocab, newToken)) break;
                llama_sampler_accept(sampler, newToken);

                char buf[256];
                int len = llama_token_to_piece(vocab, newToken, buf, sizeof(buf), 0, true);
                if (len > 0) {
                    std::string tokenStr(buf, static_cast<size_t>(len));
                    callback(tokenStr);
                }

                llama_batch nextBatch = llama_batch_get_one(&newToken, 1);
                if (llama_decode(ctx, nextBatch) != 0) break;
            }

            llama_sampler_free(sampler);
        } else {
            callback("[Error: llama model not loaded]");
        }
    }
#else
    callback("[Error: compiled without USE_LLAMA_CPP]");
#endif
    
    impl_->generating = false;
}

void ModelLoader::stopGeneration() {
    impl_->stopRequested = true;
}

bool ModelLoader::isGenerating() const {
    return impl_->generating;
}

std::vector<float> ModelLoader::getEmbedding(const std::string& text) {
#ifdef USE_LLAMA_CPP
    if (impl_->llamaModel && impl_->llamaCtx) {
        const llama_vocab* vocab = llama_model_get_vocab(impl_->llamaModel);
        
        std::vector<llama_token> tokens(text.size() + 16);
        int nTokens = llama_tokenize(vocab, text.c_str(), text.size(),
                                      tokens.data(), tokens.size(), true, true);
        if (nTokens < 0) {
            tokens.resize(-nTokens);
            nTokens = llama_tokenize(vocab, text.c_str(), text.size(),
                                      tokens.data(), tokens.size(), true, true);
        }
        if (nTokens <= 0) {
            return std::vector<float>(impl_->info.embeddingSize, 0.0f);
        }
        tokens.resize(nTokens);
        
        llama_memory_t mem = llama_get_memory(impl_->llamaCtx);
        if (mem) {
            llama_memory_clear(mem, true);
        }

        uint32_t nCtx = llama_n_ctx(impl_->llamaCtx);
        uint32_t nBatch = llama_n_batch(impl_->llamaCtx);
        if (nBatch == 0) nBatch = 512;
        if (nCtx > 0 && tokens.size() > nCtx) {
            tokens.erase(tokens.begin(), tokens.begin() + (tokens.size() - nCtx));
        }

        for (size_t i = 0; i < tokens.size();) {
            size_t n = std::min<size_t>(static_cast<size_t>(nBatch), tokens.size() - i);
            llama_batch batch = llama_batch_get_one(tokens.data() + i, static_cast<int32_t>(n));
            if (llama_decode(impl_->llamaCtx, batch) != 0) {
                return std::vector<float>(impl_->info.embeddingSize, 0.0f);
            }
            i += n;
        }
        
        float* emb = llama_get_embeddings(impl_->llamaCtx);
        if (emb) {
            int embSize = llama_model_n_embd(impl_->llamaModel);
            std::vector<float> embedding(emb, emb + embSize);
            
            float norm = 0.0f;
            for (float v : embedding) norm += v * v;
            norm = std::sqrt(norm);
            if (norm > 0) {
                for (float& v : embedding) v /= norm;
            }
            
            return embedding;
        }
    }
#endif
    const size_t FALLBACK_DIM = 256;
    std::vector<float> embedding(FALLBACK_DIM, 0.0f);
    // Unigram counts
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        embedding[c % FALLBACK_DIM] += 1.0f;
    }
    // Bigram counts (simple hashed)
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(text[i]);
        unsigned char b = static_cast<unsigned char>(text[i + 1]);
        uint32_t h = (static_cast<uint32_t>(a) * 31u + static_cast<uint32_t>(b)) % FALLBACK_DIM;
        embedding[h] += 1.0f;
    }
    // L2 normalize
    float norm = 0.0f;
    for (float v : embedding) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-8f) {
        for (float& v : embedding) v /= norm;
    }
    // Ensure ModelInfo reflects fallback embedding size
    impl_->info.embeddingSize = static_cast<uint32_t>(FALLBACK_DIM);
    return embedding;
}

std::vector<std::vector<float>> ModelLoader::getEmbeddings(const std::vector<std::string>& texts) {
    std::vector<std::vector<float>> results;
    for (const auto& text : texts) {
        results.push_back(getEmbedding(text));
    }
    return results;
}

float ModelLoader::similarity(const std::string& text1, const std::string& text2) {
    auto emb1 = getEmbedding(text1);
    auto emb2 = getEmbedding(text2);
    float dot = 0, norm1 = 0, norm2 = 0;
    for (size_t i = 0; i < emb1.size() && i < emb2.size(); i++) {
        dot += emb1[i] * emb2[i];
        norm1 += emb1[i] * emb1[i];
        norm2 += emb2[i] * emb2[i];
    }
    return dot / (std::sqrt(norm1) * std::sqrt(norm2) + 1e-8f);
}

std::vector<uint32_t> ModelLoader::tokenize(const std::string& text) {
#ifdef USE_LLAMA_CPP
    if (impl_->llamaModel) {
        const llama_vocab* vocab = llama_model_get_vocab(impl_->llamaModel);
        
        std::vector<llama_token> tokens(text.size() + 16);
        int nTokens = llama_tokenize(vocab, text.c_str(), text.size(),
                                      tokens.data(), tokens.size(), false, false);
        if (nTokens < 0) {
            tokens.resize(-nTokens);
            nTokens = llama_tokenize(vocab, text.c_str(), text.size(),
                                      tokens.data(), tokens.size(), false, false);
        }
        if (nTokens > 0) {
            tokens.resize(nTokens);
            std::vector<uint32_t> result(tokens.begin(), tokens.end());
            return result;
        }
    }
#endif
    std::vector<uint32_t> tokens;
    size_t i = 0;
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;
    while (i < text.size()) {
        // Skip whitespace
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) i++;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && !(text[j] == ' ' || text[j] == '\t' || text[j] == '\n' || text[j] == '\r')) j++;
        // FNV-1a hash the token
        uint32_t h = FNV_OFFSET;
        for (size_t k = i; k < j; ++k) {
            h ^= static_cast<unsigned char>(text[k]);
            h *= FNV_PRIME;
        }
        tokens.push_back(h);
        i = j;
    }
    if (tokens.empty() && !text.empty()) {
        uint32_t h = FNV_OFFSET;
        for (char c : text) {
            h ^= static_cast<unsigned char>(c);
            h *= FNV_PRIME;
        }
        tokens.push_back(h);
    }
    return tokens;
}

std::string ModelLoader::detokenize(const std::vector<uint32_t>& tokens) {
#ifdef USE_LLAMA_CPP
    if (impl_->llamaModel) {
        const llama_vocab* vocab = llama_model_get_vocab(impl_->llamaModel);
        
        std::string result;
        char buf[128];
        for (uint32_t token : tokens) {
            int len = llama_token_to_piece(vocab, static_cast<llama_token>(token), 
                                            buf, sizeof(buf), 0, true);
            if (len > 0) {
                result.append(buf, len);
            }
        }
        return result;
    }
#endif
    std::string result;
    for (auto t : tokens) {
        result += "t" + std::to_string(t) + " ";
    }
    return result;
}

uint32_t ModelLoader::tokenCount(const std::string& text) const {
#ifdef USE_LLAMA_CPP
    if (impl_->llamaModel) {
        const llama_vocab* vocab = llama_model_get_vocab(impl_->llamaModel);
        
        std::vector<llama_token> tokens(text.size() + 16);
        int nTokens = llama_tokenize(vocab, text.c_str(), text.size(),
                                      tokens.data(), tokens.size(), false, false);
        if (nTokens < 0) {
            return static_cast<uint32_t>(-nTokens);
        }
        return static_cast<uint32_t>(nTokens);
    }
#endif
    return text.size() / 4 + 1;
}

bool ModelLoader::downloadModel(const std::string& modelName, 
                                 std::function<void(double)> progressCallback) {
    std::string url = modelUrlForName(modelName);
    if (url.empty()) {
        impl_->setError("Unknown model id (no URL mapping): " + modelName);
        return false;
    }

    std::string modelsDir = impl_->config.cacheDir.empty() ? defaultModelsDir() : impl_->config.cacheDir;
    std::filesystem::create_directories(modelsDir);

    std::string filename = modelName;
    if (filename.rfind("http://", 0) == 0 || filename.rfind("https://", 0) == 0) {
        filename = std::filesystem::path(filename).filename().string();
        if (filename.empty()) filename = "model.gguf";
    }

    std::string dest = (std::filesystem::path(modelsDir) / filename).string();
    std::string part = dest + ".part";
    std::string errPath = part + ".err";

    impl_->info.name = filename;
    impl_->info.path = dest;

    if (std::filesystem::exists(dest) && std::filesystem::file_size(dest) > 0) {
        if (progressCallback) progressCallback(1.0);
        return true;
    }

    uint64_t existingPart = 0;
    {
        std::error_code ec;
        if (std::filesystem::exists(part, ec) && !ec) {
            existingPart = static_cast<uint64_t>(std::filesystem::file_size(part, ec));
            if (ec) existingPart = 0;
        }
    }

    impl_->downloading = true;
    impl_->setState(ModelState::DOWNLOADING);

    uint64_t expected = tryFetchContentLengthCurl(url);
    if (expected > 0) {
        impl_->info.sizeBytes = expected;
    }
    if (expected > 0) {
        uint64_t free = getFreeDiskSpace(dest);
        if (free > 0 && free < expected + (64ULL * 1024 * 1024)) {
            impl_->downloading = false;
            impl_->setError("Not enough disk space for download: need " + formatBytes(expected) +
                            ", available " + formatBytes(free));
            return false;
        }
    }

    std::atomic<bool> done{false};
    std::thread watcher([&]() {
        while (!done.load()) {
            if (progressCallback) {
                uint64_t cur = 0;
                std::error_code ec;
                if (std::filesystem::exists(part, ec) && !ec) {
                    cur = std::filesystem::file_size(part, ec);
                    if (ec) cur = 0;
                }
                if (expected > 0) {
                    double p = static_cast<double>(cur) / static_cast<double>(expected);
                    if (p > 0.99) p = 0.99;
                    if (p < 0.0) p = 0.0;
                    progressCallback(p);
                } else {
                    progressCallback(cur > 0 ? 0.5 : 0.0);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });

    utils::Logger::info("Downloading model: " + filename);
    utils::SleepInhibitor inhibitor;
    inhibitor.acquire("SynapseNet model download");
    const int maxAttempts = 50;
    int attempt = 0;
    int lastExit = 0;
    std::string lastErr;
    for (; attempt < maxAttempts; ++attempt) {
#ifdef _WIN32
        std::string cmd = "powershell -NoProfile -Command \""
                          "$ProgressPreference='SilentlyContinue';"
                          "Invoke-WebRequest -Uri " + url + " -OutFile " + part + "\"";
#else
        std::string cmd = "curl -sS -L --fail --retry 5 --retry-delay 2 --retry-all-errors --connect-timeout 20 ";
        if (existingPart > 0) {
            cmd += "-C - ";
        }
        cmd += "--speed-time 60 --speed-limit 1024 ";
        cmd += "-o " + shellEscape(part) + " " + shellEscape(url);
        cmd += " >/dev/null 2>" + shellEscape(errPath);
#endif

        uint64_t before = 0;
        {
            std::error_code ec;
            if (std::filesystem::exists(part, ec) && !ec) {
                before = static_cast<uint64_t>(std::filesystem::file_size(part, ec));
                if (ec) before = 0;
            }
        }

        int rc = std::system(cmd.c_str());
        lastExit = decodeSystemExitCode(rc);

        uint64_t after = before;
        {
            std::error_code ec;
            if (std::filesystem::exists(part, ec) && !ec) {
                after = static_cast<uint64_t>(std::filesystem::file_size(part, ec));
                if (ec) after = before;
            }
        }

        if (expected > 0 && after >= expected) {
            lastExit = 0;
            lastErr.clear();
            break;
        }

        if (lastExit == 0 && expected > 0 && after < expected) {
            lastExit = 1;
            lastErr = "Download incomplete: " + formatBytes(after) + " / " + formatBytes(expected);
        } else if (lastExit != 0) {
            lastErr = trimWS(readSmallFile(errPath, 4096));
            if (lastErr.empty()) lastErr = "curl exit " + std::to_string(lastExit);
        } else {
            lastErr.clear();
        }

        if (expected > 0 && after >= expected) break;
        if (attempt + 1 >= maxAttempts) break;

        utils::Logger::warn("Download retry (" + std::to_string(attempt + 1) + "/" + std::to_string(maxAttempts) +
                            ") exit=" + std::to_string(lastExit) + " bytes=" + formatBytes(after) +
                            (lastErr.empty() ? "" : " err=" + lastErr));
        existingPart = after;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    done = true;
    if (watcher.joinable()) watcher.join();

    if (lastExit != 0) {
        impl_->downloading = false;
        std::string msg = "Download failed";
        if (!lastErr.empty()) msg += ": " + lastErr;
        msg += " (exit " + std::to_string(lastExit) + ")";
        impl_->setError(msg);
        std::error_code ec;
        uint64_t sz = 0;
        if (std::filesystem::exists(part, ec) && !ec) {
            sz = static_cast<uint64_t>(std::filesystem::file_size(part, ec));
            if (ec) sz = 0;
        }
        if (sz == 0) {
            std::filesystem::remove(part, ec);
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(part, dest, ec);
    if (ec) {
        impl_->downloading = false;
        impl_->setError("Download saved but rename failed: " + ec.message());
        return false;
    }

    impl_->downloading = false;
    impl_->setState(ModelState::UNLOADED);
    if (progressCallback) progressCallback(1.0);
    return true;
}

bool ModelLoader::cancelDownload() {
    impl_->downloading = false;
    return true;
}

std::vector<std::string> ModelLoader::listAvailable() const {
    return {
        "llama-2-7b-chat.Q2_K.gguf",
        "llama-2-7b-chat.Q4_K_M.gguf",
        "tinyllama-1.1b-chat-v1.0.Q2_K.gguf",
        "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
        "deepseek-coder-6.7b-instruct.Q4_K_M.gguf"
    };
}

std::vector<std::string> ModelLoader::listDownloaded() const {
    std::vector<std::string> models;
    std::string modelDir = std::getenv("HOME") ? 
                           std::string(std::getenv("HOME")) + "/.synapsenet/models" : 
                           "./models";
    
    if (std::filesystem::exists(modelDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(modelDir)) {
            if (entry.path().extension() == ".gguf") {
                models.push_back(entry.path().filename().string());
            }
        }
    }
    return models;
}

std::vector<ModelInfo> ModelLoader::listModels(const std::string& directory) const {
    std::vector<ModelInfo> models;
    if (!std::filesystem::exists(directory)) return models;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".gguf") {
            std::string name = entry.path().filename().string();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.rfind("ggml-vocab-", 0) == 0) continue;
            std::error_code ec;
            auto sz = std::filesystem::file_size(entry.path(), ec);
            if (ec) continue;
            if (sz < 50ULL * 1024ULL * 1024ULL) continue;

            ModelInfo info;
            info.name = name;
            info.path = entry.path().string();
            info.sizeBytes = static_cast<uint64_t>(sz);
            info.format = ModelFormat::GGUF;
            models.push_back(info);
        }
    }
    return models;
}

bool ModelLoader::deleteModel(const std::string& path) {
    return std::filesystem::remove(path);
}

void ModelLoader::setConfig(const LoaderConfig& config) {
    impl_->config = config;
}

LoaderConfig ModelLoader::getConfig() const {
    return impl_->config;
}

void ModelLoader::setContextSize(uint32_t size) {
    impl_->contextSize = size;
}

void ModelLoader::setThreads(uint32_t threads) {
    impl_->threads = threads;
}

void ModelLoader::setGpuLayers(uint32_t layers) {
    impl_->config.gpuLayers = layers;
}

void ModelLoader::setBatchSize(uint32_t size) {
    impl_->config.batchSize = size;
}

void ModelLoader::onStateChange(std::function<void(ModelState)> callback) {
    impl_->stateCallback = callback;
}

void ModelLoader::onProgress(std::function<void(double)> callback) {
    impl_->progressCallback = callback;
}

void ModelLoader::onError(std::function<void(const std::string&)> callback) {
    impl_->errorCallback = callback;
}

bool ModelLoader::validateModel(const std::string& path) {
    if (!std::filesystem::exists(path)) return false;
    std::ifstream f(path, std::ios::binary);
    char magic[4];
    f.read(magic, 4);
    return magic[0] == 'G' && magic[1] == 'G' && magic[2] == 'U' && magic[3] == 'F';
}

ModelFormat ModelLoader::detectFormat(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ".gguf") return ModelFormat::GGUF;
    if (ext == ".ggml" || ext == ".bin") return ModelFormat::GGML;
    if (ext == ".safetensors") return ModelFormat::SAFETENSORS;
    if (ext == ".pt" || ext == ".pth") return ModelFormat::PYTORCH;
    if (ext == ".onnx") return ModelFormat::ONNX;
    return ModelFormat::UNKNOWN;
}

uint64_t ModelLoader::estimateMemory(const std::string& path, uint32_t contextSize) {
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    
    uint64_t fileSize = std::filesystem::file_size(path);
    // Estimate: file + context buffers + KV cache + other overhead
    // For 7B model: ~14GB (quantized), context 2048: ~2GB overhead
    uint64_t contextBuffers = static_cast<uint64_t>(contextSize) * 4096 * sizeof(float);
    uint64_t kvCache = static_cast<uint64_t>(contextSize) * 128 * sizeof(float) * 2;
    uint64_t overhead = contextBuffers / 2;
    
    uint64_t totalEstimate = fileSize + contextBuffers + kvCache + overhead;
    return totalEstimate;
}

std::string ModelLoader::formatBytes(uint64_t bytes) {
    return ::synapse::model::formatBytes(bytes);
}

uint64_t ModelLoader::getFreeDiskSpace(const std::string& path) {
    return ::synapse::model::getFreeDiskSpace(path);
}

// Task 3: Model Persistence
bool ModelLoader::saveLastModel() const {
    if (impl_->state != ModelState::READY) {
        return false;
    }
    utils::Config::instance().setLastModelPath(impl_->info.path);
    utils::Config::instance().setLastModelId(impl_->info.name);
    utils::Config::instance().setLastModelFormat(std::string(impl_->info.format == ModelFormat::GGUF ? "GGUF" : 
                                                               impl_->info.format == ModelFormat::GGML ? "GGML" : "UNKNOWN"));
    utils::Config::instance().save(utils::Config::instance().getConfigPath());
    utils::Logger::info("Saved last loaded model: " + impl_->info.name);
    return true;
}

bool ModelLoader::loadLastModel() {
    std::string lastPath = utils::Config::instance().getLastModelPath();
    if (lastPath.empty()) {
        utils::Logger::info("No last model to load");
        return false;
    }
    
    if (!std::filesystem::exists(lastPath)) {
        utils::Logger::warn("Last model path does not exist: " + lastPath);
        utils::Config::instance().setLastModelPath("");
        return false;
    }
    
    utils::Logger::info("Auto-loading last model: " + lastPath);
    bool result = load(lastPath);
    if (result) {
        saveLastModel();
    }
    return result;
}

std::vector<std::string> ModelLoader::getAvailableModels() const {
    std::vector<std::string> allModels;
    const auto& paths = getScanPaths();
    
    for (const auto& dirPath : paths) {
        std::string expandedPath = dirPath;
        if (expandedPath[0] == '~') {
            const char* home = std::getenv("HOME");
            if (home) {
                expandedPath = std::string(home) + dirPath.substr(1);
            }
        }
        
        if (std::filesystem::exists(expandedPath)) {
            auto models = listModels(expandedPath);
            for (const auto& model : models) {
                allModels.push_back(model.path);
            }
        }
    }
    
    // Remove duplicates
    std::sort(allModels.begin(), allModels.end());
    allModels.erase(std::unique(allModels.begin(), allModels.end()), allModels.end());
    return allModels;
}

// Task 4: Configurable Scan Paths
void ModelLoader::setScanPaths(const std::vector<std::string>& paths) {
    scanPaths_ = paths;
    utils::Config::instance().setModelScanPaths(paths);
    utils::Logger::info("Model scan paths updated: " + std::to_string(paths.size()) + " paths");
}

std::vector<std::string> ModelLoader::getScanPaths() const {
    if (!scanPaths_.empty()) {
        return scanPaths_;
    }
    return utils::Config::instance().getModelScanPaths();
}

}
}
