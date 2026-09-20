#include "core/naan_task_share.h"

namespace synapse {
namespace core {

bool NaanTaskShare::claimUrl(const std::string& agentId, const std::string& url) {
    if (agentId.empty() || url.empty()) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = urlOwner_.find(url);
    if (it == urlOwner_.end()) {
        urlOwner_[url] = agentId;
        return true;
    }
    return it->second == agentId;
}

void NaanTaskShare::releaseUrl(const std::string& agentId, const std::string& url) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = urlOwner_.find(url);
    if (it != urlOwner_.end() && it->second == agentId) urlOwner_.erase(it);
}

bool NaanTaskShare::claimTopic(const std::string& agentId, const std::string& topic) {
    if (agentId.empty() || topic.empty()) return false;
    // Topics are a shared queue. URL exclusivity still blocks duplicate fetches.
    return true;
}

void NaanTaskShare::releaseTopic(const std::string& agentId, const std::string& topic) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = topicOwner_.find(topic);
    if (it != topicOwner_.end() && it->second == agentId) topicOwner_.erase(it);
}

bool NaanTaskShare::noteHash(const std::string& hash) {
    if (hash.empty()) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    auto r = hashes_.insert(hash);
    return r.second;
}

void NaanTaskShare::reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    urlOwner_.clear();
    topicOwner_.clear();
    hashes_.clear();
}

size_t NaanTaskShare::claimedUrlCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return urlOwner_.size();
}

size_t NaanTaskShare::filedHashCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return hashes_.size();
}

} // namespace core
} // namespace synapse
