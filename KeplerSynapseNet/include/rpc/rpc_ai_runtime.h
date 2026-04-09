#pragma once

#include "core/tor_route_policy.h"
#include "crypto/keys.h"
#include "infrastructure/messages.h"
#include "model/model_loader.h"
#include "network/network.h"
#include "web/web.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace synapse {

struct RemoteSessionInfo {
    std::string peerId;
    std::string sessionId;
    std::string providerAddress;
    uint64_t pricePerRequestAtoms = 0;
    uint64_t expiresAt = 0;
};

struct RemotePending {
    bool done = false;
    std::string text;
    uint32_t tokensUsed = 0;
    uint64_t latencyMs = 0;
};

namespace rpc {

model::GenerationParams buildRpcAiGenerationParams(const nlohmann::json& params);

struct RpcAiCompletionInputs {
    model::ModelLoader* modelLoader = nullptr;
    std::mutex* modelMtx = nullptr;
    std::atomic<uint64_t>* modelRequests = nullptr;
    std::atomic<bool>* agentTorRequired = nullptr;
    std::atomic<bool>* agentTorReachable = nullptr;
    std::function<bool()> getDefaultWebInject;
    std::function<bool()> getDefaultWebOnion;
    std::function<bool()> getDefaultWebTor;
    std::function<core::TorRoutePolicyDecision(bool)> refreshTorRoutePolicy;
    std::function<bool()> ensureWebSubsystem;
    std::mutex* webMtx = nullptr;
    std::function<web::AIWrapper*()> getWebAi;
    std::function<web::WebSearch*()> getWebSearch;
    std::function<web::QueryDetector*()> getWebDetector;
    std::function<void()> onFailClosedSkip;
    std::function<std::string(
        const nlohmann::json&,
        const std::string&,
        const model::GenerationParams&)> runRemoteCompletion;
};

std::string runRpcAiComplete(
    const std::string& paramsJson,
    const RpcAiCompletionInputs& inputs);

struct RpcAiRemoteCompletionInputs {
    const nlohmann::json* params = nullptr;
    const std::string* prompt = nullptr;
    const model::GenerationParams* generationParams = nullptr;
    bool devMode = false;
    network::Network* network = nullptr;
    crypto::Keys* keys = nullptr;
    std::mutex* remoteMtx = nullptr;
    std::condition_variable* remoteCv = nullptr;
    std::unordered_map<std::string, RemoteSessionInfo>* remoteSessions = nullptr;
    std::unordered_map<std::string, RemotePending>* remotePending = nullptr;
    std::function<core::TorRoutePolicyDecision(bool)> refreshTorRoutePolicy;
    std::function<std::string(const std::string&, uint64_t, uint64_t&)> createAndSubmitPaymentTx;
    std::function<std::string()> nextRequestId;
};

std::string runRpcAiCompleteRemote(const RpcAiRemoteCompletionInputs& inputs);

struct RpcAiLocalWebContextInputs {
    const std::string* prompt = nullptr;
    const std::string* webQuery = nullptr;
    bool webInject = false;
    bool torRequired = false;
    bool webTor = false;
    bool webOnion = false;
    web::AIWrapper* webAi = nullptr;
    web::WebSearch* webSearch = nullptr;
    web::QueryDetector* webDetector = nullptr;
    std::function<void()> onFailClosedSkip;
};

struct RpcAiLocalWebContextResult {
    std::string finalPrompt;
    uint64_t webResults = 0;
    uint64_t webClearnet = 0;
    uint64_t webDarknet = 0;
};

RpcAiLocalWebContextResult prepareRpcAiLocalWebContext(
    const RpcAiLocalWebContextInputs& inputs);

std::string buildRpcAiLocalResponse(
    const std::string& modelName,
    const std::string& text,
    bool webInject,
    bool torDegraded,
    const std::string& webQuery,
    const RpcAiLocalWebContextResult& webContext);

} // namespace rpc
} // namespace synapse
