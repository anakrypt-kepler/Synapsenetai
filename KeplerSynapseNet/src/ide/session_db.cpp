#include "ide/session_db.h"

#include <mutex>
#include <unordered_map>
#include <vector>

#if SYNAPSE_HAVE_SQLITE3
#include <sqlite3.h>
#endif

namespace synapse {
namespace ide {

struct SessionDB::Impl {
    mutable std::mutex mtx;
    bool isOpen = false;
    std::string dbPath;

#if SYNAPSE_HAVE_SQLITE3
    sqlite3* db = nullptr;
#endif

    std::unordered_map<std::string, Session> sessions;
    std::unordered_map<std::string, std::vector<SessionMessage>> messages;
};

SessionDB::SessionDB() : impl_(std::make_unique<Impl>()) {}
SessionDB::~SessionDB() { close(); }

bool SessionDB::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->isOpen) close();
    impl_->dbPath = path;

#if SYNAPSE_HAVE_SQLITE3
    int rc = sqlite3_open(path.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        if (impl_->db) {
            sqlite3_close(impl_->db);
            impl_->db = nullptr;
        }
        return false;
    }

    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    const char* createSessions =
        "CREATE TABLE IF NOT EXISTS ide_sessions ("
        "id TEXT PRIMARY KEY,"
        "parent_session_id TEXT DEFAULT '',"
        "title TEXT DEFAULT '',"
        "message_count INTEGER DEFAULT 0,"
        "prompt_tokens INTEGER DEFAULT 0,"
        "completion_tokens INTEGER DEFAULT 0,"
        "summary_message_id TEXT DEFAULT '',"
        "cost REAL DEFAULT 0.0,"
        "todos TEXT DEFAULT '',"
        "created_at INTEGER DEFAULT 0,"
        "updated_at INTEGER DEFAULT 0"
        ");";

    const char* createMessages =
        "CREATE TABLE IF NOT EXISTS ide_messages ("
        "id TEXT PRIMARY KEY,"
        "session_id TEXT NOT NULL,"
        "role TEXT DEFAULT '',"
        "content TEXT DEFAULT '',"
        "created_at INTEGER DEFAULT 0"
        ");";

    char* errMsg = nullptr;
    rc = sqlite3_exec(impl_->db, createSessions, nullptr, nullptr, &errMsg);
    if (errMsg) sqlite3_free(errMsg);
    if (rc != SQLITE_OK) return false;

    errMsg = nullptr;
    rc = sqlite3_exec(impl_->db, createMessages, nullptr, nullptr, &errMsg);
    if (errMsg) sqlite3_free(errMsg);
    if (rc != SQLITE_OK) return false;

    impl_->isOpen = true;
    return true;
#else
    impl_->isOpen = true;
    return true;
#endif
}

void SessionDB::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
#endif
    impl_->isOpen = false;
    impl_->sessions.clear();
    impl_->messages.clear();
}

bool SessionDB::isOpen() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->isOpen;
}

bool SessionDB::createSession(const Session& session) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql =
            "INSERT INTO ide_sessions (id, parent_session_id, title, message_count, "
            "prompt_tokens, completion_tokens, summary_message_id, cost, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, session.id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, session.parentSessionId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, session.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, session.messageCount);
        sqlite3_bind_int64(stmt, 5, session.promptTokens);
        sqlite3_bind_int64(stmt, 6, session.completionTokens);
        sqlite3_bind_text(stmt, 7, session.summaryMessageId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 8, session.cost);
        sqlite3_bind_int64(stmt, 9, session.createdAt);
        sqlite3_bind_int64(stmt, 10, session.updatedAt);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
#endif

    impl_->sessions[session.id] = session;
    return true;
}

bool SessionDB::updateSession(const Session& session) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql =
            "UPDATE ide_sessions SET title=?, message_count=?, prompt_tokens=?, "
            "completion_tokens=?, summary_message_id=?, cost=?, updated_at=? WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, session.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, session.messageCount);
        sqlite3_bind_int64(stmt, 3, session.promptTokens);
        sqlite3_bind_int64(stmt, 4, session.completionTokens);
        sqlite3_bind_text(stmt, 5, session.summaryMessageId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 6, session.cost);
        sqlite3_bind_int64(stmt, 7, session.updatedAt);
        sqlite3_bind_text(stmt, 8, session.id.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
#endif

    auto it = impl_->sessions.find(session.id);
    if (it == impl_->sessions.end()) return false;
    it->second = session;
    return true;
}

bool SessionDB::deleteSession(const std::string& id) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql = "DELETE FROM ide_sessions WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        const char* sqlMsg = "DELETE FROM ide_messages WHERE session_id=?;";
        sqlite3_stmt* stmtMsg = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sqlMsg, -1, &stmtMsg, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmtMsg, 1, id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmtMsg);
            sqlite3_finalize(stmtMsg);
        }
        return rc == SQLITE_DONE;
    }
#endif

    impl_->sessions.erase(id);
    impl_->messages.erase(id);
    return true;
}

Session SessionDB::getSession(const std::string& id) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return Session{};

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql = "SELECT id, parent_session_id, title, message_count, prompt_tokens, "
                          "completion_tokens, summary_message_id, cost, created_at, updated_at "
                          "FROM ide_sessions WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return Session{};
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
        Session s;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            s.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            s.parentSessionId = parent ? parent : "";
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            s.title = title ? title : "";
            s.messageCount = sqlite3_column_int64(stmt, 3);
            s.promptTokens = sqlite3_column_int64(stmt, 4);
            s.completionTokens = sqlite3_column_int64(stmt, 5);
            const char* summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            s.summaryMessageId = summary ? summary : "";
            s.cost = sqlite3_column_double(stmt, 7);
            s.createdAt = sqlite3_column_int64(stmt, 8);
            s.updatedAt = sqlite3_column_int64(stmt, 9);
        }
        sqlite3_finalize(stmt);
        return s;
    }
#endif

    auto it = impl_->sessions.find(id);
    if (it == impl_->sessions.end()) return Session{};
    return it->second;
}

std::vector<Session> SessionDB::listSessions() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return {};

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql = "SELECT id, parent_session_id, title, message_count, prompt_tokens, "
                          "completion_tokens, summary_message_id, cost, created_at, updated_at "
                          "FROM ide_sessions ORDER BY created_at DESC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
        std::vector<Session> result;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Session s;
            s.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            s.parentSessionId = parent ? parent : "";
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            s.title = title ? title : "";
            s.messageCount = sqlite3_column_int64(stmt, 3);
            s.promptTokens = sqlite3_column_int64(stmt, 4);
            s.completionTokens = sqlite3_column_int64(stmt, 5);
            const char* summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            s.summaryMessageId = summary ? summary : "";
            s.cost = sqlite3_column_double(stmt, 7);
            s.createdAt = sqlite3_column_int64(stmt, 8);
            s.updatedAt = sqlite3_column_int64(stmt, 9);
            result.push_back(s);
        }
        sqlite3_finalize(stmt);
        return result;
    }
#endif

    std::vector<Session> result;
    result.reserve(impl_->sessions.size());
    for (const auto& kv : impl_->sessions) {
        result.push_back(kv.second);
    }
    return result;
}

bool SessionDB::addMessage(const SessionMessage& msg) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql =
            "INSERT INTO ide_messages (id, session_id, role, content, created_at) "
            "VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, msg.id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, msg.sessionId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, msg.role.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, msg.content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, msg.createdAt);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
#endif

    impl_->messages[msg.sessionId].push_back(msg);
    return true;
}

std::vector<SessionMessage> SessionDB::getMessages(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return {};

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql =
            "SELECT id, session_id, role, content, created_at "
            "FROM ide_messages WHERE session_id=? ORDER BY created_at ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_STATIC);
        std::vector<SessionMessage> result;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SessionMessage m;
            m.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            m.sessionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            m.role = role ? role : "";
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            m.content = content ? content : "";
            m.createdAt = sqlite3_column_int64(stmt, 4);
            result.push_back(m);
        }
        sqlite3_finalize(stmt);
        return result;
    }
#endif

    auto it = impl_->messages.find(sessionId);
    if (it == impl_->messages.end()) return {};
    return it->second;
}

int64_t SessionDB::messageCount(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return 0;

#if SYNAPSE_HAVE_SQLITE3
    if (impl_->db) {
        const char* sql = "SELECT COUNT(*) FROM ide_messages WHERE session_id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_STATIC);
        int64_t count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return count;
    }
#endif

    auto it = impl_->messages.find(sessionId);
    if (it == impl_->messages.end()) return 0;
    return static_cast<int64_t>(it->second.size());
}

}
}
