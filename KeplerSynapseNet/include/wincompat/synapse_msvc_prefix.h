#pragma once

// Forced in ahead of every MSVC translation unit. Winsock has to land before
// windows.h, and the names below are what the node sources already call.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _USE_MATH_DEFINES
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#ifdef DEBUG
#undef DEBUG
#endif
#include <BaseTsd.h>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <direct.h>
#include <intrin.h>
#include <io.h>
#include <process.h>

#ifndef ssize_t
using ssize_t = SSIZE_T;
#endif
using pid_t = int;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef __builtin_popcountll
#define __builtin_popcountll(x) static_cast<int>(__popcnt64(static_cast<unsigned long long>(x)))
#endif

#ifndef popen
#define popen _popen
#define pclose _pclose
#endif

#ifndef F_GETFL
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 0x4000
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#define SIGKILL 9
#define SIGTERM 15
#define SIGXCPU 24
#define WNOHANG 1
#define MSG_NOSIGNAL 0
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
#define RLIMIT_AS 0
#define RLIMIT_CPU 1
#define RLIMIT_NOFILE 2
#define RLIMIT_FSIZE 3
#define RUSAGE_SELF 0
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) > 0 && (((status) & 0x7f) != 0x7f))
#define WTERMSIG(status) ((status) & 0x7f)
#endif

struct rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};
struct rusage {
    long ru_maxrss;
};

inline int syn_setsockopt_win(SOCKET fd, int level, int optname, const void* optval, int optlen) {
    if ((optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
        optval && optlen >= static_cast<int>(sizeof(timeval))) {
        const auto* tv = static_cast<const timeval*>(optval);
        DWORD ms = static_cast<DWORD>(tv->tv_sec * 1000 + tv->tv_usec / 1000);
        if (ms == 0) ms = 1;
        return ::setsockopt(fd, level, optname, reinterpret_cast<const char*>(&ms), sizeof(ms));
    }
    return ::setsockopt(fd, level, optname, static_cast<const char*>(optval), optlen);
}
#define setsockopt(fd, lvl, opt, ptr, len) \
    syn_setsockopt_win(static_cast<SOCKET>(fd), (lvl), (opt), (ptr), static_cast<int>(len))

inline int getsockopt(SOCKET fd, int level, int optname, void* optval, int* optlen) {
    return ::getsockopt(fd, level, optname, static_cast<char*>(optval), optlen);
}
inline int send(SOCKET fd, const void* buf, int len, int flags) {
    return ::send(fd, static_cast<const char*>(buf), len, flags);
}
inline int recv(SOCKET fd, void* buf, int len, int flags) {
    return ::recv(fd, static_cast<char*>(buf), len, flags);
}
inline int close(int fd) {
    if (::closesocket(static_cast<SOCKET>(fd)) == 0) return 0;
    if (WSAGetLastError() == WSAENOTSOCK) return _close(fd);
    return -1;
}

inline int fcntl(int fd, int cmd, ...) {
    if (cmd == F_GETFL) return 0;
    if (cmd == F_SETFL) {
        va_list ap;
        va_start(ap, cmd);
        const int flags = va_arg(ap, int);
        va_end(ap);
        u_long mode = (flags & O_NONBLOCK) ? 1UL : 0UL;
        return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode) == 0 ? 0 : -1;
    }
    return -1;
}

inline int setrlimit(int, const rlimit*) { return -1; }
inline int getrlimit(int, rlimit*) { return -1; }
inline int getrusage(int, rusage*) { return -1; }
inline int fork() { return -1; }
inline int pipe(int fds[2]) { return _pipe(fds, 4096, 0x8000); }
inline int dup(int fd) { return _dup(fd); }
inline int dup2(int from, int to) { return _dup2(from, to); }
inline int waitpid(int, int*, int) { return -1; }
inline int kill(int, int) { return -1; }
inline int access(const char* path, int) { return _access(path, 0); }
inline int fsync(int fd) { return _commit(fd); }
inline long lseek(int fd, long off, int whence) { return _lseek(fd, off, whence); }
inline int ftruncate(int fd, long length) { return _chsize(fd, length); }
inline int mlock(const void*, size_t) { return -1; }
inline int munlock(const void*, size_t) { return -1; }
inline tm* localtime_r(const time_t* clock, tm* out) {
    if (!clock || !out) return nullptr;
    return localtime_s(out, clock) == 0 ? out : nullptr;
}
inline tm* gmtime_r(const time_t* clock, tm* out) {
    if (!clock || !out) return nullptr;
    return gmtime_s(out, clock) == 0 ? out : nullptr;
}
inline ssize_t read(int fd, void* buf, size_t count) {
    return _read(fd, buf, static_cast<unsigned>(count));
}
inline ssize_t write(int fd, const void* buf, size_t count) {
    return _write(fd, buf, static_cast<unsigned>(count));
}

#endif
