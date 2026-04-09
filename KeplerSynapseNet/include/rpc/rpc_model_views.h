#pragma once

#include "model/model_loader.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <string>
#include <vector>

namespace synapse::rpc {

struct RpcModelRemoteOfferView {
    std::string offerId;
    std::string peerId;
    uint64_t receivedAt = 0;
    std::string modelId;
    std::string providerAddress;
    uint64_t pricePerRequestAtoms = 0;
    uint32_t maxSlots = 0;
    uint32_t usedSlots = 0;
    uint64_t expiresAt = 0;
};

std::string accessModeToString(model::AccessMode mode);

model::AccessMode parseAccessMode(const std::string& value);

std::string buildRpcModelStatusResponse(
    bool loaded,
    const std::string& state,
    bool generating,
    const model::ModelInfo& info,
    const std::string& error,
    uint64_t requests);

std::string buildRpcModelListResponse(
    const std::vector<model::ModelInfo>& models);

std::string buildRpcModelLoadResponse(
    bool ok,
    const std::string& state,
    const std::string& error,
    const model::ModelInfo& info);

std::string buildRpcModelUnloadResponse(
    bool ok,
    const std::string& state);

std::string buildRpcModelRemoteListResponse(
    const std::vector<RpcModelRemoteOfferView>& offers);

std::string buildRpcModelRemoteRentResponse(
    const std::string& offerId,
    const std::string& peerId,
    const std::string& sessionId,
    const std::string& providerAddress,
    uint64_t pricePerRequestAtoms,
    uint64_t expiresAt);

std::string buildRpcModelOkResponse();

std::string buildRpcModelAccessGetResponse(
    model::AccessMode mode,
    uint32_t maxSlots,
    uint32_t activeSlots,
    uint32_t availableSlots,
    uint64_t pricePerHourAtoms,
    uint64_t remotePricePerRequestAtoms);

std::string buildRpcMarketListingsResponse(
    const std::vector<model::ModelListingInfo>& listings);

std::string buildRpcMarketStatsResponse(
    const model::MarketplaceStats& stats);

} // namespace synapse::rpc
