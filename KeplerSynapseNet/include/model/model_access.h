#pragma once

#include "model_loader.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace synapse {
namespace model {

enum class NodeStatus {
    ACTIVE,
    INVITED,
    PENDING,
    BANNED,
    EXPIRED
};

struct AccessEntry {
    std::string nodeId;
    NodeStatus status;
    uint64_t paidTotal;
    uint64_t lastActive;
    uint64_t sessionStart;
    uint64_t sessionRequests;
    double avgLatency;
};

struct SessionInfo {
    std::string nodeId;
    uint64_t startTime;
    uint64_t endTime;
    uint64_t requests;
    uint64_t tokensGenerated;
    uint64_t amountPaid;
};

struct EarningsReport {
    uint64_t today;
    uint64_t thisWeek;
    uint64_t thisMonth;
    uint64_t total;
    uint32_t sessionsToday;
    uint32_t uniqueUsersToday;
};

class ModelAccess {
public:
    ModelAccess();
    ~ModelAccess();
    
    bool invite(const std::string& nodeId);
    bool kick(const std::string& nodeId);
    bool ban(const std::string& nodeId, const std::string& reason = "");
    bool unban(const std::string& nodeId);
    
    bool setMode(AccessMode mode);
    bool setPrice(uint64_t ngtPerHour);
    bool setMaxSlots(uint32_t slots);
    bool setRateLimit(uint32_t requestsPerMinute);
    
    AccessMode getMode() const;
    uint64_t getPrice() const;
    uint32_t getMaxSlots() const;
    uint32_t getActiveSlots() const;
    uint32_t getAvailableSlots() const;
    
    bool canAccess(const std::string& nodeId) const;
    bool hasAvailableSlot() const;
    bool isRateLimited(const std::string& nodeId) const;
    
    bool startSession(const std::string& nodeId);
    bool endSession(const std::string& nodeId);
    bool recordRequest(const std::string& nodeId, uint32_t tokens, double latency);
    bool recordPayment(const std::string& nodeId, uint64_t amount);
    
    std::vector<AccessEntry> getACL() const;
    std::vector<AccessEntry> getActiveUsers() const;
    std::vector<SessionInfo> getRecentSessions(size_t limit = 10) const;
    AccessEntry getEntry(const std::string& nodeId) const;
    
    EarningsReport getEarnings() const;
    uint64_t getEarningsToday() const;
    uint64_t getEarningsWeek() const;
    uint64_t getEarningsTotal() const;
    
    void onSessionStart(std::function<void(const std::string&)> callback);
    void onSessionEnd(std::function<void(const SessionInfo&)> callback);
    void onPaymentReceived(std::function<void(const std::string&, uint64_t)> callback);
    
    void processExpiredSessions();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
