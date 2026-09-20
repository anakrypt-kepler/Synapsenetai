#pragma once

// Shared harvest claims for multiple local NAAN loops.
// One agent may own a URL at a time. Topics are shared work: extra crew
// keep fetching with a different engine tick instead of freezing.
// Filed sha256 values are sticky so a restart of one loop does not republish.

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace synapse {
namespace core {

class NaanTaskShare {
public:
    // Returns false if another agent already holds the URL.
    bool claimUrl(const std::string& agentId, const std::string& url);
    void releaseUrl(const std::string& agentId, const std::string& url);

    bool claimTopic(const std::string& agentId, const std::string& topic);
    void releaseTopic(const std::string& agentId, const std::string& topic);

    // Returns false if this hash was already filed by any agent.
    bool noteHash(const std::string& hash);

    void reset();

    size_t claimedUrlCount() const;
    size_t filedHashCount() const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::string> urlOwner_;
    std::unordered_map<std::string, std::string> topicOwner_;
    std::unordered_set<std::string> hashes_;
};

} // namespace core
} // namespace synapse
