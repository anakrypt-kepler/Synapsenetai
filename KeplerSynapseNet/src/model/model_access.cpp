#include "model/model_access.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <ctime>
#include <algorithm>
#include <fstream>

namespace synapse {
namespace model {

struct ModelAccess::Impl {
    std::unordered_map<std::string, AccessEntry> acl;
    std::unordered_map<std::string, std::string> banReasons;
    std::unordered_map<std::string, uint64_t> requestCounts;
    std::vector<SessionInfo> sessions;
    AccessMode mode = AccessMode::PRIVATE;
    uint64_t price = 0;
    uint32_t maxSlots = 3;
    uint32_t rateLimit = 60;
    uint64_t earningsTotal = 0;
    mutable std::mutex mtx;
    
    std::function<void(const std::string&)> sessionStartCb;
    std::function<void(const SessionInfo&)> sessionEndCb;
    std::function<void(const std::string&, uint64_t)> paymentCb;
};

ModelAccess::ModelAccess() : impl_(std::make_unique<Impl>()) {}
ModelAccess::~ModelAccess() = default;

bool ModelAccess::invite(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    AccessEntry entry;
    entry.nodeId = nodeId;
    entry.status = NodeStatus::INVITED;
    entry.paidTotal = 0;
    entry.lastActive = 0;
    entry.sessionStart = 0;
    entry.sessionRequests = 0;
    entry.avgLatency = 0;
    impl_->acl[nodeId] = entry;
    return true;
}

bool ModelAccess::kick(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->acl.find(nodeId);
    if (it != impl_->acl.end() && it->second.status == NodeStatus::ACTIVE) {
        endSession(nodeId);
    }
    impl_->acl.erase(nodeId);
    return true;
}

bool ModelAccess::ban(const std::string& nodeId, const std::string& reason) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->acl.find(nodeId);
    if (it != impl_->acl.end() && it->second.status == NodeStatus::ACTIVE) {
        endSession(nodeId);
    }
    impl_->acl[nodeId].status = NodeStatus::BANNED;
    impl_->banReasons[nodeId] = reason;
    return true;
}

bool ModelAccess::unban(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->acl.find(nodeId);
    if (it == impl_->acl.end()) return false;
    it->second.status = NodeStatus::INVITED;
    impl_->banReasons.erase(nodeId);
    return true;
}

bool ModelAccess::setMode(AccessMode mode) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->mode = mode;
    return true;
}

bool ModelAccess::setPrice(uint64_t ngtPerHour) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->price = ngtPerHour;
    return true;
}

bool ModelAccess::setMaxSlots(uint32_t slots) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxSlots = slots;
    return true;
}

bool ModelAccess::setRateLimit(uint32_t requestsPerMinute) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->rateLimit = requestsPerMinute;
    return true;
}

AccessMode ModelAccess::getMode() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->mode;
}

uint64_t ModelAccess::getPrice() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->price;
}

uint32_t ModelAccess::getMaxSlots() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->maxSlots;
}

uint32_t ModelAccess::getActiveSlots() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint32_t active = 0;
    for (const auto& [id, entry] : impl_->acl) {
        if (entry.status == NodeStatus::ACTIVE) active++;
    }
    return active;
}

uint32_t ModelAccess::getAvailableSlots() const {
    return getMaxSlots() - getActiveSlots();
}

bool ModelAccess::canAccess(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->mode == AccessMode::PRIVATE) return false;
    auto it = impl_->acl.find(nodeId);
    if (it != impl_->acl.end() && it->second.status == NodeStatus::BANNED) return false;
    if (impl_->mode == AccessMode::SHARED) {
        return it != impl_->acl.end() && 
               (it->second.status == NodeStatus::INVITED || it->second.status == NodeStatus::ACTIVE);
    }
    return true;
}

bool ModelAccess::hasAvailableSlot() const {
    return getAvailableSlots() > 0;
}

bool ModelAccess::isRateLimited(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->requestCounts.find(nodeId);
    if (it == impl_->requestCounts.end()) return false;
    return it->second >= impl_->rateLimit;
}

bool ModelAccess::startSession(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!hasAvailableSlot()) return false;
    
    auto& entry = impl_->acl[nodeId];
    entry.status = NodeStatus::ACTIVE;
    entry.sessionStart = std::time(nullptr);
    entry.sessionRequests = 0;
    entry.lastActive = entry.sessionStart;
    
    if (impl_->sessionStartCb) impl_->sessionStartCb(nodeId);
    return true;
}

bool ModelAccess::endSession(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->acl.find(nodeId);
    if (it == impl_->acl.end() || it->second.status != NodeStatus::ACTIVE) return false;
    
    SessionInfo session;
    session.nodeId = nodeId;
    session.startTime = it->second.sessionStart;
    session.endTime = std::time(nullptr);
    session.requests = it->second.sessionRequests;
    session.tokensGenerated = session.requests * 100;
    session.amountPaid = it->second.paidTotal;
    impl_->sessions.push_back(session);
    
    it->second.status = NodeStatus::EXPIRED;
    it->second.sessionStart = 0;
    
    if (impl_->sessionEndCb) impl_->sessionEndCb(session);
    return true;
}

bool ModelAccess::recordRequest(const std::string& nodeId, uint32_t tokens, double latency) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->acl.find(nodeId);
    if (it == impl_->acl.end()) return false;
    
    it->second.sessionRequests++;
    it->second.lastActive = std::time(nullptr);
    double n = static_cast<double>(it->second.sessionRequests);
    it->second.avgLatency = ((n - 1) * it->second.avgLatency + latency) / n;
    impl_->requestCounts[nodeId]++;
    
    return true;
}

bool ModelAccess::recordPayment(const std::string& nodeId, uint64_t amount) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->acl.find(nodeId);
    if (it == impl_->acl.end()) return false;
    
    it->second.paidTotal += amount;
    impl_->earningsTotal += amount;
    
    if (impl_->paymentCb) impl_->paymentCb(nodeId, amount);
    return true;
}

std::vector<AccessEntry> ModelAccess::getACL() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<AccessEntry> result;
    for (const auto& [id, entry] : impl_->acl) {
        result.push_back(entry);
    }
    return result;
}

std::vector<AccessEntry> ModelAccess::getActiveUsers() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<AccessEntry> result;
    for (const auto& [id, entry] : impl_->acl) {
        if (entry.status == NodeStatus::ACTIVE) result.push_back(entry);
    }
    return result;
}

std::vector<SessionInfo> ModelAccess::getRecentSessions(size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<SessionInfo> result;
    size_t start = impl_->sessions.size() > limit ? impl_->sessions.size() - limit : 0;
    for (size_t i = impl_->sessions.size(); i > start; i--) {
        result.push_back(impl_->sessions[i - 1]);
    }
    return result;
}

AccessEntry ModelAccess::getEntry(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->acl.find(nodeId);
    return it != impl_->acl.end() ? it->second : AccessEntry{};
}

EarningsReport ModelAccess::getEarnings() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    EarningsReport report{};
    uint64_t now = std::time(nullptr);
    uint64_t dayStart = now - (now % 86400);
    uint64_t weekStart = now - (now % 604800);
    
    std::unordered_set<std::string> todayUsers;
    for (const auto& session : impl_->sessions) {
        if (session.endTime >= dayStart) {
            report.today += session.amountPaid;
            report.sessionsToday++;
            todayUsers.insert(session.nodeId);
        }
        if (session.endTime >= weekStart) {
            report.thisWeek += session.amountPaid;
        }
    }
    report.uniqueUsersToday = todayUsers.size();
    report.total = impl_->earningsTotal;
    return report;
}

uint64_t ModelAccess::getEarningsToday() const {
    return getEarnings().today;
}

uint64_t ModelAccess::getEarningsWeek() const {
    return getEarnings().thisWeek;
}

uint64_t ModelAccess::getEarningsTotal() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->earningsTotal;
}

void ModelAccess::onSessionStart(std::function<void(const std::string&)> callback) {
    impl_->sessionStartCb = callback;
}

void ModelAccess::onSessionEnd(std::function<void(const SessionInfo&)> callback) {
    impl_->sessionEndCb = callback;
}

void ModelAccess::onPaymentReceived(std::function<void(const std::string&, uint64_t)> callback) {
    impl_->paymentCb = callback;
}

void ModelAccess::processExpiredSessions() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    uint64_t timeout = 3600;
    
    for (auto& [id, entry] : impl_->acl) {
        if (entry.status == NodeStatus::ACTIVE && now - entry.lastActive > timeout) {
            entry.status = NodeStatus::EXPIRED;
        }
    }
    impl_->requestCounts.clear();
}

}
}
