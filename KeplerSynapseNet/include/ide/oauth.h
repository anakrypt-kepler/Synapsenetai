#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace synapse {
namespace ide {

struct OAuthToken {
    std::string accessToken;
    std::string refreshToken;
    int expiresIn = 0;
    int64_t expiresAt = 0;

    void setExpiresAt();
    bool isExpired() const;
    void setExpiresIn();

    std::string toJson() const;
    static OAuthToken fromJson(const std::string& json);
};

struct DeviceCode {
    std::string deviceCode;
    std::string userCode;
    std::string verificationUri;
    int expiresIn = 0;
    int interval = 5;
};

struct OAuthProviderConfig {
    std::string clientId;
    std::string deviceCodeUrl;
    std::string accessTokenUrl;
    std::string scope;
};

class OAuthManager {
public:
    OAuthManager();
    ~OAuthManager();

    OAuthToken refreshToken(const std::string& providerId,
                            const OAuthToken& currentToken);

    DeviceCode requestDeviceCode(const OAuthProviderConfig& config);
    OAuthToken pollForToken(const DeviceCode& code,
                            const OAuthProviderConfig& config);

    bool saveToken(const std::string& providerId, const OAuthToken& token,
                   const std::string& path);
    OAuthToken loadToken(const std::string& providerId,
                         const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
