#include "rpc/rpc_model_views.h"

#include "infrastructure/messages.h"

#include <cctype>
#include <stdexcept>

namespace synapse::rpc {

namespace {

using json = nlohmann::json;

} // namespace

std::string accessModeToString(model::AccessMode mode) {
    switch (mode) {
        case model::AccessMode::PRIVATE: return "PRIVATE";
        case model::AccessMode::SHARED: return "SHARED";
        case model::AccessMode::PAID: return "PAID";
        case model::AccessMode::COMMUNITY: return "COMMUNITY";
    }
    return "UNKNOWN";
}

model::AccessMode parseAccessMode(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        normalized.push_back(
            static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    if (normalized == "PRIVATE") return model::AccessMode::PRIVATE;
    if (normalized == "SHARED") return model::AccessMode::SHARED;
    if (normalized == "PAID") return model::AccessMode::PAID;
    if (normalized == "COMMUNITY" || normalized == "PUBLIC") {
        return model::AccessMode::COMMUNITY;
    }
    throw std::runtime_error("invalid access mode");
}

std::string buildRpcModelStatusResponse(
    bool loaded,
    const std::string& state,
    bool generating,
    const model::ModelInfo& info,
    const std::string& error,
    uint64_t requests) {
    json out;
    out["loaded"] = loaded;
    out["state"] = state;
    out["generating"] = generating;
    out["name"] = info.name;
    out["path"] = info.path;
    out["sizeBytes"] = info.sizeBytes;
    out["error"] = error;
    out["requests"] = requests;
    return out.dump();
}

std::string buildRpcModelListResponse(
    const std::vector<model::ModelInfo>& models) {
    json out = json::array();
    for (const auto& modelInfo : models) {
        out.push_back({
            {"name", modelInfo.name},
            {"path", modelInfo.path},
            {"sizeBytes", modelInfo.sizeBytes},
            {"format", static_cast<int>(modelInfo.format)},
            {"quantization", modelInfo.quantization}
        });
    }
    return out.dump();
}

std::string buildRpcModelLoadResponse(
    bool ok,
    const std::string& state,
    const std::string& error,
    const model::ModelInfo& info) {
    json out;
    out["ok"] = ok;
    out["state"] = state;
    out["error"] = error;
    out["name"] = info.name;
    out["path"] = info.path;
    out["sizeBytes"] = info.sizeBytes;
    return out.dump();
}

std::string buildRpcModelUnloadResponse(
    bool ok,
    const std::string& state) {
    json out;
    out["ok"] = ok;
    out["state"] = state;
    return out.dump();
}

std::string buildRpcModelRemoteListResponse(
    const std::vector<RpcModelRemoteOfferView>& offers) {
    json out = json::array();
    for (const auto& offer : offers) {
        out.push_back({
            {"offerId", offer.offerId},
            {"peerId", offer.peerId},
            {"receivedAt", offer.receivedAt},
            {"modelId", offer.modelId},
            {"providerAddress", offer.providerAddress},
            {"pricePerRequestAtoms", offer.pricePerRequestAtoms},
            {"maxSlots", offer.maxSlots},
            {"usedSlots", offer.usedSlots},
            {"expiresAt", offer.expiresAt}
        });
    }
    return out.dump();
}

std::string buildRpcModelRemoteRentResponse(
    const std::string& offerId,
    const std::string& peerId,
    const std::string& sessionId,
    const std::string& providerAddress,
    uint64_t pricePerRequestAtoms,
    uint64_t expiresAt) {
    json out;
    out["ok"] = true;
    out["offerId"] = offerId;
    out["peerId"] = peerId;
    out["sessionId"] = sessionId;
    out["providerAddress"] = providerAddress;
    out["pricePerRequestAtoms"] = pricePerRequestAtoms;
    out["expiresAt"] = expiresAt;
    return out.dump();
}

std::string buildRpcModelOkResponse() {
    return json({{"ok", true}}).dump();
}

std::string buildRpcModelAccessGetResponse(
    model::AccessMode mode,
    uint32_t maxSlots,
    uint32_t activeSlots,
    uint32_t availableSlots,
    uint64_t pricePerHourAtoms,
    uint64_t remotePricePerRequestAtoms) {
    json out;
    out["mode"] = accessModeToString(mode);
    out["maxSlots"] = maxSlots;
    out["activeSlots"] = activeSlots;
    out["availableSlots"] = availableSlots;
    out["pricePerHourAtoms"] = pricePerHourAtoms;
    out["remotePricePerRequestAtoms"] = remotePricePerRequestAtoms;
    return out.dump();
}

std::string buildRpcMarketListingsResponse(
    const std::vector<model::ModelListingInfo>& listings) {
    json out = json::array();
    for (const auto& listing : listings) {
        out.push_back({
            {"modelId", listing.modelId},
            {"ownerId", listing.ownerId},
            {"name", listing.name},
            {"description", listing.description},
            {"sizeBytes", listing.size},
            {"format", listing.format},
            {"pricePerHourAtoms", listing.pricePerHourAtoms},
            {"pricePerRequestAtoms", listing.pricePerRequestAtoms},
            {"maxSlots", listing.maxSlots},
            {"usedSlots", listing.usedSlots},
            {"availableSlots", listing.availableSlots},
            {"ratingMilli", listing.ratingMilli},
            {"ratingCount", listing.ratingCount},
            {"totalRequests", listing.totalRequests},
            {"totalEarningsAtoms", listing.totalEarningsAtoms},
            {"active", listing.active},
            {"createdAt", listing.createdAt},
            {"lastActive", listing.lastActive}
        });
    }
    return out.dump();
}

std::string buildRpcMarketStatsResponse(
    const model::MarketplaceStats& stats) {
    json out;
    out["totalListings"] = stats.totalListings;
    out["activeListings"] = stats.activeListings;
    out["totalRentals"] = stats.totalRentals;
    out["activeRentals"] = stats.activeRentals;
    out["totalSessions"] = stats.totalSessions;
    out["activeSessions"] = stats.activeSessions;
    out["totalRequests"] = stats.totalRequests;
    out["totalVolumeAtoms"] = stats.totalVolumeAtoms;
    out["totalEarningsAtoms"] = stats.totalEarningsAtoms;
    out["avgPricePerRequestAtoms"] = stats.avgPricePerRequestAtoms;
    return out.dump();
}

} // namespace synapse::rpc
