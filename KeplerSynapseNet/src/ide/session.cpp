#include "ide/session.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>
#include <unordered_map>

namespace synapse {
namespace ide {

namespace {

std::string generateId() {
    static std::mt19937 gen(std::random_device{}());
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 32; ++i) {
        id.push_back(chars[dist(gen)]);
    }
    return id;
}

int64_t nowTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}

struct SessionService::Impl {
    mutable std::mutex mtx;
    std::unordered_map<std::string, Session> sessions;
    std::unordered_map<std::string, std::vector<SessionMessage>> messages;
};

SessionService::SessionService() : impl_(std::make_unique<Impl>()) {}
SessionService::~SessionService() = default;

Session SessionService::create(const std::string& title) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    Session s;
    s.id = generateId();
    s.title = title;
    s.createdAt = nowTimestamp();
    s.updatedAt = s.createdAt;
    impl_->sessions[s.id] = s;
    return s;
}

Session SessionService::createThread(const std::string& parentSessionId,
                                      const std::string& title) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    Session s;
    s.id = generateId();
    s.parentSessionId = parentSessionId;
    s.title = title;
    s.createdAt = nowTimestamp();
    s.updatedAt = s.createdAt;
    impl_->sessions[s.id] = s;
    return s;
}

Session SessionService::get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->sessions.find(id);
    if (it == impl_->sessions.end()) return Session{};
    return it->second;
}

std::vector<Session> SessionService::list() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Session> result;
    result.reserve(impl_->sessions.size());
    for (const auto& kv : impl_->sessions) {
        result.push_back(kv.second);
    }
    return result;
}

bool SessionService::save(const Session& session) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->sessions.find(session.id);
    if (it == impl_->sessions.end()) return false;
    it->second = session;
    it->second.updatedAt = nowTimestamp();
    return true;
}

bool SessionService::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->sessions.erase(id) > 0;
}

void SessionService::addMessage(const std::string& sessionId, const SessionMessage& msg) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    SessionMessage m = msg;
    if (m.id.empty()) {
        m.id = generateId();
    }
    m.sessionId = sessionId;
    if (m.createdAt == 0) {
        m.createdAt = nowTimestamp();
    }
    impl_->messages[sessionId].push_back(m);

    auto sit = impl_->sessions.find(sessionId);
    if (sit != impl_->sessions.end()) {
        sit->second.messageCount = static_cast<int64_t>(impl_->messages[sessionId].size());
        sit->second.updatedAt = nowTimestamp();
    }
}

std::vector<SessionMessage> SessionService::getMessages(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->messages.find(sessionId);
    if (it == impl_->messages.end()) return {};
    return it->second;
}

std::string SessionService::createAgentToolSessionId(const std::string& messageId,
                                                       const std::string& toolCallId) {
    return messageId + "$$" + toolCallId;
}

bool SessionService::parseAgentToolSessionId(const std::string& sessionId,
                                               std::string& messageId,
                                               std::string& toolCallId) {
    auto pos = sessionId.find("$$");
    if (pos == std::string::npos) return false;
    messageId = sessionId.substr(0, pos);
    toolCallId = sessionId.substr(pos + 2);
    return !messageId.empty() && !toolCallId.empty();
}

bool SessionService::isAgentToolSession(const std::string& sessionId) {
    return sessionId.find("$$") != std::string::npos;
}

}
}
