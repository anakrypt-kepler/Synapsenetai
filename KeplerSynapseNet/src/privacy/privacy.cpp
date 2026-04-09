#include "privacy/privacy.h"
#include "crypto/crypto.h"
#include "utils/logger.h"
#include <mutex>
#include <random>
#include <ctime>

namespace synapse {
namespace privacy {

struct Privacy::Impl {
    mutable std::mutex mtx;
    std::mt19937_64 rng;
    PrivacyConfig config;
    PrivacyMode mode = PrivacyMode::NONE;
    bool enabled = false;
    Socks5Proxy socks5;
    OnionService onion;
    
    Impl() : rng(std::random_device{}()) {}
};

Privacy::Privacy() : impl_(std::make_unique<Impl>()) {}
Privacy::~Privacy() { shutdown(); }

bool Privacy::init(const PrivacyConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
    
    if (config.useTor) {
        impl_->socks5.setProxy(config.torSocksHost, config.torSocksPort);
        impl_->onion.setControlConfig(config.torControlHost, config.torControlPort,
                                      config.torControlPassword, config.torControlCookiePath);
        if (!config.onionServiceDir.empty()) {
            impl_->onion.init(config.onionServiceDir);
        }
    }
    
    return true;
}

void Privacy::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->enabled = false;
    impl_->onion.stop();
    impl_->socks5.disconnect();
}

bool Privacy::enable(PrivacyMode mode) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->mode = mode;
    impl_->enabled = true;
    
    if (mode >= PrivacyMode::BASIC && impl_->config.useTor) {
        if (!impl_->socks5.connect(impl_->config.torSocksHost, impl_->config.torSocksPort)) {
            utils::Logger::warn(
                "Privacy enable failed: could not connect to Tor SOCKS at " +
                impl_->config.torSocksHost + ":" + std::to_string(impl_->config.torSocksPort));
            return false;
        }
    }
    
    if (mode >= PrivacyMode::FULL && impl_->config.useTor && !impl_->config.onionServiceDir.empty()) {
        impl_->onion.setRotationInterval(impl_->config.rotationInterval);
        impl_->onion.enableAutoRotation(impl_->config.rotateIdentity);
        if (!impl_->onion.start(impl_->config.onionVirtualPort, impl_->config.onionTargetPort)) {
            utils::Logger::warn(
                "Privacy enable failed: could not start onion service on " +
                std::to_string(impl_->config.onionVirtualPort) + " -> 127.0.0.1:" +
                std::to_string(impl_->config.onionTargetPort));
            return false;
        }
    }
    
    return true;
}

void Privacy::disable() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->enabled = false;
    impl_->mode = PrivacyMode::NONE;
}

bool Privacy::isEnabled() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->enabled;
}

PrivacyMode Privacy::getMode() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->mode;
}

std::string Privacy::getOnionAddress() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->onion.getHostname();
}

bool Privacy::rotateIdentity() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->onion.rotateIdentity();
}

bool Privacy::connectThroughTor(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->socks5.isConnected()) {
        if (!impl_->socks5.connect(impl_->config.torSocksHost, impl_->config.torSocksPort)) {
            return false;
        }
    }
    
    return impl_->socks5.connectToTarget(host, port);
}

std::vector<uint8_t> Privacy::sendThroughTor(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->socks5.sendData(data);
}

void Privacy::setConfig(const PrivacyConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
}

PrivacyConfig Privacy::getConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->config;
}

}
}
