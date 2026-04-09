#pragma once

#include <memory>
#include <optional>
#include <string>

namespace synapse::utils {

struct SingleInstanceOptions {
    bool allowTerminateExisting = true;
    int terminateTimeoutMs = 15000;
};

class SingleInstanceLock {
public:
    static std::unique_ptr<SingleInstanceLock> acquire(
        const std::string& dataDir,
        std::string* errorOut = nullptr,
        const SingleInstanceOptions& options = {});

    ~SingleInstanceLock();

    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

private:
    SingleInstanceLock() = default;

    std::string lockPath_;

#ifdef _WIN32
    void* mutex_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace synapse::utils
