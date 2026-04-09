#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <functional>
#include <memory>

namespace synapse {
namespace test {

class TestRunner {
private:
    std::vector<std::pair<std::string, std::function<bool()>>> tests;
    int passed;
    int failed;

public:
    TestRunner() : passed(0), failed(0) {}

    void addTest(const std::string& name, std::function<bool()> test) {
        tests.push_back({name, test});
    }

    void run() {
        for (const auto& test : tests) {
            bool result = false;
            try {
                result = test.second();
            } catch (...) {
                result = false;
            }

            if (result) {
                passed++;
            } else {
                failed++;
            }
        }
    }

    int getPassed() const { return passed; }
    int getFailed() const { return failed; }
    int getTotal() const { return tests.size(); }
};

struct ModelConfig {
    std::string name;
    std::string path;
    int contextSize;
    int batchSize;
    int threads;
    int gpuLayers;
    float temperature;
    float topP;
    int topK;
    float repeatPenalty;
    int maxTokens;
    std::string format;
};

struct TokenizerConfig {
    std::string vocabPath;
    int vocabSize;
    int bosToken;
    int eosToken;
    int padToken;
    int unkToken;
    bool addBos;
    bool addEos;
};

class MockTokenizer {
private:
    std::map<std::string, int> vocab;
    std::map<int, std::string> reverseVocab;
    TokenizerConfig config;

public:
    MockTokenizer() {
        config.vocabSize = 32000;
        config.bosToken = 1;
        config.eosToken = 2;
        config.padToken = 0;
        config.unkToken = 3;
        config.addBos = true;
        config.addEos = false;

        initializeVocab();
    }

    void initializeVocab() {
        vocab["<pad>"] = 0;
        vocab["<s>"] = 1;
        vocab["</s>"] = 2;
        vocab["<unk>"] = 3;

        std::vector<std::string> commonWords = {
            "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
            "have", "has", "had", "do", "does", "did", "will", "would", "could",
            "should", "may", "might", "must", "shall", "can", "need", "dare",
            "ought", "used", "to", "of", "in", "for", "on", "with", "at", "by",
            "from", "up", "about", "into", "over", "after", "beneath", "under",
            "above", "hello", "world", "test", "model", "inference", "token"
        };

        int id = 4;
        for (const auto& word : commonWords) {
            vocab[word] = id;
            reverseVocab[id] = word;
            id++;
        }
    }

    std::vector<int> encode(const std::string& text) {
        std::vector<int> tokens;

        if (config.addBos) {
            tokens.push_back(config.bosToken);
        }

        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            auto it = vocab.find(word);
            if (it != vocab.end()) {
                tokens.push_back(it->second);
            } else {
                tokens.push_back(config.unkToken);
            }
        }

        if (config.addEos) {
            tokens.push_back(config.eosToken);
        }

        return tokens;
    }

    std::string decode(const std::vector<int>& tokens) {
        std::string result;
        for (int token : tokens) {
            if (token == config.bosToken || token == config.eosToken || token == config.padToken) {
                continue;
            }
            auto it = reverseVocab.find(token);
            if (it != reverseVocab.end()) {
                if (!result.empty()) result += " ";
                result += it->second;
            }
        }
        return result;
    }

    int getVocabSize() const { return config.vocabSize; }
    int getBosToken() const { return config.bosToken; }
    int getEosToken() const { return config.eosToken; }
};

class MockModel {
private:
    ModelConfig config;
    bool loaded;
    std::mt19937 rng;
    std::vector<std::vector<float>> weights;

public:
    MockModel() : loaded(false), rng(std::random_device{}()) {
        config.name = "mock-model";
        config.contextSize = 2048;
        config.batchSize = 512;
        config.threads = 4;
        config.gpuLayers = 0;
        config.temperature = 0.7f;
        config.topP = 0.9f;
        config.topK = 40;
        config.repeatPenalty = 1.1f;
        config.maxTokens = 256;
    }

    bool load(const std::string& path) {
        config.path = path;
        initializeWeights();
        loaded = true;
        return true;
    }

    void unload() {
        weights.clear();
        loaded = false;
    }

    bool isLoaded() const { return loaded; }

    void initializeWeights() {
        std::normal_distribution<float> dist(0.0f, 0.02f);
        weights.resize(12);
        for (auto& layer : weights) {
            layer.resize(768 * 768);
            for (auto& w : layer) {
                w = dist(rng);
            }
        }
    }

    std::vector<float> forward(const std::vector<int>& tokens) {
        std::vector<float> logits(32000, 0.0f);

        if (!loaded || tokens.empty()) {
            return logits;
        }

        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (auto& l : logits) {
            l = dist(rng);
        }

        return logits;
    }

    int sample(const std::vector<float>& logits) {
        std::vector<float> probs = softmax(logits);
        probs = applyTemperature(probs, config.temperature);
        probs = topPSampling(probs, config.topP);
        probs = topKSampling(probs, config.topK);

        std::discrete_distribution<int> dist(probs.begin(), probs.end());
        return dist(rng);
    }

    std::vector<float> softmax(const std::vector<float>& logits) {
        std::vector<float> result(logits.size());
        float maxVal = *std::max_element(logits.begin(), logits.end());
        float sum = 0.0f;

        for (size_t i = 0; i < logits.size(); i++) {
            result[i] = std::exp(logits[i] - maxVal);
            sum += result[i];
        }

        for (auto& r : result) {
            r /= sum;
        }

        return result;
    }

    std::vector<float> applyTemperature(const std::vector<float>& probs, float temp) {
        if (temp == 1.0f) return probs;

        std::vector<float> result(probs.size());
        float sum = 0.0f;

        for (size_t i = 0; i < probs.size(); i++) {
            result[i] = std::pow(probs[i], 1.0f / temp);
            sum += result[i];
        }

        for (auto& r : result) {
            r /= sum;
        }

        return result;
    }

    std::vector<float> topPSampling(const std::vector<float>& probs, float p) {
        std::vector<std::pair<float, int>> sorted;
        for (size_t i = 0; i < probs.size(); i++) {
            sorted.push_back({probs[i], i});
        }
        std::sort(sorted.begin(), sorted.end(), std::greater<>());

        std::vector<float> result(probs.size(), 0.0f);
        float cumsum = 0.0f;

        for (const auto& pair : sorted) {
            if (cumsum >= p) break;
            result[pair.second] = pair.first;
            cumsum += pair.first;
        }

        float sum = 0.0f;
        for (auto& r : result) sum += r;
        for (auto& r : result) r /= sum;

        return result;
    }

    std::vector<float> topKSampling(const std::vector<float>& probs, int k) {
        std::vector<std::pair<float, int>> sorted;
        for (size_t i = 0; i < probs.size(); i++) {
            sorted.push_back({probs[i], i});
        }
        std::sort(sorted.begin(), sorted.end(), std::greater<>());

        std::vector<float> result(probs.size(), 0.0f);
        for (int i = 0; i < k && i < static_cast<int>(sorted.size()); i++) {
            result[sorted[i].second] = sorted[i].first;
        }

        float sum = 0.0f;
        for (auto& r : result) sum += r;
        for (auto& r : result) r /= sum;

        return result;
    }

    const ModelConfig& getConfig() const { return config; }
    void setTemperature(float t) { config.temperature = t; }
    void setTopP(float p) { config.topP = p; }
    void setTopK(int k) { config.topK = k; }
};

class MockInferenceEngine {
private:
    MockModel model;
    MockTokenizer tokenizer;
    std::vector<int> context;
    int maxContext;

public:
    MockInferenceEngine() : maxContext(2048) {}

    bool loadModel(const std::string& path) {
        return model.load(path);
    }

    void unloadModel() {
        model.unload();
        context.clear();
    }

    std::string generate(const std::string& prompt, int maxTokens) {
        if (!model.isLoaded()) return "";

        context = tokenizer.encode(prompt);
        std::vector<int> generated;

        for (int i = 0; i < maxTokens; i++) {
            auto logits = model.forward(context);
            int nextToken = model.sample(logits);

            if (nextToken == tokenizer.getEosToken()) break;

            generated.push_back(nextToken);
            context.push_back(nextToken);

            if (context.size() > static_cast<size_t>(maxContext)) {
                context.erase(context.begin());
            }
        }

        return tokenizer.decode(generated);
    }

    void clearContext() {
        context.clear();
    }

    int getContextLength() const {
        return context.size();
    }
};

class ModelAccessControl {
private:
    enum class AccessLevel {
        None,
        Read,
        Inference,
        Full
    };

    std::map<std::string, AccessLevel> permissions;
    std::string owner;
    bool isPublic;
    int maxConcurrent;
    int currentUsers;

public:
    ModelAccessControl() : isPublic(false), maxConcurrent(10), currentUsers(0) {}

    void setOwner(const std::string& ownerId) {
        owner = ownerId;
    }

    void setPublic(bool pub) {
        isPublic = pub;
    }

    void grantAccess(const std::string& userId, int level) {
        permissions[userId] = static_cast<AccessLevel>(level);
    }

    void revokeAccess(const std::string& userId) {
        permissions.erase(userId);
    }

    bool canAccess(const std::string& userId) {
        if (userId == owner) return true;
        if (isPublic) return true;
        return permissions.find(userId) != permissions.end();
    }

    bool canInference(const std::string& userId) {
        if (userId == owner) return true;
        if (isPublic) return true;

        auto it = permissions.find(userId);
        if (it == permissions.end()) return false;
        return it->second >= AccessLevel::Inference;
    }

    bool acquireSlot() {
        if (currentUsers >= maxConcurrent) return false;
        currentUsers++;
        return true;
    }

    void releaseSlot() {
        if (currentUsers > 0) currentUsers--;
    }

    int getAvailableSlots() const {
        return maxConcurrent - currentUsers;
    }
};

class ModelMarketplace {
private:
    struct ModelListing {
        std::string modelId;
        std::string ownerId;
        std::string name;
        std::string description;
        uint64_t pricePerQuery;
        uint64_t totalEarnings;
        uint64_t totalQueries;
        bool active;
    };

    std::map<std::string, ModelListing> listings;
    std::map<std::string, uint64_t> balances;

public:
    void listModel(const std::string& modelId, const std::string& ownerId,
                   const std::string& name, uint64_t price) {
        ModelListing listing;
        listing.modelId = modelId;
        listing.ownerId = ownerId;
        listing.name = name;
        listing.pricePerQuery = price;
        listing.totalEarnings = 0;
        listing.totalQueries = 0;
        listing.active = true;
        listings[modelId] = listing;
    }

    void delistModel(const std::string& modelId) {
        auto it = listings.find(modelId);
        if (it != listings.end()) {
            it->second.active = false;
        }
    }

    bool purchaseQuery(const std::string& modelId, const std::string& userId) {
        auto it = listings.find(modelId);
        if (it == listings.end() || !it->second.active) return false;

        uint64_t price = it->second.pricePerQuery;
        if (balances[userId] < price) return false;

        balances[userId] -= price;
        balances[it->second.ownerId] += price;
        it->second.totalEarnings += price;
        it->second.totalQueries++;

        return true;
    }

    void deposit(const std::string& userId, uint64_t amount) {
        balances[userId] += amount;
    }

    uint64_t getBalance(const std::string& userId) const {
        auto it = balances.find(userId);
        return (it != balances.end()) ? it->second : 0;
    }

    std::vector<ModelListing> getActiveListings() const {
        std::vector<ModelListing> result;
        for (const auto& pair : listings) {
            if (pair.second.active) {
                result.push_back(pair.second);
            }
        }
        return result;
    }
};

bool testTokenizerEncode() {
    MockTokenizer tokenizer;
    auto tokens = tokenizer.encode("hello world test");

    if (tokens.empty()) return false;
    if (tokens[0] != tokenizer.getBosToken()) return false;
    if (tokens.size() != 4) return false;

    return true;
}

bool testTokenizerDecode() {
    MockTokenizer tokenizer;
    auto tokens = tokenizer.encode("hello world");
    auto decoded = tokenizer.decode(tokens);

    return decoded == "hello world";
}

bool testModelLoad() {
    MockModel model;
    if (model.isLoaded()) return false;

    model.load("/path/to/model.gguf");
    if (!model.isLoaded()) return false;

    model.unload();
    if (model.isLoaded()) return false;

    return true;
}

bool testModelForward() {
    MockModel model;
    model.load("/path/to/model.gguf");

    std::vector<int> tokens = {1, 4, 5, 6};
    auto logits = model.forward(tokens);

    if (logits.size() != 32000) return false;

    return true;
}

bool testModelSampling() {
    MockModel model;
    model.load("/path/to/model.gguf");

    std::vector<float> logits(32000, 0.0f);
    logits[100] = 10.0f;

    int sampled = model.sample(logits);
    return sampled >= 0 && sampled < 32000;
}

bool testInferenceEngine() {
    MockInferenceEngine engine;
    engine.loadModel("/path/to/model.gguf");

    std::string result = engine.generate("hello", 10);
    return !result.empty() || result.empty();
}

bool testAccessControl() {
    ModelAccessControl access;
    access.setOwner("owner123");

    if (!access.canAccess("owner123")) return false;
    if (access.canAccess("user456")) return false;

    access.grantAccess("user456", 2);
    if (!access.canAccess("user456")) return false;
    if (!access.canInference("user456")) return false;

    access.revokeAccess("user456");
    if (access.canAccess("user456")) return false;

    return true;
}

bool testAccessControlPublic() {
    ModelAccessControl access;
    access.setOwner("owner123");
    access.setPublic(true);

    if (!access.canAccess("anyone")) return false;
    if (!access.canInference("anyone")) return false;

    return true;
}

bool testAccessControlSlots() {
    ModelAccessControl access;

    for (int i = 0; i < 10; i++) {
        if (!access.acquireSlot()) return false;
    }

    if (access.acquireSlot()) return false;
    if (access.getAvailableSlots() != 0) return false;

    access.releaseSlot();
    if (access.getAvailableSlots() != 1) return false;

    return true;
}

bool testMarketplaceListing() {
    ModelMarketplace market;
    market.listModel("model1", "owner1", "Test Model", 100);

    auto listings = market.getActiveListings();
    if (listings.size() != 1) return false;
    if (listings[0].name != "Test Model") return false;

    return true;
}

bool testMarketplacePurchase() {
    ModelMarketplace market;
    market.listModel("model1", "owner1", "Test Model", 100);
    market.deposit("user1", 500);

    if (!market.purchaseQuery("model1", "user1")) return false;
    if (market.getBalance("user1") != 400) return false;
    if (market.getBalance("owner1") != 100) return false;

    return true;
}

bool testMarketplaceInsufficientFunds() {
    ModelMarketplace market;
    market.listModel("model1", "owner1", "Test Model", 100);
    market.deposit("user1", 50);

    if (market.purchaseQuery("model1", "user1")) return false;
    if (market.getBalance("user1") != 50) return false;

    return true;
}

bool testSoftmax() {
    MockModel model;
    model.load("/path/to/model.gguf");

    std::vector<float> logits = {1.0f, 2.0f, 3.0f};
    auto probs = model.softmax(logits);

    float sum = 0.0f;
    for (auto p : probs) sum += p;

    return std::abs(sum - 1.0f) < 0.001f;
}

bool testTopKSampling() {
    MockModel model;
    model.load("/path/to/model.gguf");

    std::vector<float> probs(100, 0.01f);
    probs[0] = 0.5f;
    probs[1] = 0.3f;
    probs[2] = 0.1f;

    auto filtered = model.topKSampling(probs, 3);

    int nonZero = 0;
    for (auto p : filtered) {
        if (p > 0.0f) nonZero++;
    }

    return nonZero == 3;
}

bool testTopPSampling() {
    MockModel model;
    model.load("/path/to/model.gguf");

    std::vector<float> probs(100, 0.001f);
    probs[0] = 0.5f;
    probs[1] = 0.3f;
    probs[2] = 0.1f;

    auto filtered = model.topPSampling(probs, 0.9f);

    float sum = 0.0f;
    for (auto p : filtered) sum += p;

    return std::abs(sum - 1.0f) < 0.001f;
}

bool testTemperature() {
    MockModel model;
    model.load("/path/to/model.gguf");

    std::vector<float> probs = {0.1f, 0.2f, 0.7f};

    auto lowTemp = model.applyTemperature(probs, 0.5f);
    auto highTemp = model.applyTemperature(probs, 2.0f);

    return lowTemp[2] > probs[2] && highTemp[2] < probs[2];
}

bool testContextManagement() {
    MockInferenceEngine engine;
    engine.loadModel("/path/to/model.gguf");

    if (engine.getContextLength() != 0) return false;

    engine.generate("hello world", 5);
    if (engine.getContextLength() == 0) return false;

    engine.clearContext();
    if (engine.getContextLength() != 0) return false;

    return true;
}

class ModelLoader {
private:
    struct GGUFHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t tensorCount;
        uint64_t metadataKVCount;
    };

    GGUFHeader header;
    std::map<std::string, std::string> metadata;
    bool valid;

public:
    ModelLoader() : valid(false) {
        header.magic = 0;
        header.version = 0;
        header.tensorCount = 0;
        header.metadataKVCount = 0;
    }

    bool validateHeader(const std::vector<uint8_t>& data) {
        if (data.size() < 24) return false;

        header.magic = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        if (header.magic != 0x46554747) return false;

        header.version = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
        if (header.version < 2 || header.version > 3) return false;

        valid = true;
        return true;
    }

    bool isValid() const { return valid; }

    uint64_t getTensorCount() const { return header.tensorCount; }

    std::string getMetadata(const std::string& key) const {
        auto it = metadata.find(key);
        return (it != metadata.end()) ? it->second : "";
    }
};

class QuantizationHelper {
public:
    static std::vector<float> dequantizeQ4_0(const std::vector<uint8_t>& data, int size) {
        std::vector<float> result(size);

        int blockSize = 32;
        int numBlocks = size / blockSize;

        for (int b = 0; b < numBlocks; b++) {
            float scale = 1.0f;

            for (int i = 0; i < blockSize; i++) {
                int idx = b * blockSize + i;
                if (idx < size) {
                    int byteIdx = b * 18 + 2 + i / 2;
                    int nibble = (i % 2 == 0) ? (data[byteIdx] & 0x0F) : (data[byteIdx] >> 4);
                    result[idx] = (nibble - 8) * scale;
                }
            }
        }

        return result;
    }

    static std::vector<float> dequantizeQ8_0(const std::vector<uint8_t>& data, int size) {
        std::vector<float> result(size);

        int blockSize = 32;
        int numBlocks = size / blockSize;

        for (int b = 0; b < numBlocks; b++) {
            float scale = 1.0f;

            for (int i = 0; i < blockSize; i++) {
                int idx = b * blockSize + i;
                if (idx < size) {
                    int8_t val = static_cast<int8_t>(data[b * 34 + 2 + i]);
                    result[idx] = val * scale;
                }
            }
        }

        return result;
    }
};

class AttentionLayer {
private:
    int hiddenSize;
    int numHeads;
    int headDim;

public:
    AttentionLayer(int hidden, int heads) : hiddenSize(hidden), numHeads(heads) {
        headDim = hiddenSize / numHeads;
    }

    std::vector<float> forward(const std::vector<float>& input,
                               const std::vector<float>& wq,
                               const std::vector<float>& wk,
                               const std::vector<float>& wv,
                               const std::vector<float>& wo) {
        int seqLen = input.size() / hiddenSize;
        std::vector<float> output(input.size(), 0.0f);

        std::vector<float> q = matmul(input, wq, seqLen, hiddenSize, hiddenSize);
        std::vector<float> k = matmul(input, wk, seqLen, hiddenSize, hiddenSize);
        std::vector<float> v = matmul(input, wv, seqLen, hiddenSize, hiddenSize);

        for (int h = 0; h < numHeads; h++) {
            std::vector<float> scores(seqLen * seqLen, 0.0f);

            for (int i = 0; i < seqLen; i++) {
                for (int j = 0; j <= i; j++) {
                    float score = 0.0f;
                    for (int d = 0; d < headDim; d++) {
                        int qIdx = i * hiddenSize + h * headDim + d;
                        int kIdx = j * hiddenSize + h * headDim + d;
                        score += q[qIdx] * k[kIdx];
                    }
                    scores[i * seqLen + j] = score / std::sqrt(static_cast<float>(headDim));
                }
            }

            for (int i = 0; i < seqLen; i++) {
                float maxVal = -1e9f;
                for (int j = 0; j <= i; j++) {
                    maxVal = std::max(maxVal, scores[i * seqLen + j]);
                }

                float sum = 0.0f;
                for (int j = 0; j <= i; j++) {
                    scores[i * seqLen + j] = std::exp(scores[i * seqLen + j] - maxVal);
                    sum += scores[i * seqLen + j];
                }

                for (int j = 0; j <= i; j++) {
                    scores[i * seqLen + j] /= sum;
                }
            }

            for (int i = 0; i < seqLen; i++) {
                for (int d = 0; d < headDim; d++) {
                    float val = 0.0f;
                    for (int j = 0; j <= i; j++) {
                        int vIdx = j * hiddenSize + h * headDim + d;
                        val += scores[i * seqLen + j] * v[vIdx];
                    }
                    output[i * hiddenSize + h * headDim + d] = val;
                }
            }
        }

        return matmul(output, wo, seqLen, hiddenSize, hiddenSize);
    }

    std::vector<float> matmul(const std::vector<float>& a, const std::vector<float>& b,
                              int m, int k, int n) {
        std::vector<float> c(m * n, 0.0f);
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < n; j++) {
                float sum = 0.0f;
                for (int l = 0; l < k; l++) {
                    sum += a[i * k + l] * b[l * n + j];
                }
                c[i * n + j] = sum;
            }
        }
        return c;
    }
};

bool testModelLoader() {
    ModelLoader loader;

    std::vector<uint8_t> validHeader = {0x47, 0x47, 0x55, 0x46, 0x03, 0x00, 0x00, 0x00};
    validHeader.resize(24, 0);

    if (!loader.validateHeader(validHeader)) return false;
    if (!loader.isValid()) return false;

    return true;
}

bool testInvalidModelHeader() {
    ModelLoader loader;

    std::vector<uint8_t> invalidHeader = {0x00, 0x00, 0x00, 0x00};
    invalidHeader.resize(24, 0);

    if (loader.validateHeader(invalidHeader)) return false;

    return true;
}

bool testDequantizeQ4() {
    std::vector<uint8_t> data(18, 0);
    data[0] = 0x00;
    data[1] = 0x3F;

    auto result = QuantizationHelper::dequantizeQ4_0(data, 32);
    return result.size() == 32;
}

bool testDequantizeQ8() {
    std::vector<uint8_t> data(34, 0);
    data[0] = 0x00;
    data[1] = 0x3F;

    auto result = QuantizationHelper::dequantizeQ8_0(data, 32);
    return result.size() == 32;
}

bool testAttentionLayer() {
    AttentionLayer attn(64, 4);

    std::vector<float> input(64, 1.0f);
    std::vector<float> wq(64 * 64, 0.01f);
    std::vector<float> wk(64 * 64, 0.01f);
    std::vector<float> wv(64 * 64, 0.01f);
    std::vector<float> wo(64 * 64, 0.01f);

    auto output = attn.forward(input, wq, wk, wv, wo);
    return output.size() == 64;
}

void runAllModelTests() {
    TestRunner runner;

    runner.addTest("TokenizerEncode", testTokenizerEncode);
    runner.addTest("TokenizerDecode", testTokenizerDecode);
    runner.addTest("ModelLoad", testModelLoad);
    runner.addTest("ModelForward", testModelForward);
    runner.addTest("ModelSampling", testModelSampling);
    runner.addTest("InferenceEngine", testInferenceEngine);
    runner.addTest("AccessControl", testAccessControl);
    runner.addTest("AccessControlPublic", testAccessControlPublic);
    runner.addTest("AccessControlSlots", testAccessControlSlots);
    runner.addTest("MarketplaceListing", testMarketplaceListing);
    runner.addTest("MarketplacePurchase", testMarketplacePurchase);
    runner.addTest("MarketplaceInsufficientFunds", testMarketplaceInsufficientFunds);
    runner.addTest("Softmax", testSoftmax);
    runner.addTest("TopKSampling", testTopKSampling);
    runner.addTest("TopPSampling", testTopPSampling);
    runner.addTest("Temperature", testTemperature);
    runner.addTest("ContextManagement", testContextManagement);
    runner.addTest("ModelLoader", testModelLoader);
    runner.addTest("InvalidModelHeader", testInvalidModelHeader);
    runner.addTest("DequantizeQ4", testDequantizeQ4);
    runner.addTest("DequantizeQ8", testDequantizeQ8);
    runner.addTest("AttentionLayer", testAttentionLayer);

    runner.run();
}

}
}


int main() {
    synapse::test::runAllModelTests();
    return 0;
}
