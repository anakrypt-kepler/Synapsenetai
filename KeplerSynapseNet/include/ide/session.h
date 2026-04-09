#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace synapse {
namespace ide {

enum class TodoStatus {
    Pending,
    InProgress,
    Completed
};

struct Todo {
    std::string content;
    TodoStatus status = TodoStatus::Pending;
    std::string activeForm;
};

struct Session {
    std::string id;
    std::string parentSessionId;
    std::string title;
    int64_t messageCount = 0;
    int64_t promptTokens = 0;
    int64_t completionTokens = 0;
    std::string summaryMessageId;
    double cost = 0.0;
    std::vector<Todo> todos;
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
};

struct SessionMessage {
    std::string id;
    std::string sessionId;
    std::string role;
    std::string content;
    int64_t createdAt = 0;
};

class SessionService {
public:
    SessionService();
    ~SessionService();

    Session create(const std::string& title);
    Session createThread(const std::string& parentSessionId, const std::string& title);
    Session get(const std::string& id) const;
    std::vector<Session> list() const;
    bool save(const Session& session);
    bool remove(const std::string& id);

    void addMessage(const std::string& sessionId, const SessionMessage& msg);
    std::vector<SessionMessage> getMessages(const std::string& sessionId) const;

    static std::string createAgentToolSessionId(const std::string& messageId,
                                                 const std::string& toolCallId);
    static bool parseAgentToolSessionId(const std::string& sessionId,
                                         std::string& messageId,
                                         std::string& toolCallId);
    static bool isAgentToolSession(const std::string& sessionId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
