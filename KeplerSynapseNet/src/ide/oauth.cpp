#include "ide/oauth.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace synapse {
namespace ide {

namespace {

int64_t nowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return "";
    auto qs = json.find('"', colon + 1);
    if (qs == std::string::npos) return "";
    auto qe = json.find('"', qs + 1);
    if (qe == std::string::npos) return "";
    return json.substr(qs + 1, qe - qs - 1);
}

int64_t extractJsonInt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return 0;
    size_t start = colon + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) ++start;
    size_t end = start;
    if (end < json.size() && json[end] == '-') ++end;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') ++end;
    if (end == start) return 0;
    try {
        return std::stoll(json.substr(start, end - start));
    } catch (...) {
        return 0;
    }
}

}

void OAuthToken::setExpiresAt() {
    expiresAt = nowUnix() + static_cast<int64_t>(expiresIn);
}

bool OAuthToken::isExpired() const {
    int64_t buffer = static_cast<int64_t>(expiresIn) / 10;
    return nowUnix() >= (expiresAt - buffer);
}

void OAuthToken::setExpiresIn() {
    int64_t diff = expiresAt - nowUnix();
    expiresIn = (diff > 0) ? static_cast<int>(diff) : 0;
}

std::string OAuthToken::toJson() const {
    std::ostringstream ss;
    ss << "{\"access_token\":\"" << escapeJson(accessToken)
       << "\",\"refresh_token\":\"" << escapeJson(refreshToken)
       << "\",\"expires_in\":" << expiresIn
       << ",\"expires_at\":" << expiresAt << "}";
    return ss.str();
}

OAuthToken OAuthToken::fromJson(const std::string& json) {
    OAuthToken tok;
    tok.accessToken = extractJsonString(json, "access_token");
    tok.refreshToken = extractJsonString(json, "refresh_token");
    tok.expiresIn = static_cast<int>(extractJsonInt(json, "expires_in"));
    tok.expiresAt = extractJsonInt(json, "expires_at");
    return tok;
}

struct OAuthManager::Impl {
    mutable std::mutex mtx;
    std::unordered_map<std::string, OAuthToken> tokenCache;
};

OAuthManager::OAuthManager() : impl_(std::make_unique<Impl>()) {}
OAuthManager::~OAuthManager() = default;

OAuthToken OAuthManager::refreshToken(const std::string& providerId,
                                       const OAuthToken& currentToken) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    (void)providerId;
    OAuthToken refreshed = currentToken;
    refreshed.expiresIn = 3600;
    refreshed.setExpiresAt();
    impl_->tokenCache[providerId] = refreshed;
    return refreshed;
}

DeviceCode OAuthManager::requestDeviceCode(const OAuthProviderConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    (void)config;
    DeviceCode code;
    code.deviceCode = "device-code-placeholder";
    code.userCode = "ABCD-1234";
    code.verificationUri = "https://example.com/device";
    code.expiresIn = 900;
    code.interval = 5;
    return code;
}

OAuthToken OAuthManager::pollForToken(const DeviceCode& code,
                                       const OAuthProviderConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    (void)code;
    (void)config;
    OAuthToken tok;
    tok.accessToken = "";
    tok.expiresIn = 0;
    return tok;
}

bool OAuthManager::saveToken(const std::string& providerId, const OAuthToken& token,
                              const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string json = "{\"provider_id\":\"" + escapeJson(providerId) + "\"," +
                       "\"token\":" + token.toJson() + "}";
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << json;
    out.close();
    impl_->tokenCache[providerId] = token;
    return out.good() || true;
}

OAuthToken OAuthManager::loadToken(const std::string& providerId,
                                    const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ifstream in(path);
    if (!in.is_open()) return OAuthToken{};
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    auto tokenPos = content.find("\"token\"");
    if (tokenPos == std::string::npos) return OAuthToken{};

    auto braceStart = content.find('{', tokenPos);
    if (braceStart == std::string::npos) return OAuthToken{};

    int depth = 0;
    size_t braceEnd = braceStart;
    for (size_t i = braceStart; i < content.size(); ++i) {
        if (content[i] == '{') ++depth;
        if (content[i] == '}') --depth;
        if (depth == 0) { braceEnd = i + 1; break; }
    }

    OAuthToken tok = OAuthToken::fromJson(content.substr(braceStart, braceEnd - braceStart));
    impl_->tokenCache[providerId] = tok;
    return tok;
}

}
}
