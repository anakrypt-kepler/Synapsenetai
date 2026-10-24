// Local KV/SQLite wrapper for wallet/chain metadata. If SYNAPSE_HAVE_SQLITE3
// is off, some paths no-op — do not assume every build has a real DB.

#include "database/database.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>

#if SYNAPSE_HAVE_SQLITE3
#include <sqlite3.h>
#endif

namespace synapse {
namespace database {

struct WriteBatch::Impl {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> puts;
    std::vector<std::string> dels;
};

WriteBatch::WriteBatch() : impl_(std::make_unique<Impl>()) {}
WriteBatch::~WriteBatch() = default;

void WriteBatch::put(const std::string& key, const std::vector<uint8_t>& value) {
    impl_->puts.emplace_back(key, value);
}

void WriteBatch::del(const std::string& key) {
    impl_->dels.push_back(key);
}

void WriteBatch::clear() {
    impl_->puts.clear();
    impl_->dels.clear();
}

#if SYNAPSE_HAVE_SQLITE3
struct Iterator::Impl {
    sqlite3_stmt* stmt = nullptr;
    bool valid_ = false;
    std::string currentKey;
    std::vector<uint8_t> currentValue;
};

Iterator::Iterator() : impl_(std::make_unique<Impl>()) {}
Iterator::~Iterator() {
    if (impl_->stmt) sqlite3_finalize(impl_->stmt);
}

void Iterator::seekToFirst() {
    if (impl_->stmt) {
        sqlite3_reset(impl_->stmt);
        next();
    }
}

void Iterator::seekToLast() {
    impl_->valid_ = false;
    if (!impl_->stmt) return;
    sqlite3* db = sqlite3_db_handle(impl_->stmt);
    if (!db) return;

    sqlite3_stmt* s = nullptr;
    const char* sql = "SELECT key, value FROM kv ORDER BY key DESC LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return;
    int rc = sqlite3_step(s);
    if (rc == SQLITE_ROW) {
        impl_->currentKey = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        const void* blob = sqlite3_column_blob(s, 1);
        int blobSize = sqlite3_column_bytes(s, 1);
        impl_->currentValue.assign(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + blobSize);
        impl_->valid_ = true;
    } else {
        impl_->valid_ = false;
    }
    sqlite3_finalize(s);
}

void Iterator::seek(const std::string& key) {
    impl_->valid_ = false;
    if (!impl_->stmt) return;
    sqlite3* db = sqlite3_db_handle(impl_->stmt);
    if (!db) return;

    sqlite3_stmt* s = nullptr;
    const char* sql = "SELECT key, value FROM kv WHERE key >= ? ORDER BY key LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(s, 1, key.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(s);
    if (rc == SQLITE_ROW) {
        impl_->currentKey = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        const void* blob = sqlite3_column_blob(s, 1);
        int blobSize = sqlite3_column_bytes(s, 1);
        impl_->currentValue.assign(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + blobSize);
        impl_->valid_ = true;
    } else {
        impl_->valid_ = false;
    }
    sqlite3_finalize(s);
}

bool Iterator::valid() const { return impl_->valid_; }

void Iterator::next() {
    if (!impl_->stmt) { impl_->valid_ = false; return; }
    int rc = sqlite3_step(impl_->stmt);
    if (rc == SQLITE_ROW) {
        impl_->currentKey = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt, 0));
        const void* blob = sqlite3_column_blob(impl_->stmt, 1);
        int blobSize = sqlite3_column_bytes(impl_->stmt, 1);
        impl_->currentValue.assign(static_cast<const uint8_t*>(blob), 
                                   static_cast<const uint8_t*>(blob) + blobSize);
        impl_->valid_ = true;
    } else {
        impl_->valid_ = false;
    }
}

void Iterator::prev() {
    if (!impl_->valid_) return;
    if (!impl_->stmt) { impl_->valid_ = false; return; }
    sqlite3* db = sqlite3_db_handle(impl_->stmt);
    if (!db) { impl_->valid_ = false; return; }

    sqlite3_stmt* s = nullptr;
    const char* sql = "SELECT key, value FROM kv WHERE key < ? ORDER BY key DESC LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) { impl_->valid_ = false; return; }
    sqlite3_bind_text(s, 1, impl_->currentKey.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(s);
    if (rc == SQLITE_ROW) {
        impl_->currentKey = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        const void* blob = sqlite3_column_blob(s, 1);
        int blobSize = sqlite3_column_bytes(s, 1);
        impl_->currentValue.assign(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + blobSize);
        impl_->valid_ = true;
    } else {
        impl_->valid_ = false;
    }
    sqlite3_finalize(s);
}
std::string Iterator::key() const { return impl_->currentKey; }
std::vector<uint8_t> Iterator::value() const { return impl_->currentValue; }

namespace {

// A connection may not load an extension or follow a caller-supplied name to
// another file. URI filenames stay disabled, ATTACH is refused, and a second
// database file has to stay in the same directory as the open database.
bool sqlitePathRejected(const std::string& path) {
    if (path.empty()) return true;
    if (path == ":memory:") return false;
    if (path.find('\0') != std::string::npos) return true;
    if (path.find('?') != std::string::npos ||
        path.find('#') != std::string::npos ||
        path.find('%') != std::string::npos) {
        return true;
    }
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    if (path.size() >= 2 &&
        ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/'))) {
        return true;
    }

    std::string lower(path.size(), '\0');
    for (size_t i = 0; i < path.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        lower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    }
    if (lower.compare(0, 5, "file:") == 0) return true;
    if (lower.find("://") != std::string::npos) return true;

    const std::filesystem::path fsPath(path);
    for (const auto& part : fsPath) {
        if (part == "..") return true;
    }
    for (const auto& part : fsPath.lexically_normal()) {
        if (part == "..") return true;
    }
    const auto filename = fsPath.filename();
    return filename.empty() || filename == "." || filename == "..";
}

#ifndef SQLITE_OMIT_AUTHORIZATION
bool nameIsLoadExtension(const char* name) {
    static constexpr char kExpected[] = "load_extension";
    if (!name) return false;
    for (size_t i = 0; kExpected[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == 0) return false;
        const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        if (lower != kExpected[i]) return false;
    }
    return name[sizeof(kExpected) - 1] == '\0';
}

extern "C" int synapseDbSqliteDenyForeignFile(
    void*,
    int action,
    const char* arg1,
    const char* arg2,
    const char*,
    const char*) {
    if (action == SQLITE_ATTACH || action == SQLITE_DETACH) return SQLITE_DENY;
    if (action == SQLITE_FUNCTION &&
        (nameIsLoadExtension(arg1) || nameIsLoadExtension(arg2))) {
        return SQLITE_DENY;
    }
    return SQLITE_OK;
}
#endif

bool lockDownSqlite(sqlite3* db) {
    if (!db) return false;
#ifndef SQLITE_OMIT_LOAD_EXTENSION
    if (sqlite3_enable_load_extension(db, 0) != SQLITE_OK) return false;
#if SQLITE_VERSION_NUMBER >= 3013000
    {
        int prior = 0;
        if (sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, &prior) != SQLITE_OK) {
            return false;
        }
    }
#endif
#endif
#if SQLITE_VERSION_NUMBER >= 3026000
    {
        int prior = 0;
        if (sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, &prior) != SQLITE_OK) return false;
    }
#endif
#if SQLITE_VERSION_NUMBER >= 3031000
    {
        int prior = 0;
        if (sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &prior) != SQLITE_OK) return false;
    }
#endif
    sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
#ifndef SQLITE_OMIT_AUTHORIZATION
    if (sqlite3_set_authorizer(db, synapseDbSqliteDenyForeignFile, nullptr) != SQLITE_OK) return false;
#endif
    return true;
}

std::filesystem::path canonicalDirectory(const std::filesystem::path& dir, std::error_code& ec) {
    ec.clear();
    std::filesystem::path parent = dir;
    if (parent.empty()) parent = std::filesystem::current_path(ec);
    if (ec) return {};
    const auto canonical = std::filesystem::weakly_canonical(parent, ec);
    if (ec || canonical.empty()) return {};
    return canonical;
}

bool sameCanonicalDirectory(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code ec;
    const auto leftDir = canonicalDirectory(left, ec);
    if (ec || leftDir.empty()) return false;
    const auto rightDir = canonicalDirectory(right, ec);
    if (ec || rightDir.empty()) return false;
    return leftDir == rightDir;
}

bool openedDatabaseStaysPut(sqlite3* db, const std::string& requested) {
    const char* actual = sqlite3_db_filename(db, "main");
    if (requested == ":memory:") return actual == nullptr || actual[0] == '\0';
    if (!actual || actual[0] == '\0') return false;
    if (std::string(actual) == requested) return true;
    if (sqlitePathRejected(actual)) return false;
    return sameCanonicalDirectory(
        std::filesystem::path(requested).parent_path(),
        std::filesystem::path(actual).parent_path());
}

bool openContainedSqlite(const std::string& path, sqlite3** out) {
    if (!out) return false;
    *out = nullptr;
    if (sqlitePathRejected(path)) return false;
    if (path != ":memory:") {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        if (!ec && std::filesystem::is_symlink(status)) return false;
        if (ec && ec != std::errc::no_such_file_or_directory) return false;
    }
    sqlite3* db = nullptr;
    // SQLITE_OPEN_URI is omitted so a file: name cannot select another file.
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    const int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
    if (rc != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return false;
    }
    if (!lockDownSqlite(db) || !openedDatabaseStaysPut(db, path)) {
        sqlite3_close(db);
        return false;
    }
    *out = db;
    return true;
}

bool sqliteSiblingPathAllowed(const std::string& openPath, const std::string& extraPath) {
    if (openPath == ":memory:" || extraPath == ":memory:") return false;
    if (sqlitePathRejected(openPath) || sqlitePathRejected(extraPath)) return false;
    const std::filesystem::path opened(openPath);
    const std::filesystem::path extra(extraPath);
    if (opened.filename().empty() || extra.filename().empty()) return false;
    return sameCanonicalDirectory(opened.parent_path(), extra.parent_path());
}

}  // namespace

struct Database::Impl {
    sqlite3* db = nullptr;
    std::string path;
    mutable std::mutex mtx;
    bool isOpen = false;
};

Database::Database() : impl_(std::make_unique<Impl>()) {}

Database::~Database() { close(); }

bool Database::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->isOpen) return false;
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
    if (!openContainedSqlite(path, &impl_->db)) return false;

    int rc = SQLITE_OK;
    
    const char* createTable = 
        "CREATE TABLE IF NOT EXISTS kv ("
        "key TEXT PRIMARY KEY,"
        "value BLOB"
        ");";
    
    char* errMsg = nullptr;
    rc = sqlite3_exec(impl_->db, createTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        sqlite3_free(errMsg);
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
        return false;
    }
    
    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    
    impl_->path = path;
    impl_->isOpen = true;
    return true;
}

void Database::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
    impl_->isOpen = false;
}

bool Database::isOpen() const {
    return impl_->isOpen;
}

bool Database::put(const std::string& key, const std::vector<uint8_t>& value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO kv (key, value) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, value.data(), value.size(), SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::put(const std::string& key, const std::string& value) {
    return put(key, std::vector<uint8_t>(value.begin(), value.end()));
}

std::vector<uint8_t> Database::get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return {};
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT value FROM kv WHERE key = ?;";
    
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    
    std::vector<uint8_t> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        result.assign(static_cast<const uint8_t*>(blob), 
                      static_cast<const uint8_t*>(blob) + blobSize);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::string Database::getString(const std::string& key) const {
    auto data = get(key);
    return std::string(data.begin(), data.end());
}

bool Database::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM kv WHERE key = ?;";
    
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::exists(const std::string& key) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT 1 FROM kv WHERE key = ? LIMIT 1;";
    
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    
    return found;
}

bool Database::write(WriteBatch& batch) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    
    sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    
    for (const auto& [key, value] : batch.impl_->puts) {
        sqlite3_stmt* stmt;
        const char* sql = "INSERT OR REPLACE INTO kv (key, value) VALUES (?, ?);";
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_blob(stmt, 2, value.data(), value.size(), SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    for (const auto& key : batch.impl_->dels) {
        sqlite3_stmt* stmt;
        const char* sql = "DELETE FROM kv WHERE key = ?;";
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, nullptr);
    batch.clear();
    return true;
}

std::unique_ptr<Iterator> Database::newIterator() const {
    auto iter = std::make_unique<Iterator>();
    
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->db) {
        const char* sql = "SELECT key, value FROM kv ORDER BY key;";
        sqlite3_prepare_v2(impl_->db, sql, -1, &iter->impl_->stmt, nullptr);
    }
    
    return iter;
}

// Bound prefix match. LIKE still treats % and _ as wildcards, so those
// characters in caller text are escaped and the trailing % stays the only wildcard.
static std::string likeLiteralPrefix(const std::string& prefix) {
    std::string out;
    out.reserve(prefix.size());
    for (char c : prefix) {
        if (c == '\\' || c == '%' || c == '_') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

void Database::forEach(const std::string& prefix, 
                       std::function<bool(const std::string&, const std::vector<uint8_t>&)> fn) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return;
    
    sqlite3_stmt* stmt;
    std::string sql = "SELECT key, value FROM kv";
    if (!prefix.empty()) {
        sql += " WHERE key LIKE ? || '%' ESCAPE '\\'";
    }
    sql += " ORDER BY key;";
    
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;
    
    const std::string boundPrefix = prefix.empty() ? std::string() : likeLiteralPrefix(prefix);
    if (!prefix.empty()) {
        sqlite3_bind_text(stmt, 1, boundPrefix.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const void* blob = sqlite3_column_blob(stmt, 1);
        int blobSize = sqlite3_column_bytes(stmt, 1);
        std::vector<uint8_t> value(static_cast<const uint8_t*>(blob),
                                   static_cast<const uint8_t*>(blob) + blobSize);
        if (!fn(key, value)) break;
    }
    
    sqlite3_finalize(stmt);
}

std::vector<std::string> Database::keys(const std::string& prefix) const {
    std::vector<std::string> result;
    forEach(prefix, [&result](const std::string& key, const std::vector<uint8_t>&) {
        result.push_back(key);
        return true;
    });
    return result;
}

size_t Database::count(const std::string& prefix) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return 0;
    
    sqlite3_stmt* stmt;
    std::string sql = "SELECT COUNT(*) FROM kv";
    if (!prefix.empty()) {
        sql += " WHERE key LIKE ? || '%' ESCAPE '\\'";
    }
    sql += ";";
    
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return 0;
    
    const std::string boundPrefix = prefix.empty() ? std::string() : likeLiteralPrefix(prefix);
    if (!prefix.empty()) {
        sqlite3_bind_text(stmt, 1, boundPrefix.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    size_t cnt = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        cnt = sqlite3_column_int64(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return cnt;
}

bool Database::compact() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    return sqlite3_exec(impl_->db, "VACUUM;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool Database::integrityCheck(std::string* details) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) {
        if (details) *details = "database_not_open";
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(impl_->db, "PRAGMA integrity_check;", -1, &stmt, nullptr) != SQLITE_OK) {
        if (details) *details = sqlite3_errmsg(impl_->db);
        return false;
    }

    bool ok = true;
    std::string report;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* row = sqlite3_column_text(stmt, 0);
        std::string line = row ? reinterpret_cast<const char*>(row) : "";
        if (line != "ok") {
            ok = false;
            if (!report.empty()) report += "; ";
            report += line;
        }
    }

    sqlite3_finalize(stmt);
    if (details) *details = ok ? "ok" : report;
    return ok;
}

std::string Database::getPath() const {
    return impl_->path;
}

uint64_t Database::size() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return 0;
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, "SELECT page_count * page_size FROM pragma_page_count(), pragma_page_size();", 
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    uint64_t sz = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sz = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return sz;
}

bool Database::backup(const std::string& destPath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    if (!sqliteSiblingPathAllowed(impl_->path, destPath)) return false;

    sqlite3* destDb = nullptr;
    if (!openContainedSqlite(destPath, &destDb)) return false;
    
    sqlite3_backup* bkp = sqlite3_backup_init(destDb, "main", impl_->db, "main");
    if (!bkp) {
        sqlite3_close(destDb);
        return false;
    }
    
    sqlite3_backup_step(bkp, -1);
    sqlite3_backup_finish(bkp);
    
    int rc = sqlite3_errcode(destDb);
    sqlite3_close(destDb);
    
    return rc == SQLITE_OK;
}

bool Database::restore(const std::string& srcPath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    if (!sqliteSiblingPathAllowed(impl_->path, srcPath)) return false;

    sqlite3* srcDb = nullptr;
    if (!openContainedSqlite(srcPath, &srcDb)) return false;
    
    sqlite3_backup* bkp = sqlite3_backup_init(impl_->db, "main", srcDb, "main");
    if (!bkp) {
        sqlite3_close(srcDb);
        return false;
    }
    
    sqlite3_backup_step(bkp, -1);
    sqlite3_backup_finish(bkp);
    
    int rc = sqlite3_errcode(impl_->db);
    sqlite3_close(srcDb);
    
    return rc == SQLITE_OK;
}

bool Database::beginTransaction() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    return sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool Database::commitTransaction() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    return sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool Database::rollbackTransaction() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    return sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::vector<std::pair<std::string, std::vector<uint8_t>>> Database::getRange(
    const std::string& startKey, const std::string& endKey, size_t limit) const {
    
    std::vector<std::pair<std::string, std::vector<uint8_t>>> results;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return results;
    
    sqlite3_stmt* stmt;
    std::string sql = "SELECT key, value FROM kv WHERE key >= ? AND key < ? ORDER BY key";
    if (limit > 0) {
        sql += " LIMIT " + std::to_string(limit);
    }
    sql += ";";
    
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    
    sqlite3_bind_text(stmt, 1, startKey.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, endKey.c_str(), -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const void* blob = sqlite3_column_blob(stmt, 1);
        int blobSize = sqlite3_column_bytes(stmt, 1);
        std::vector<uint8_t> value(static_cast<const uint8_t*>(blob),
                                    static_cast<const uint8_t*>(blob) + blobSize);
        results.emplace_back(key, value);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

bool Database::clear() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db) return false;
    return sqlite3_exec(impl_->db, "DELETE FROM kv;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

#else  // SYNAPSE_HAVE_SQLITE3

struct Iterator::Impl {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;
    size_t index = 0;
    bool valid_ = false;
};

Iterator::Iterator() : impl_(std::make_unique<Impl>()) {}
Iterator::~Iterator() = default;

void Iterator::seekToFirst() {
    if (impl_->entries.empty()) {
        impl_->valid_ = false;
        impl_->index = 0;
        return;
    }
    impl_->index = 0;
    impl_->valid_ = true;
}

void Iterator::seekToLast() {
    if (impl_->entries.empty()) {
        impl_->valid_ = false;
        impl_->index = 0;
        return;
    }
    impl_->index = impl_->entries.size() - 1;
    impl_->valid_ = true;
}

void Iterator::seek(const std::string& key) {
    auto it = std::lower_bound(
        impl_->entries.begin(),
        impl_->entries.end(),
        key,
        [](const auto& a, const std::string& k) { return a.first < k; }
    );
    if (it == impl_->entries.end()) {
        impl_->valid_ = false;
        impl_->index = impl_->entries.size();
        return;
    }
    impl_->index = static_cast<size_t>(it - impl_->entries.begin());
    impl_->valid_ = true;
}

bool Iterator::valid() const { return impl_->valid_; }

void Iterator::next() {
    if (!impl_->valid_) return;
    if (impl_->index + 1 >= impl_->entries.size()) {
        impl_->valid_ = false;
        impl_->index = impl_->entries.size();
        return;
    }
    impl_->index += 1;
    impl_->valid_ = true;
}

void Iterator::prev() {
    if (!impl_->valid_) return;
    if (impl_->index == 0 || impl_->entries.empty()) {
        impl_->valid_ = false;
        impl_->index = 0;
        return;
    }
    impl_->index -= 1;
    impl_->valid_ = true;
}

std::string Iterator::key() const {
    if (!impl_->valid_ || impl_->index >= impl_->entries.size()) return {};
    return impl_->entries[impl_->index].first;
}

std::vector<uint8_t> Iterator::value() const {
    if (!impl_->valid_ || impl_->index >= impl_->entries.size()) return {};
    return impl_->entries[impl_->index].second;
}

struct Database::Impl {
    std::map<std::string, std::vector<uint8_t>> kv;
    std::vector<std::map<std::string, std::vector<uint8_t>>> txStack;
    std::string path;
    mutable std::mutex mtx;
    bool isOpen = false;
};

Database::Database() : impl_(std::make_unique<Impl>()) {}

Database::~Database() { close(); }

bool Database::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->isOpen) return false;
    impl_->path = path;
    impl_->kv.clear();
    impl_->txStack.clear();
    impl_->isOpen = true;
    return true;
}

void Database::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->isOpen = false;
    impl_->txStack.clear();
}

bool Database::isOpen() const { return impl_->isOpen; }

bool Database::put(const std::string& key, const std::vector<uint8_t>& value) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;
    impl_->kv[key] = value;
    return true;
}

bool Database::put(const std::string& key, const std::string& value) {
    return put(key, std::vector<uint8_t>(value.begin(), value.end()));
}

std::vector<uint8_t> Database::get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return {};
    auto it = impl_->kv.find(key);
    if (it == impl_->kv.end()) return {};
    return it->second;
}

std::string Database::getString(const std::string& key) const {
    auto v = get(key);
    return std::string(v.begin(), v.end());
}

bool Database::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;
    return impl_->kv.erase(key) > 0;
}

bool Database::exists(const std::string& key) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;
    return impl_->kv.find(key) != impl_->kv.end();
}

bool Database::write(WriteBatch& batch) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;
    for (const auto& [key, value] : batch.impl_->puts) {
        impl_->kv[key] = value;
    }
    for (const auto& key : batch.impl_->dels) {
        impl_->kv.erase(key);
    }
    batch.clear();
    return true;
}

std::unique_ptr<Iterator> Database::newIterator() const {
    auto iter = std::make_unique<Iterator>();
    std::lock_guard<std::mutex> lock(impl_->mtx);
    iter->impl_->entries.reserve(impl_->kv.size());
    for (const auto& [k, v] : impl_->kv) {
        iter->impl_->entries.emplace_back(k, v);
    }
    iter->impl_->valid_ = false;
    iter->impl_->index = 0;
    return iter;
}

void Database::forEach(
    const std::string& prefix,
    std::function<bool(const std::string&, const std::vector<uint8_t>&)> fn
) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return;
    for (const auto& [k, v] : impl_->kv) {
        if (!prefix.empty() && k.rfind(prefix, 0) != 0) continue;
        if (!fn(k, v)) break;
    }
}

std::vector<std::string> Database::keys(const std::string& prefix) const {
    std::vector<std::string> out;
    forEach(prefix, [&out](const std::string& k, const std::vector<uint8_t>&) {
        out.push_back(k);
        return true;
    });
    return out;
}

size_t Database::count(const std::string& prefix) const {
    size_t cnt = 0;
    forEach(prefix, [&cnt](const std::string&, const std::vector<uint8_t>&) {
        ++cnt;
        return true;
    });
    return cnt;
}

bool Database::compact() { return true; }

bool Database::integrityCheck(std::string* details) const {
    if (details) *details = impl_->isOpen ? "ok" : "database_not_open";
    return impl_->isOpen;
}

std::string Database::getPath() const { return impl_->path; }

uint64_t Database::size() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t sz = 0;
    for (const auto& [k, v] : impl_->kv) {
        sz += static_cast<uint64_t>(k.size());
        sz += static_cast<uint64_t>(v.size());
    }
    return sz;
}

bool Database::backup(const std::string&) { return false; }
bool Database::restore(const std::string&) { return false; }

bool Database::beginTransaction() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;
    impl_->txStack.push_back(impl_->kv);
    return true;
}

bool Database::commitTransaction() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->txStack.empty()) return false;
    impl_->txStack.pop_back();
    return true;
}

bool Database::rollbackTransaction() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->txStack.empty()) return false;
    impl_->kv = std::move(impl_->txStack.back());
    impl_->txStack.pop_back();
    return true;
}

std::vector<std::pair<std::string, std::vector<uint8_t>>> Database::getRange(
    const std::string& startKey,
    const std::string& endKey,
    size_t limit
) const {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> out;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return out;
    auto it = impl_->kv.lower_bound(startKey);
    for (; it != impl_->kv.end() && it->first < endKey; ++it) {
        out.emplace_back(it->first, it->second);
        if (limit > 0 && out.size() >= limit) break;
    }
    return out;
}

bool Database::clear() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->isOpen) return false;
    impl_->kv.clear();
    return true;
}

#endif  // SYNAPSE_HAVE_SQLITE3

}
}
