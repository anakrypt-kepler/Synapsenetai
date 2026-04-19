#include "quantum/identity_registry.h"
#include "quantum/application_signature.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace synapse::quantum {

namespace {

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

bool isHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool looksLikeValidIdentity(const std::string& id) {
    size_t colon = id.find(':');
    if (colon == std::string::npos) return false;
    if (colon == 0 || colon == id.size() - 1) return false;
    for (size_t i = 0; i < id.size(); ++i) {
        if (i == colon) continue;
        if (!isHexChar(id[i])) return false;
    }
    return true;
}

}

IdentityRegistry::IdentityRegistry() = default;

IdentityRegistry& IdentityRegistry::instance() {
    static IdentityRegistry registry;
    return registry;
}

void IdentityRegistry::setStoragePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mtx_);
    storagePath_ = path;
    loaded_ = false;
    bindings_.clear();
}

bool IdentityRegistry::registerIdentity(const std::string& address, const std::string& identityId) {
    if (address.empty() || !looksLikeValidIdentity(identityId)) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    if (!loaded_) loadLocked();
    auto it = bindings_.find(address);
    if (it != bindings_.end()) {
        return it->second == identityId;
    }
    bindings_[address] = identityId;
    return persistLocked();
}

std::string IdentityRegistry::lookupIdentity(const std::string& address) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!loaded_) loadLocked();
    auto it = bindings_.find(address);
    return it == bindings_.end() ? std::string() : it->second;
}

bool IdentityRegistry::hasBinding(const std::string& address) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!loaded_) loadLocked();
    return bindings_.find(address) != bindings_.end();
}

bool IdentityRegistry::verifyBinding(const std::string& address,
                                     const std::vector<uint8_t>& envelopeBytes) const {
    if (envelopeBytes.empty()) return false;
    std::string envelopeId;
    if (!applicationSignatureIdentityId(envelopeBytes, envelopeId)) return false;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!loaded_) loadLocked();
    auto it = bindings_.find(address);
    if (it == bindings_.end()) {
        bindings_[address] = envelopeId;
        persistLocked();
        return true;
    }
    return it->second == envelopeId;
}

void IdentityRegistry::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    bindings_.clear();
    loaded_ = true;
    persistLocked();
}

bool IdentityRegistry::loadLocked() const {
    loaded_ = true;
    if (storagePath_.empty()) return true;
    std::ifstream in(storagePath_);
    if (!in) return true;
    std::stringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    size_t i = 0;
    auto skipSpace = [&]() {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
    };
    skipSpace();
    if (i >= text.size() || text[i] != '{') return false;
    ++i;
    while (i < text.size()) {
        skipSpace();
        if (i < text.size() && text[i] == '}') { ++i; break; }
        if (i >= text.size() || text[i] != '"') return false;
        ++i;
        size_t keyStart = i;
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) i += 2;
            else ++i;
        }
        if (i >= text.size()) return false;
        std::string key = text.substr(keyStart, i - keyStart);
        ++i;
        skipSpace();
        if (i >= text.size() || text[i] != ':') return false;
        ++i;
        skipSpace();
        if (i >= text.size() || text[i] != '"') return false;
        ++i;
        size_t valStart = i;
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) i += 2;
            else ++i;
        }
        if (i >= text.size()) return false;
        std::string value = text.substr(valStart, i - valStart);
        ++i;
        if (!key.empty() && looksLikeValidIdentity(value)) {
            bindings_.emplace(std::move(key), std::move(value));
        }
        skipSpace();
        if (i < text.size() && text[i] == ',') { ++i; continue; }
        if (i < text.size() && text[i] == '}') { ++i; break; }
        return false;
    }
    return true;
}

bool IdentityRegistry::persistLocked() const {
    if (storagePath_.empty()) return true;
    try {
        std::filesystem::path p(storagePath_);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    } catch (...) {
        return false;
    }
    std::ofstream out(storagePath_, std::ios::trunc);
    if (!out) return false;
    out << "{";
    bool first = true;
    for (const auto& [address, id] : bindings_) {
        if (!first) out << ",";
        out << "\"" << escapeJson(address) << "\":\"" << escapeJson(id) << "\"";
        first = false;
    }
    out << "}";
    return out.good();
}

}
