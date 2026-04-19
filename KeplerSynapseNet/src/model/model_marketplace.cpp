#include "model/model_loader.h"
#include "crypto/crypto.h"
#include <cstring>
#include <mutex>
#include <map>
#include <vector>
#include <algorithm>
#include <random>
#include <ctime>

namespace synapse {
namespace model {

static bool safeAddU64(uint64_t a, uint64_t b, uint64_t& out) {
    if (UINT64_MAX - a < b) return false;
    out = a + b;
    return true;
}

struct ModelListing {
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
    uint32_t ratingMilli = 0; // 0..5000
    uint32_t ratingCount = 0;
    uint64_t totalRequests;
    uint64_t totalEarningsAtoms = 0;
    uint64_t totalVolumeAtoms = 0;
    bool active;
    uint64_t createdAt;
    uint64_t lastActive;
};

struct RentalSession {
    std::string sessionId;
    std::string modelId;
    std::string renterId;
    uint64_t startTime;
    uint64_t endTime;
    uint64_t totalPaidAtoms = 0;
    uint32_t requestCount = 0;
    uint64_t tokensUsed = 0;
    uint64_t totalLatencyMs = 0;
    bool active;
};

struct ModelMarketplace::Impl {
    std::map<std::string, ModelListing> listings;
    std::map<std::string, RentalSession> sessions;
    std::map<std::string, std::vector<std::string>> ownerListings;
    std::map<std::string, std::vector<std::string>> renterSessions;
    mutable std::mutex mtx;
    
    std::function<void(const std::string&, uint64_t)> onPayment;
    std::function<void(const std::string&)> onSessionStart;
    std::function<void(const std::string&)> onSessionEnd;
    
    std::string generateId();
};

std::string ModelMarketplace::Impl::generateId() {
    std::vector<uint8_t> random(16);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 16; i++) {
        random[i] = static_cast<uint8_t>(dis(gen));
    }
    return crypto::toHex(random.data(), random.size());
}

ModelMarketplace::ModelMarketplace() : impl_(std::make_unique<Impl>()) {}
ModelMarketplace::~ModelMarketplace() = default;

std::string ModelMarketplace::listModel(const std::string& ownerId, const std::string& name,
                                         const std::string& description, uint64_t size,
                                         const std::string& format, uint64_t pricePerHourAtoms,
                                         uint64_t pricePerRequestAtoms, uint32_t maxSlots) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    ModelListing listing;
    listing.modelId = impl_->generateId();
    listing.ownerId = ownerId;
    listing.name = name;
    listing.description = description;
    listing.size = size;
    listing.format = format;
    listing.pricePerHourAtoms = pricePerHourAtoms;
    listing.pricePerRequestAtoms = pricePerRequestAtoms;
    listing.maxSlots = maxSlots;
    listing.usedSlots = 0;
    listing.ratingMilli = 0;
    listing.ratingCount = 0;
    listing.totalRequests = 0;
    listing.totalEarningsAtoms = 0;
    listing.totalVolumeAtoms = 0;
    listing.active = true;
    listing.createdAt = std::time(nullptr);
    listing.lastActive = listing.createdAt;
    
    impl_->listings[listing.modelId] = listing;
    impl_->ownerListings[ownerId].push_back(listing.modelId);
    
    return listing.modelId;
}

bool ModelMarketplace::upsertModel(const std::string& listingId, const std::string& ownerId, const std::string& name,
                                   const std::string& description, uint64_t size, const std::string& format,
                                   uint64_t pricePerHourAtoms, uint64_t pricePerRequestAtoms, uint32_t maxSlots, bool active) {
    if (listingId.empty()) return false;
    std::lock_guard<std::mutex> lock(impl_->mtx);

    auto it = impl_->listings.find(listingId);
    if (it == impl_->listings.end()) {
        ModelListing listing;
        listing.modelId = listingId;
        listing.ownerId = ownerId;
        listing.name = name;
        listing.description = description;
        listing.size = size;
        listing.format = format;
        listing.pricePerHourAtoms = pricePerHourAtoms;
        listing.pricePerRequestAtoms = pricePerRequestAtoms;
        listing.maxSlots = maxSlots;
        listing.usedSlots = 0;
        listing.ratingMilli = 0;
        listing.ratingCount = 0;
        listing.totalRequests = 0;
        listing.totalEarningsAtoms = 0;
        listing.totalVolumeAtoms = 0;
        listing.active = active;
        listing.createdAt = std::time(nullptr);
        listing.lastActive = listing.createdAt;
        impl_->listings[listingId] = listing;
        impl_->ownerListings[ownerId].push_back(listingId);
        return true;
    }

    ModelListing& listing = it->second;
    listing.ownerId = ownerId;
    listing.name = name;
    listing.description = description;
    listing.size = size;
    listing.format = format;
    listing.pricePerHourAtoms = pricePerHourAtoms;
    listing.pricePerRequestAtoms = pricePerRequestAtoms;
    listing.maxSlots = maxSlots;
    listing.active = active;
    listing.lastActive = std::time(nullptr);
    return true;
}

bool ModelMarketplace::unlistModel(const std::string& modelId, const std::string& ownerId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->listings.find(modelId);
    if (it == impl_->listings.end()) return false;
    if (it->second.ownerId != ownerId) return false;
    
    it->second.active = false;
    
    return true;
}

bool ModelMarketplace::updatePrice(const std::string& modelId, const std::string& ownerId,
                                    uint64_t pricePerHourAtoms, uint64_t pricePerRequestAtoms) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->listings.find(modelId);
    if (it == impl_->listings.end()) return false;
    if (it->second.ownerId != ownerId) return false;
    
    it->second.pricePerHourAtoms = pricePerHourAtoms;
    it->second.pricePerRequestAtoms = pricePerRequestAtoms;
    
    return true;
}

bool ModelMarketplace::updateSlots(const std::string& modelId, const std::string& ownerId, uint32_t maxSlots) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->listings.find(modelId);
    if (it == impl_->listings.end()) return false;
    if (it->second.ownerId != ownerId) return false;
    
    it->second.maxSlots = maxSlots;
    
    return true;
}

std::string ModelMarketplace::rentModel(const std::string& modelId, const std::string& renterId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->listings.find(modelId);
    if (it == impl_->listings.end()) return "";
    if (!it->second.active) return "";
    if (it->second.usedSlots >= it->second.maxSlots) return "";
    
    RentalSession session;
    session.sessionId = impl_->generateId();
    session.modelId = modelId;
    session.renterId = renterId;
    session.startTime = std::time(nullptr);
    session.endTime = 0;
    session.totalPaidAtoms = 0;
    session.requestCount = 0;
    session.tokensUsed = 0;
    session.totalLatencyMs = 0;
    session.active = true;
    
    impl_->sessions[session.sessionId] = session;
    impl_->renterSessions[renterId].push_back(session.sessionId);
    it->second.usedSlots++;
    it->second.lastActive = session.startTime;
    
    if (impl_->onSessionStart) {
        impl_->onSessionStart(session.sessionId);
    }
    
    return session.sessionId;
}

bool ModelMarketplace::endRental(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->sessions.find(sessionId);
    if (it == impl_->sessions.end()) return false;
    if (!it->second.active) return false;
    
    it->second.active = false;
    it->second.endTime = std::time(nullptr);
    
    auto listingIt = impl_->listings.find(it->second.modelId);
    if (listingIt != impl_->listings.end()) {
        listingIt->second.usedSlots--;
    }
    
    if (impl_->onSessionEnd) {
        impl_->onSessionEnd(sessionId);
    }
    
    return true;
}

bool ModelMarketplace::recordRequest(const std::string& sessionId, uint32_t tokensUsed, uint64_t latencyMs) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->sessions.find(sessionId);
    if (it == impl_->sessions.end()) return false;
    if (!it->second.active) return false;
    
    it->second.requestCount++;
    {
        uint64_t tmp = 0;
        if (!safeAddU64(it->second.tokensUsed, tokensUsed, tmp)) return false;
        it->second.tokensUsed = tmp;
    }
    {
        uint64_t tmp = 0;
        if (!safeAddU64(it->second.totalLatencyMs, latencyMs, tmp)) return false;
        it->second.totalLatencyMs = tmp;
    }
    
    auto listingIt = impl_->listings.find(it->second.modelId);
    if (listingIt != impl_->listings.end()) {
        listingIt->second.totalRequests++;
        listingIt->second.lastActive = std::time(nullptr);
    }
    
    return true;
}

bool ModelMarketplace::recordPayment(const std::string& sessionId, uint64_t amountAtoms) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->sessions.find(sessionId);
    if (it == impl_->sessions.end()) return false;
    if (!it->second.active) return false;

    uint64_t tmp = 0;
    if (!safeAddU64(it->second.totalPaidAtoms, amountAtoms, tmp)) return false;
    it->second.totalPaidAtoms = tmp;

    auto listingIt = impl_->listings.find(it->second.modelId);
    if (listingIt != impl_->listings.end()) {
        uint64_t t1 = 0;
        if (!safeAddU64(listingIt->second.totalEarningsAtoms, amountAtoms, t1)) return false;
        listingIt->second.totalEarningsAtoms = t1;

        uint64_t t2 = 0;
        if (!safeAddU64(listingIt->second.totalVolumeAtoms, amountAtoms, t2)) return false;
        listingIt->second.totalVolumeAtoms = t2;

        listingIt->second.lastActive = std::time(nullptr);
        if (impl_->onPayment) {
            impl_->onPayment(listingIt->second.ownerId, amountAtoms);
        }
    }
    return true;
}

bool ModelMarketplace::rateModel(const std::string& modelId, const std::string& renterId, uint32_t ratingMilli) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    (void)renterId;
    if (ratingMilli > 5000) return false;
    
    auto it = impl_->listings.find(modelId);
    if (it == impl_->listings.end()) return false;
    
    it->second.ratingCount++;
    uint64_t n = it->second.ratingCount;
    uint64_t total = static_cast<uint64_t>(it->second.ratingMilli) * (n - 1) + ratingMilli;
    it->second.ratingMilli = static_cast<uint32_t>(total / n);
    
    return true;
}

std::vector<ModelListingInfo> ModelMarketplace::searchModels(const std::string& query, 
                                                              const std::string& sortBy,
                                                              const std::string& format,
                                                              uint64_t maxPricePerHourAtoms, int limit) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<ModelListingInfo> results;
    
    for (const auto& [id, listing] : impl_->listings) {
        if (!listing.active) continue;
        if (!format.empty() && listing.format != format) continue;
        if (maxPricePerHourAtoms > 0 && listing.pricePerHourAtoms > maxPricePerHourAtoms) continue;
        
        if (!query.empty()) {
            bool match = listing.name.find(query) != std::string::npos ||
                        listing.description.find(query) != std::string::npos;
            if (!match) continue;
        }
        
        ModelListingInfo info;
        info.modelId = listing.modelId;
        info.ownerId = listing.ownerId;
        info.name = listing.name;
        info.description = listing.description;
        info.size = listing.size;
        info.format = listing.format;
        info.pricePerHourAtoms = listing.pricePerHourAtoms;
        info.pricePerRequestAtoms = listing.pricePerRequestAtoms;
        info.maxSlots = listing.maxSlots;
        info.usedSlots = listing.usedSlots;
        info.availableSlots = listing.maxSlots > listing.usedSlots ? (listing.maxSlots - listing.usedSlots) : 0;
        info.ratingMilli = listing.ratingMilli;
        info.ratingCount = listing.ratingCount;
        info.totalRequests = listing.totalRequests;
        info.totalEarningsAtoms = listing.totalEarningsAtoms;
        info.active = listing.active;
        info.createdAt = listing.createdAt;
        info.lastActive = listing.lastActive;
        
        results.push_back(info);
        
        if (limit > 0 && (int)results.size() >= limit) break;
    }
    
    if (sortBy == "price") {
        std::sort(results.begin(), results.end(), [](const ModelListingInfo& a, const ModelListingInfo& b) {
            return a.pricePerRequestAtoms < b.pricePerRequestAtoms;
        });
    } else if (sortBy == "recent") {
        std::sort(results.begin(), results.end(), [](const ModelListingInfo& a, const ModelListingInfo& b) {
            return a.lastActive > b.lastActive;
        });
    } else {
        std::sort(results.begin(), results.end(), [](const ModelListingInfo& a, const ModelListingInfo& b) {
            return a.ratingMilli > b.ratingMilli;
        });
    }
    
    return results;
}

std::vector<ModelListingInfo> ModelMarketplace::getOwnerListings(const std::string& ownerId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<ModelListingInfo> results;
    
    auto it = impl_->ownerListings.find(ownerId);
    if (it == impl_->ownerListings.end()) return results;
    
    for (const std::string& modelId : it->second) {
        auto listingIt = impl_->listings.find(modelId);
        if (listingIt == impl_->listings.end()) continue;
        
        const ModelListing& listing = listingIt->second;
        
        ModelListingInfo info;
        info.modelId = listing.modelId;
        info.ownerId = listing.ownerId;
        info.name = listing.name;
        info.description = listing.description;
        info.size = listing.size;
        info.format = listing.format;
        info.pricePerHourAtoms = listing.pricePerHourAtoms;
        info.pricePerRequestAtoms = listing.pricePerRequestAtoms;
        info.maxSlots = listing.maxSlots;
        info.usedSlots = listing.usedSlots;
        info.availableSlots = listing.maxSlots > listing.usedSlots ? (listing.maxSlots - listing.usedSlots) : 0;
        info.ratingMilli = listing.ratingMilli;
        info.ratingCount = listing.ratingCount;
        info.totalRequests = listing.totalRequests;
        info.totalEarningsAtoms = listing.totalEarningsAtoms;
        info.active = listing.active;
        info.createdAt = listing.createdAt;
        info.lastActive = listing.lastActive;
        
        results.push_back(info);
    }
    
    return results;
}

std::vector<ModelListingInfo> ModelMarketplace::getAllListings(bool includeInactive) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<ModelListingInfo> results;
    for (const auto& [id, listing] : impl_->listings) {
        if (!includeInactive && !listing.active) continue;
        ModelListingInfo info;
        info.modelId = listing.modelId;
        info.ownerId = listing.ownerId;
        info.name = listing.name;
        info.description = listing.description;
        info.size = listing.size;
        info.format = listing.format;
        info.pricePerHourAtoms = listing.pricePerHourAtoms;
        info.pricePerRequestAtoms = listing.pricePerRequestAtoms;
        info.maxSlots = listing.maxSlots;
        info.usedSlots = listing.usedSlots;
        info.availableSlots = listing.maxSlots > listing.usedSlots ? (listing.maxSlots - listing.usedSlots) : 0;
        info.ratingMilli = listing.ratingMilli;
        info.ratingCount = listing.ratingCount;
        info.totalRequests = listing.totalRequests;
        info.totalEarningsAtoms = listing.totalEarningsAtoms;
        info.active = listing.active;
        info.createdAt = listing.createdAt;
        info.lastActive = listing.lastActive;
        results.push_back(info);
    }
    return results;
}

MarketplaceStats ModelMarketplace::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    MarketplaceStats stats;
    stats.totalListings = 0;
    stats.activeListings = 0;
    stats.totalRentals = 0;
    stats.activeRentals = 0;
    stats.totalSessions = impl_->sessions.size();
    stats.activeSessions = 0;
    stats.totalRequests = 0;
    stats.totalVolumeAtoms = 0;
    stats.totalEarningsAtoms = 0;
    stats.avgPricePerRequestAtoms = 0;
    
    for (const auto& [id, listing] : impl_->listings) {
        stats.totalListings++;
        if (listing.active) stats.activeListings++;
        stats.totalRequests += listing.totalRequests;
        uint64_t tmp = 0;
        if (safeAddU64(stats.totalEarningsAtoms, listing.totalEarningsAtoms, tmp)) stats.totalEarningsAtoms = tmp;
        if (safeAddU64(stats.totalVolumeAtoms, listing.totalVolumeAtoms, tmp)) stats.totalVolumeAtoms = tmp;
    }
    
    for (const auto& [id, session] : impl_->sessions) {
        stats.totalRentals++;
        if (session.active) {
            stats.activeSessions++;
            stats.activeRentals++;
        }
    }

    // avg price per request over active listings (simple mean)
    uint64_t count = 0;
    uint64_t sum = 0;
    for (const auto& [id, listing] : impl_->listings) {
        if (!listing.active) continue;
        uint64_t tmp = 0;
        if (!safeAddU64(sum, listing.pricePerRequestAtoms, tmp)) {
            sum = UINT64_MAX;
            break;
        }
        sum = tmp;
        count++;
    }
    if (count > 0) stats.avgPricePerRequestAtoms = sum / count;
    
    return stats;
}

void ModelMarketplace::onPaymentReceived(std::function<void(const std::string&, uint64_t)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onPayment = callback;
}

void ModelMarketplace::onSessionStarted(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onSessionStart = callback;
}

void ModelMarketplace::onSessionEnded(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onSessionEnd = callback;
}

}
}
