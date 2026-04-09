#pragma once

#include <string>

namespace synapse::utils {

class SleepInhibitor {
public:
    SleepInhibitor() = default;
    ~SleepInhibitor();

    SleepInhibitor(const SleepInhibitor&) = delete;
    SleepInhibitor& operator=(const SleepInhibitor&) = delete;

    void acquire(const std::string& reason);
    void release();

private:
#if defined(__APPLE__)
    int pid_ = -1;
#elif defined(_WIN32)
    unsigned long previousState_ = 0;
    bool active_ = false;
#else
    bool active_ = false;
#endif
};

} // namespace synapse::utils

