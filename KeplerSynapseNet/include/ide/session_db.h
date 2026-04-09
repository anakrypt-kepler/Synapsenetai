#pragma once

#include "ide/session.h"

#include <memory>
#include <string>
#include <vector>

namespace synapse {
namespace ide {

class SessionDB {
public:
    SessionDB();
    ~SessionDB();

    bool open(const std::string& path);
    void close();
    bool isOpen() const;

    bool createSession(const Session& session);
    bool updateSession(const Session& session);
    bool deleteSession(const std::string& id);
    Session getSession(const std::string& id) const;
    std::vector<Session> listSessions() const;

    bool addMessage(const SessionMessage& msg);
    std::vector<SessionMessage> getMessages(const std::string& sessionId) const;
    int64_t messageCount(const std::string& sessionId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
