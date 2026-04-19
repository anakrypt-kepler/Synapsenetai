#include "utils/single_instance.h"

#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <libproc.h>
#elif defined(__linux__)
#include <limits.h>
#endif
#endif

namespace synapse::utils {
namespace {

static std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

#ifdef _WIN32
static std::string getSelfExePath() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::string(buf, buf + n);
}

static std::optional<std::string> getProcessExePath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::nullopt;
    char buf[MAX_PATH];
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameA(h, 0, buf, &size);
    CloseHandle(h);
    if (!ok || size == 0) return std::nullopt;
    return std::string(buf, buf + size);
}
#else
static std::string getSelfExePath() {
#if defined(__APPLE__)
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    int n = proc_pidpath(getpid(), buf, sizeof(buf));
    if (n <= 0) return {};
    return std::string(buf);
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return std::string(buf);
#else
    return {};
#endif
}

static bool isProcessAlive(pid_t pid) {
    if (pid <= 0) return false;
    int rc = ::kill(pid, 0);
    if (rc == 0) return true;
    return errno == EPERM;
}

static std::optional<std::string> getProcessExePath(pid_t pid) {
#if defined(__APPLE__)
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    int n = proc_pidpath(pid, buf, sizeof(buf));
    if (n <= 0) return std::nullopt;
    return std::string(buf);
#elif defined(__linux__)
    char linkPath[64];
    std::snprintf(linkPath, sizeof(linkPath), "/proc/%d/exe", pid);
    char buf[PATH_MAX];
    ssize_t n = ::readlink(linkPath, buf, sizeof(buf) - 1);
    if (n <= 0) return std::nullopt;
    buf[n] = '\0';
    return std::string(buf);
#else
    (void)pid;
    return std::nullopt;
#endif
}

static std::optional<std::string> getProcessName(pid_t pid) {
#if defined(__APPLE__)
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    int n = proc_name(pid, buf, sizeof(buf));
    if (n <= 0) return std::nullopt;
    return std::string(buf);
#elif defined(__linux__)
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::string name;
    std::getline(in, name);
    name = trim(name);
    if (name.empty()) return std::nullopt;
    return name;
#else
    (void)pid;
    return std::nullopt;
#endif
}

static bool isSameProgram(const std::string& selfExe, const std::string& otherExe) {
    std::error_code ec1;
    std::error_code ec2;
    std::string selfCanon = std::filesystem::weakly_canonical(selfExe, ec1).string();
    std::string otherCanon = std::filesystem::weakly_canonical(otherExe, ec2).string();
    if (!ec1 && !ec2 && !selfCanon.empty() && !otherCanon.empty()) {
        if (selfCanon == otherCanon) return true;
    }
    auto selfName = std::filesystem::path(selfExe).filename().string();
    auto otherName = std::filesystem::path(otherExe).filename().string();
    if (!selfName.empty() && selfName == otherName && selfName == "synapsed") return true;
    return false;
}

static bool tryLockFileFcntl(int fd) {
    struct flock fl;
    std::memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    int rc;
    do {
        rc = fcntl(fd, F_SETLK, &fl);
    } while (rc != 0 && errno == EINTR);
    return rc == 0;
}

static pid_t getLockOwnerPidFcntl(int fd) {
    struct flock fl;
    std::memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    if (fcntl(fd, F_GETLK, &fl) != 0) return -1;
    if (fl.l_type == F_UNLCK) return -1;
    return fl.l_pid;
}

static void unlockFileFcntl(int fd) {
    struct flock fl;
    std::memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fcntl(fd, F_SETLK, &fl);
}

static bool terminateProcessIfSameExe(pid_t pid, const std::string& selfExe, const std::string& lockExe, int timeoutMs) {
    if (!isProcessAlive(pid)) return true;

    auto otherExeOpt = getProcessExePath(pid);
    if (!otherExeOpt.has_value() && lockExe.empty()) {
        auto nameOpt = getProcessName(pid);
        if (!nameOpt.has_value() || *nameOpt != "synapsed") return false;
    }

    if (otherExeOpt.has_value()) {
        if (!isSameProgram(selfExe, *otherExeOpt)) {
            if (lockExe.empty() || !isSameProgram(selfExe, lockExe)) return false;
        }
    } else {
        if (!isSameProgram(selfExe, lockExe)) return false;
    }

    if (::kill(pid, SIGTERM) != 0 && errno != ESRCH) return false;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!isProcessAlive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (::kill(pid, SIGKILL) != 0 && errno != ESRCH) return false;

    auto deadline2 = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < deadline2) {
        if (!isProcessAlive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !isProcessAlive(pid);
}
#endif

struct LockInfo {
    long pid = 0;
    std::string exe;
};

static LockInfo readLockInfo(const std::string& path) {
    LockInfo info;
    std::ifstream in(path);
    if (!in) return info;
    std::string pidLine;
    std::string exeLine;
    std::getline(in, pidLine);
    std::getline(in, exeLine);
    pidLine = trim(pidLine);
    exeLine = trim(exeLine);
    if (!pidLine.empty()) {
        try {
            info.pid = std::stol(pidLine);
        } catch (...) {
            info.pid = 0;
        }
    }
    info.exe = exeLine;
    return info;
}

static bool writeLockInfoToFd(int fd, long pid, const std::string& exe) {
    std::string data = std::to_string(pid);
    data.push_back('\n');
    data += exe;
    data.push_back('\n');

    if (ftruncate(fd, 0) != 0) return false;
    if (lseek(fd, 0, SEEK_SET) < 0) return false;

    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) return false;
        if (errno == EINTR) continue;
        return false;
    }
    fsync(fd);
    return true;
}

static bool writeLockInfo(const std::string& path, long pid, const std::string& exe) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << pid << "\n" << exe << "\n";
    return true;
}

} // namespace

std::unique_ptr<SingleInstanceLock> SingleInstanceLock::acquire(
    const std::string& dataDir,
    std::string* errorOut,
    const SingleInstanceOptions& options) {
    auto lock = std::unique_ptr<SingleInstanceLock>(new SingleInstanceLock());
    std::filesystem::create_directories(dataDir);
    lock->lockPath_ = dataDir + "/synapsed.lock";

#ifdef _WIN32
    std::string name = "Global\\SynapseNet_" + std::to_string(std::hash<std::string>{}(dataDir));
    HANDLE m = CreateMutexA(nullptr, TRUE, name.c_str());
    if (!m) {
        if (errorOut) *errorOut = "Failed to create instance mutex";
        return nullptr;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        if (errorOut) *errorOut = "Another SynapseNet instance is already running";
        return nullptr;
    }
    lock->mutex_ = m;
    writeLockInfo(lock->lockPath_, static_cast<long>(GetCurrentProcessId()), getSelfExePath());
    return lock;
#else
    const std::string selfExe = getSelfExePath();
    if (selfExe.empty()) {
        if (errorOut) *errorOut = "Failed to resolve executable path";
        return nullptr;
    }

    for (int attempt = 0; attempt < 4; ++attempt) {
        int fd = ::open(lock->lockPath_.c_str(), O_RDWR | O_CREAT
#ifdef O_CLOEXEC
                        | O_CLOEXEC
#endif
                        , 0644);
        if (fd < 0) {
            if (errorOut) *errorOut = "Failed to open instance lock file";
            return nullptr;
        }
        int flags = fcntl(fd, F_GETFD, 0);
        if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

        if (tryLockFileFcntl(fd)) {
            lock->fd_ = fd;
            if (!writeLockInfoToFd(fd, static_cast<long>(getpid()), selfExe)) {
                unlockFileFcntl(fd);
                ::close(fd);
                if (errorOut) *errorOut = "Failed to write instance lock file";
                return nullptr;
            }
            return lock;
        }

        pid_t ownerPid = getLockOwnerPidFcntl(fd);
        ::close(fd);

        if (!options.allowTerminateExisting) {
            if (errorOut) {
                if (ownerPid > 0) *errorOut = "Another SynapseNet instance is already running (pid " + std::to_string(ownerPid) + ")";
                else *errorOut = "Another SynapseNet instance is already running";
            }
            return nullptr;
        }

        LockInfo info = readLockInfo(lock->lockPath_);
        pid_t otherPid = ownerPid > 0 ? ownerPid : static_cast<pid_t>(info.pid);
        if (otherPid <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (!isProcessAlive(otherPid)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!terminateProcessIfSameExe(otherPid, selfExe, info.exe, options.terminateTimeoutMs)) {
            if (errorOut) {
                *errorOut = "Another instance is running (pid " + std::to_string(otherPid) + ") and could not be terminated safely";
            }
            return nullptr;
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.terminateTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            int fd2 = ::open(lock->lockPath_.c_str(), O_RDWR | O_CREAT
#ifdef O_CLOEXEC
                            | O_CLOEXEC
#endif
                            , 0644);
            if (fd2 < 0) break;
            int flags2 = fcntl(fd2, F_GETFD, 0);
            if (flags2 >= 0) fcntl(fd2, F_SETFD, flags2 | FD_CLOEXEC);
            bool ok = tryLockFileFcntl(fd2);
            if (ok) {
                lock->fd_ = fd2;
                if (!writeLockInfoToFd(fd2, static_cast<long>(getpid()), selfExe)) {
                    unlockFileFcntl(fd2);
                    ::close(fd2);
                    if (errorOut) *errorOut = "Failed to write instance lock file";
                    return nullptr;
                }
                return lock;
            }
            ::close(fd2);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (errorOut) *errorOut = "Failed to acquire instance lock";
    return nullptr;
#endif
}

SingleInstanceLock::~SingleInstanceLock() {
#ifdef _WIN32
    if (mutex_) {
        ReleaseMutex(reinterpret_cast<HANDLE>(mutex_));
        CloseHandle(reinterpret_cast<HANDLE>(mutex_));
        mutex_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        unlockFileFcntl(fd_);
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

} // namespace synapse::utils
