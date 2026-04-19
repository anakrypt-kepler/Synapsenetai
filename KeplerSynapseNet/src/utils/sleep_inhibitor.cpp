#include "utils/sleep_inhibitor.h"

#if defined(__APPLE__)
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace synapse::utils {

SleepInhibitor::~SleepInhibitor() { release(); }

void SleepInhibitor::acquire(const std::string& reason) {
    (void)reason;
    release();

#if defined(__APPLE__)
    pid_t pid = -1;
    const char* argv[] = {"caffeinate", "-dimsu", nullptr};
    int rc = posix_spawnp(&pid, "caffeinate", nullptr, nullptr,
                          const_cast<char* const*>(argv), environ);
    if (rc == 0 && pid > 0) pid_ = static_cast<int>(pid);
#elif defined(_WIN32)
    previousState_ = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    active_ = true;
#else
    active_ = true;
#endif
}

void SleepInhibitor::release() {
#if defined(__APPLE__)
    if (pid_ > 0) {
        ::kill(static_cast<pid_t>(pid_), SIGTERM);
        int status = 0;
        ::waitpid(static_cast<pid_t>(pid_), &status, 0);
        pid_ = -1;
    }
#elif defined(_WIN32)
    if (active_) {
        SetThreadExecutionState(ES_CONTINUOUS);
        active_ = false;
    }
#else
    active_ = false;
#endif
}

} // namespace synapse::utils

