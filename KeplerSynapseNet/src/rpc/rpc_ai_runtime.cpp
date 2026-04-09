#include "rpc/rpc_ai_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace synapse::rpc {

using json = nlohmann::json;

namespace {

json parseRpcParams(const std::string& paramsJson) {
    if (paramsJson.empty()) {
        return json::object();
    }
    json parsed = json::parse(paramsJson, nullptr, false);
    if (parsed.is_discarded()) {
        throw std::runtime_error("Invalid JSON params");
    }
    if (parsed.is_array()) {
        if (parsed.empty()) {
            return json::object();
        }
        if (!parsed.front().is_object()) {
            throw std::runtime_error("Expected object params");
        }
        return parsed.front();
    }
    if (!parsed.is_object()) {
        throw std::runtime_error("Expected object params");
    }
    return parsed;
}

void validateAiCompletionInputs(const RpcAiCompletionInputs& inputs) {
    if (!inputs.modelLoader || !inputs.modelMtx || !inputs.modelRequests ||
        !inputs.agentTorRequired || !inputs.agentTorReachable ||
        !inputs.refreshTorRoutePolicy || !inputs.runRemoteCompletion) {
        throw std::runtime_error("invalid_rpc_ai_completion_inputs");
    }
}

void validateRemoteCompletionInputs(const RpcAiRemoteCompletionInputs& inputs) {
    if (!inputs.params || !inputs.prompt || !inputs.generationParams || !inputs.network ||
        !inputs.keys || !inputs.remoteMtx || !inputs.remoteCv || !inputs.remoteSessions ||
        !inputs.remotePending || !inputs.refreshTorRoutePolicy ||
        !inputs.createAndSubmitPaymentTx || !inputs.nextRequestId) {
        throw std::runtime_error("invalid_rpc_ai_remote_completion_inputs");
    }
}

network::Message makeRpcMessage(const std::string& command, const std::vector<uint8_t>& payload) {
    network::Message msg;
    msg.command = command;
    msg.payload = payload;
    msg.timestamp = std::time(nullptr);
    return msg;
}

} // namespace

model::GenerationParams buildRpcAiGenerationParams(const json& params) {
    model::GenerationParams gp;
    if (params.contains("maxTokens")) {
        gp.maxTokens = static_cast<uint32_t>(std::max(1, params.value("maxTokens", 512)));
    }
    if (params.contains("temperature")) {
        gp.temperature = static_cast<float>(std::max(0.0, params.value("temperature", 0.7)));
    }
    if (params.contains("topP")) {
        gp.topP = static_cast<float>(std::max(0.0, params.value("topP", 0.9)));
    }
    if (params.contains("topK")) {
        gp.topK = static_cast<uint32_t>(std::max(0, params.value("topK", 40)));
    }
    if (params.contains("seed")) {
        gp.seed = static_cast<uint64_t>(std::max<int64_t>(0, params.value("seed", 0)));
    }
    if (params.contains("stopSequences") && params["stopSequences"].is_array()) {
        gp.stopSequences.clear();
        for (const auto& s : params["stopSequences"]) {
            if (s.is_string()) {
                gp.stopSequences.push_back(s.get<std::string>());
            }
        }
    }
    if (params.contains("jsonMode")) {
        gp.jsonMode = params.value("jsonMode", false);
    }
    return gp;
}

std::string runRpcAiComplete(
    const std::string& paramsJson,
    const RpcAiCompletionInputs& inputs) {
    validateAiCompletionInputs(inputs);

    const json params = parseRpcParams(paramsJson);
    std::string prompt = params.value("prompt", "");
    if (prompt.empty()) {
        throw std::runtime_error("prompt required");
    }
    if (prompt.size() > 1024 * 1024) {
        throw std::runtime_error("prompt too large");
    }

    const bool remote = params.value("remote", false) || params.contains("remoteSessionId");
    bool webInject = params.value(
        "webInject",
        inputs.getDefaultWebInject ? inputs.getDefaultWebInject() : false);
    bool webOnion = params.value(
        "webOnion",
        inputs.getDefaultWebOnion ? inputs.getDefaultWebOnion() : false);
    bool webTor = params.value(
        "webTor",
        inputs.getDefaultWebTor ? inputs.getDefaultWebTor() : false);
    const std::string webQuery = params.value("webQuery", "");

    bool torDegraded = false;
    const bool torRequired = inputs.agentTorRequired->load();
    const bool torReachable = inputs.agentTorReachable->load();
    const auto route = inputs.refreshTorRoutePolicy(true);
    if (torRequired) {
        webInject = true;
    }
    torDegraded = route.torDegraded;
    if (torRequired) {
        webTor = torReachable && route.allowWebClearnet;
        webOnion = torReachable && route.allowWebOnion;
        if (!webTor && !webOnion) {
            webInject = false;
        }
    }

    const model::GenerationParams generationParams = buildRpcAiGenerationParams(params);
    if (remote) {
        return inputs.runRemoteCompletion(params, prompt, generationParams);
    }

    std::lock_guard<std::mutex> modelLock(*inputs.modelMtx);
    if (!inputs.modelLoader->isLoaded()) {
        throw std::runtime_error("model_not_loaded");
    }
    if (inputs.modelLoader->isGenerating()) {
        throw std::runtime_error("model_busy");
    }
    inputs.modelRequests->fetch_add(1);

    RpcAiLocalWebContextResult webContext;
    webContext.finalPrompt = prompt;
    if (webInject && inputs.ensureWebSubsystem && inputs.ensureWebSubsystem()) {
        if (inputs.webMtx) {
            std::lock_guard<std::mutex> webLock(*inputs.webMtx);
            RpcAiLocalWebContextInputs webInputs;
            webInputs.prompt = &prompt;
            webInputs.webQuery = &webQuery;
            webInputs.webInject = webInject;
            webInputs.torRequired = torRequired;
            webInputs.webTor = webTor;
            webInputs.webOnion = webOnion;
            webInputs.webAi = inputs.getWebAi ? inputs.getWebAi() : nullptr;
            webInputs.webSearch = inputs.getWebSearch ? inputs.getWebSearch() : nullptr;
            webInputs.webDetector = inputs.getWebDetector ? inputs.getWebDetector() : nullptr;
            webInputs.onFailClosedSkip = inputs.onFailClosedSkip;
            webContext = prepareRpcAiLocalWebContext(webInputs);
        }
    }

    const std::string text =
        inputs.modelLoader->generate(webContext.finalPrompt, generationParams);
    const auto info = inputs.modelLoader->getInfo();
    return buildRpcAiLocalResponse(
        info.name,
        text,
        webInject,
        torDegraded,
        webQuery,
        webContext);
}

std::string runRpcAiCompleteRemote(const RpcAiRemoteCompletionInputs& inputs) {
    validateRemoteCompletionInputs(inputs);

    if (!inputs.keys->isValid()) {
        throw std::runtime_error("wallet_not_ready");
    }

    const auto route = inputs.refreshTorRoutePolicy(true);
    if (!route.allowP2PDiscovery) {
        throw std::runtime_error("tor_fail_closed_remote_model");
    }

    const std::string sessionId =
        inputs.params->value("remoteSessionId", inputs.params->value("sessionId", ""));
    if (sessionId.empty()) {
        throw std::runtime_error("remoteSessionId required");
    }

    RemoteSessionInfo session;
    {
        std::lock_guard<std::mutex> lock(*inputs.remoteMtx);
        auto it = inputs.remoteSessions->find(sessionId);
        if (it == inputs.remoteSessions->end()) {
            throw std::runtime_error("remote_session_not_found");
        }
        session = it->second;
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    if (session.expiresAt != 0 && session.expiresAt < now) {
        throw std::runtime_error("remote_session_expired");
    }

    uint64_t feeAtoms = 0;
    std::string paymentTxidHex;
    if (session.pricePerRequestAtoms > 0) {
        paymentTxidHex = inputs.createAndSubmitPaymentTx(
            session.providerAddress,
            session.pricePerRequestAtoms,
            feeAtoms);
    }

    RemoteModelInferMessage req;
    req.sessionId = session.sessionId;
    req.requestId = inputs.nextRequestId();
    req.prompt = *inputs.prompt;
    req.maxTokens = inputs.generationParams->maxTokens;
    req.temperature = inputs.generationParams->temperature;
    req.topP = inputs.generationParams->topP;
    req.topK = inputs.generationParams->topK;
    req.paymentTxidHex = paymentTxidHex;
    req.timestamp = now;

    auto pubV = inputs.keys->getPublicKey();
    if (pubV.size() >= req.renterPubKey.size()) {
        std::memcpy(req.renterPubKey.data(), pubV.data(), req.renterPubKey.size());
    }

    {
        std::lock_guard<std::mutex> lock(*inputs.remoteMtx);
        RemotePending pending;
        pending.done = false;
        (*inputs.remotePending)[req.requestId] = std::move(pending);
    }

    inputs.network->send(session.peerId, makeRpcMessage("m_infer", req.serialize()));

    const uint64_t deadline =
        static_cast<uint64_t>(std::time(nullptr)) + (inputs.devMode ? 45 : 120);
    RemotePending done;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(*inputs.remoteMtx);
            auto it = inputs.remotePending->find(req.requestId);
            if (it != inputs.remotePending->end() && it->second.done) {
                done = it->second;
                inputs.remotePending->erase(it);
                break;
            }
        }
        if (static_cast<uint64_t>(std::time(nullptr)) >= deadline) {
            std::lock_guard<std::mutex> lock(*inputs.remoteMtx);
            inputs.remotePending->erase(req.requestId);
            throw std::runtime_error("remote_infer_timeout");
        }
        std::unique_lock<std::mutex> lk(*inputs.remoteMtx);
        inputs.remoteCv->wait_for(lk, std::chrono::milliseconds(250));
    }

    json out;
    out["model"] = "remote";
    out["text"] = done.text;
    json remote;
    remote["peerId"] = session.peerId;
    remote["sessionId"] = session.sessionId;
    remote["providerAddress"] = session.providerAddress;
    remote["pricePerRequestAtoms"] = session.pricePerRequestAtoms;
    remote["paymentTxid"] = paymentTxidHex;
    remote["feeAtoms"] = feeAtoms;
    out["remote"] = remote;
    return out.dump();
}

RpcAiLocalWebContextResult prepareRpcAiLocalWebContext(
    const RpcAiLocalWebContextInputs& inputs) {
    RpcAiLocalWebContextResult out;
    if (inputs.prompt) {
        out.finalPrompt = *inputs.prompt;
    }

    if (!inputs.webInject || !inputs.prompt || !inputs.webAi || !inputs.webSearch) {
        return out;
    }

    web::SearchConfig cfg = inputs.webSearch->getConfig();
    cfg.enableClearnet = inputs.torRequired ? inputs.webTor : true;
    cfg.enableDarknet = inputs.webOnion;
    cfg.routeClearnetThroughTor = inputs.webTor;
    inputs.webSearch->setConfig(cfg);

    if (!cfg.enableClearnet && !cfg.enableDarknet) {
        if (inputs.onFailClosedSkip) {
            inputs.onFailClosedSkip();
        }
        return out;
    }

    try {
        if (inputs.webQuery && !inputs.webQuery->empty() && inputs.webDetector) {
            web::QueryAnalysis analysis = inputs.webDetector->analyze(*inputs.webQuery);
            std::vector<web::SearchResult> results =
                inputs.webSearch->search(*inputs.webQuery, analysis.type);
            out.webResults = static_cast<uint64_t>(results.size());
            for (const auto& result : results) {
                if (result.isOnion) {
                    out.webDarknet += 1;
                } else {
                    out.webClearnet += 1;
                }
            }
            out.finalPrompt = inputs.webAi->injectContext(*inputs.prompt, results);
            return out;
        }

        out.finalPrompt = inputs.webAi->processQuery(*inputs.prompt);
        const auto stats = inputs.webAi->getStats();
        out.webResults = stats.lastResultCount;
        out.webClearnet = stats.lastClearnetResults;
        out.webDarknet = stats.lastDarknetResults;
    } catch (...) {
        out.finalPrompt = *inputs.prompt;
    }

    return out;
}

std::string buildRpcAiLocalResponse(
    const std::string& modelName,
    const std::string& text,
    bool webInject,
    bool torDegraded,
    const std::string& webQuery,
    const RpcAiLocalWebContextResult& webContext) {
    json out;
    out["model"] = modelName;
    out["text"] = text;
    out["webInject"] = webInject;
    out["torDegraded"] = torDegraded;
    if (webInject) {
        json web;
        web["lastResults"] = webContext.webResults;
        web["lastClearnetResults"] = webContext.webClearnet;
        web["lastDarknetResults"] = webContext.webDarknet;
        if (!webQuery.empty()) {
            web["query"] = webQuery;
        }
        out["web"] = web;
    }
    return out.dump();
}

} // namespace synapse::rpc
