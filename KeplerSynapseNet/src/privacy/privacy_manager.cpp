#include "privacy/privacy.h"
#include <mutex>

namespace synapse {
namespace privacy {

struct PrivacyManager::Impl {
    bool initialized = false;
    bool privacyMode = false;
    mutable std::mutex mtx;
    
    Socks5Proxy socks5_;
    OnionService onion_;
    SessionCrypto sessionCrypto_;
    StealthAddress stealth_;
    Dandelion dandelion_;
    MixInference mixInference_;
    Amnesia amnesia_;
    HiddenVolume hiddenVolume_;
    DecoyTraffic decoyTraffic_;
};

PrivacyManager::PrivacyManager() : impl_(std::make_unique<Impl>()) {}
PrivacyManager::~PrivacyManager() { shutdown(); }

bool PrivacyManager::init() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (impl_->initialized) return true;
    
    impl_->initialized = true;
    return true;
}

void PrivacyManager::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (!impl_->initialized) return;
    
    impl_->decoyTraffic_.stop();
    impl_->mixInference_.stop();
    impl_->onion_.stop();
    impl_->socks5_.disconnect();
    impl_->sessionCrypto_.clearAllSessions();
    impl_->amnesia_.disable();
    impl_->hiddenVolume_.unmount();
    
    impl_->initialized = false;
}

Socks5Proxy& PrivacyManager::socks5() {
    return impl_->socks5_;
}

OnionService& PrivacyManager::onion() {
    return impl_->onion_;
}

SessionCrypto& PrivacyManager::sessionCrypto() {
    return impl_->sessionCrypto_;
}

StealthAddress& PrivacyManager::stealth() {
    return impl_->stealth_;
}

Dandelion& PrivacyManager::dandelion() {
    return impl_->dandelion_;
}

MixInference& PrivacyManager::mixInference() {
    return impl_->mixInference_;
}

Amnesia& PrivacyManager::amnesia() {
    return impl_->amnesia_;
}

HiddenVolume& PrivacyManager::hiddenVolume() {
    return impl_->hiddenVolume_;
}

DecoyTraffic& PrivacyManager::decoyTraffic() {
    return impl_->decoyTraffic_;
}

void PrivacyManager::enablePrivacyMode(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->privacyMode = enable;
    
    if (enable) {
        impl_->amnesia_.enable();
        impl_->decoyTraffic_.start();
    } else {
        impl_->decoyTraffic_.stop();
        impl_->amnesia_.disable();
    }
}

bool PrivacyManager::isPrivacyModeEnabled() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->privacyMode;
}

}
}
