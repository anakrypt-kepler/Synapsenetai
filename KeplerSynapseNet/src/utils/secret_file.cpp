#include "utils/secret_file.h"

#include <cerrno>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace synapse {
namespace utils {
namespace {

bool pathArgOk(const std::string& path) {
    return !path.empty() && path.find('\0') == std::string::npos;
}

#ifdef _WIN32

bool handleDaclIsCurrentUserOnly(HANDLE file) {
    DWORD bytesNeeded = 0;
    GetKernelObjectSecurity(file, DACL_SECURITY_INFORMATION, nullptr, 0, &bytesNeeded);
    if (bytesNeeded == 0) return false;

    std::vector<unsigned char> buffer(bytesNeeded);
    if (!GetKernelObjectSecurity(
            file,
            DACL_SECURITY_INFORMATION,
            buffer.data(),
            bytesNeeded,
            &bytesNeeded)) {
        return false;
    }

    auto* descriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(buffer.data());
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present || !dacl) {
        return false;
    }

    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) return false;
    if ((control & SE_DACL_PROTECTED) == 0) return false;

    ACL_SIZE_INFORMATION aclSize{};
    if (!GetAclInformation(dacl, &aclSize, sizeof(aclSize), AclSizeInformation)) return false;
    if (aclSize.AceCount != 1) return false;

    void* ace = nullptr;
    if (!GetAce(dacl, 0, &ace) || !ace) return false;
    auto* header = static_cast<ACE_HEADER*>(ace);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return false;
    auto* allow = static_cast<ACCESS_ALLOWED_ACE*>(ace);
    const DWORD neededRights = FILE_READ_DATA | FILE_WRITE_DATA | DELETE;
    if ((allow->Mask & neededRights) != neededRights) return false;

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD tokenBytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
    if (tokenBytes == 0) {
        CloseHandle(token);
        return false;
    }
    std::vector<unsigned char> tokenBuffer(tokenBytes);
    if (!GetTokenInformation(token, TokenUser, tokenBuffer.data(), tokenBytes, &tokenBytes)) {
        CloseHandle(token);
        return false;
    }
    CloseHandle(token);

    auto* tokenUser = reinterpret_cast<TOKEN_USER*>(tokenBuffer.data());
    return EqualSid(tokenUser->User.Sid, reinterpret_cast<PSID>(&allow->SidStart)) == TRUE;
}

struct CurrentUserAcl {
    std::vector<unsigned char> tokenBuffer;
    PACL acl = nullptr;
    SECURITY_DESCRIPTOR descriptor{};

    CurrentUserAcl() = default;
    CurrentUserAcl(const CurrentUserAcl&) = delete;
    CurrentUserAcl& operator=(const CurrentUserAcl&) = delete;

    ~CurrentUserAcl() {
        if (acl) LocalFree(acl);
    }

    bool build(bool inherit) {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        DWORD tokenBytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
        if (tokenBytes == 0) {
            CloseHandle(token);
            return false;
        }
        tokenBuffer.resize(tokenBytes);
        if (!GetTokenInformation(token, TokenUser, tokenBuffer.data(), tokenBytes, &tokenBytes)) {
            CloseHandle(token);
            return false;
        }
        CloseHandle(token);
        auto* tokenUser = reinterpret_cast<TOKEN_USER*>(tokenBuffer.data());
        if (!tokenUser->User.Sid) return false;

        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_ALL_ACCESS;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = inherit ? (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) : NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = static_cast<LPWSTR>(tokenUser->User.Sid);

        if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS || !acl) return false;
        if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) ||
            !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
            return false;
        }
        return true;
    }
};

std::wstring widePath(const std::string& path) {
    return std::filesystem::path(path).wstring();
}

bool rejectReparse(const std::wstring& wide, bool directory) {
    const DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
    const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return isDir == directory;
}

bool applyCurrentUserDacl(const std::wstring& wide, bool directory) {
    if (!rejectReparse(wide, directory)) return false;
    CurrentUserAcl acl;
    if (!acl.build(directory)) return false;

    std::wstring mutableName = wide;
    const DWORD info = DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;
    if (SetNamedSecurityInfoW(
            &mutableName[0],
            SE_FILE_OBJECT,
            info,
            nullptr,
            nullptr,
            acl.acl,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    HANDLE file = CreateFileW(
        wide.c_str(),
        READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const BOOL restricted = SetKernelObjectSecurity(file, info, &acl.descriptor);
    const bool currentUser = restricted && handleDaclIsCurrentUserOnly(file);
    CloseHandle(file);
    return currentUser;
}

bool writeAll(HANDLE file, const std::string& contents) {
    size_t offset = 0;
    while (offset < contents.size()) {
        const size_t chunk = contents.size() - offset;
        const DWORD ask = static_cast<DWORD>(chunk > 1u << 20 ? (1u << 20) : chunk);
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, ask, &written, nullptr) || written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

#else

bool writeAll(int fd, const std::string& contents) {
    size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t n = ::write(fd, contents.data() + offset, contents.size() - offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}

bool modeIs(int fd, mode_t expected) {
    struct stat st{};
    if (::fstat(fd, &st) != 0) return false;
    if (st.st_uid != ::geteuid()) return false;
    if ((st.st_mode & 0777) != expected) return false;
    if (expected == (S_IRUSR | S_IWUSR)) return S_ISREG(st.st_mode);
    return S_ISDIR(st.st_mode);
}

#endif

} // namespace

bool lockFileToCurrentUser(const std::string& path) {
    if (!pathArgOk(path)) return false;
#ifdef _WIN32
    return applyCurrentUserDacl(widePath(path), false);
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return false;
    const mode_t mode = S_IRUSR | S_IWUSR;
    if (::fchmod(fd, mode) != 0 || !modeIs(fd, mode)) {
        ::close(fd);
        return false;
    }
    ::close(fd);
    return true;
#endif
}

bool lockDirectoryToCurrentUser(const std::string& path) {
    if (!pathArgOk(path)) return false;
#ifdef _WIN32
    return applyCurrentUserDacl(widePath(path), true);
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return false;
    const mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
    if (::fchmod(fd, mode) != 0 || !modeIs(fd, mode)) {
        ::close(fd);
        return false;
    }
    ::close(fd);
    return true;
#endif
}

bool writeFileForCurrentUser(const std::string& path, const std::string& contents) {
    if (!pathArgOk(path)) return false;
    const std::string tmp = path + ".privtmp";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

#ifdef _WIN32
    const std::wstring tmpWide = widePath(tmp);
    const std::wstring destWide = widePath(path);
    const DWORD destAttrs = GetFileAttributesW(destWide.c_str());
    if (destAttrs != INVALID_FILE_ATTRIBUTES && (destAttrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }

    CurrentUserAcl acl;
    if (!acl.build(false)) return false;
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = &acl.descriptor;
    attributes.bInheritHandle = FALSE;

    HANDLE file = CreateFileW(
        tmpWide.c_str(),
        GENERIC_READ | GENERIC_WRITE | WRITE_DAC,
        0,
        &attributes,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    const DWORD info = DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;
    const bool restricted =
        SetKernelObjectSecurity(file, info, &acl.descriptor) && handleDaclIsCurrentUserOnly(file);
    const bool wrote = restricted && writeAll(file, contents);
    CloseHandle(file);
    if (!wrote) {
        DeleteFileW(tmpWide.c_str());
        return false;
    }
    if (!MoveFileExW(tmpWide.c_str(), destWide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpWide.c_str());
        return false;
    }
    // The temp file was already limited to the current user, and a same-directory
    // rename keeps that DACL. A failed follow-up lock must not delete the secret.
    lockFileToCurrentUser(path);
    return true;
#else
    const int flags = O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC;
    const int fd = ::open(tmp.c_str(), flags, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    const mode_t mode = S_IRUSR | S_IWUSR;
    if (::fchmod(fd, mode) != 0 || !modeIs(fd, mode) || !writeAll(fd, contents)) {
        ::close(fd);
        ::unlink(tmp.c_str());
        return false;
    }
    if (::close(fd) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    // The temp file was already mode 0600. A failed follow-up lock must not
    // unlink the secret that just replaced the previous file.
    lockFileToCurrentUser(path);
    return true;
#endif
}

} // namespace utils
} // namespace synapse
