#include "ide/tools/bash_tool.h"

#include <cctype>
#include <cerrno>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synapse {
namespace ide {
namespace tools {

namespace {

enum class PathError {
    Ok,
    BadRoot,
    Absolute,
    Traversal,
    Outside,
    Invalid
};

struct ConfinedPath {
    PathError error = PathError::Invalid;
    std::filesystem::path root;
    std::filesystem::path path;
};

struct CommandOutput {
    bool started = false;
    int exitCode = -1;
    std::string output;
};

bool isControlText(const std::string& text) {
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

bool isDotDotSegment(std::string segment) {
#ifdef _WIN32
    const auto colon = segment.find(':');
    if (colon != std::string::npos) segment.resize(colon);
#endif
    while (!segment.empty()) {
        const unsigned char c = static_cast<unsigned char>(segment.back());
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') segment.pop_back();
        else break;
    }
#ifdef _WIN32
    while (segment.size() > 2 && segment.back() == '.') segment.pop_back();
#endif
    return segment == "..";
}

bool hasDotDotSegment(const std::string& pathText) {
    std::string segment;
    auto flush = [&]() {
        bool bad = isDotDotSegment(segment);
        segment.clear();
        return bad;
    };
    for (char c : pathText) {
        if (c == '/' || c == '\\') {
            if (flush()) return true;
        } else {
            segment.push_back(c);
        }
    }
    if (flush()) return true;

    const std::filesystem::path parsed(pathText);
    for (const auto& part : parsed) {
        if (isDotDotSegment(part.generic_string())) return true;
    }
    return false;
}

bool isAbsoluteInput(const std::string& pathText) {
    if (pathText.empty()) return false;
    const unsigned char lead = static_cast<unsigned char>(pathText[0]);
    if (lead == '/' || lead == '\\') return true;
    const std::filesystem::path parsed(pathText);
    return parsed.is_absolute() || parsed.has_root_name() || parsed.has_root_directory() ||
           parsed.has_root_path();
}

#ifdef _WIN32
bool isReservedDeviceSegment(const std::string& segment) {
    std::string name;
    for (unsigned char c : segment) {
        if (c == '.' || c == ':') break;
        if (c == ' ' || c == '\t') continue;
        name.push_back(static_cast<char>(std::tolower(c)));
    }
    if (name == "con" || name == "prn" || name == "aux" || name == "nul") return true;
    if (name.size() == 4 && (name.compare(0, 3, "com") == 0 || name.compare(0, 3, "lpt") == 0) &&
        name[3] >= '1' && name[3] <= '9') {
        return true;
    }
    return false;
}
#endif

PathError inspectSegments(const std::string& pathText) {
    std::string segment;
    auto flush = [&]() {
        if (isDotDotSegment(segment)) return PathError::Traversal;
#ifdef _WIN32
        if (isReservedDeviceSegment(segment)) return PathError::Invalid;
#endif
        return PathError::Ok;
    };
    for (char c : pathText) {
        if (c == '/' || c == '\\') {
            const PathError issue = flush();
            segment.clear();
            if (issue != PathError::Ok) return issue;
        } else {
            segment.push_back(c);
        }
    }
    const PathError tail = flush();
    if (tail != PathError::Ok) return tail;
    if (hasDotDotSegment(pathText)) return PathError::Traversal;
    return PathError::Ok;
}

std::filesystem::path canonicalOrLexical(const std::filesystem::path& path, std::error_code& ec) {
    namespace fs = std::filesystem;
    ec.clear();
    fs::path canon = fs::weakly_canonical(path, ec);
    if (!ec && !canon.empty()) return canon;

    ec.clear();
    fs::path cursor = path;
    std::vector<fs::path> tail;
    while (!cursor.empty()) {
        std::error_code existsEc;
        if (fs::exists(cursor, existsEc)) break;
        if (!cursor.has_filename() || cursor.filename().empty()) break;
        tail.push_back(cursor.filename());
        const fs::path parent = cursor.parent_path();
        if (parent.empty() || parent == cursor) return {};
        cursor = parent;
    }
    if (cursor.empty()) return {};
    std::error_code existsEc;
    if (!fs::exists(cursor, existsEc)) return {};
    canon = fs::weakly_canonical(cursor, ec);
    if (ec || canon.empty()) return {};
    for (auto it = tail.rbegin(); it != tail.rend(); ++it) {
        if (isDotDotSegment(it->generic_string())) {
            ec = std::make_error_code(std::errc::permission_denied);
            return {};
        }
        canon /= *it;
    }
    ec.clear();
    return canon.lexically_normal();
}

bool lockInside(const std::filesystem::path& root, std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path canon = canonicalOrLexical(path, ec);
    if (ec || canon.empty()) return false;
    if (canon == root) {
        path = canon;
        return true;
    }

    const fs::path relative = canon.lexically_relative(root);
    if (relative.empty()) return false;
    bool child = false;
    for (const auto& part : relative) {
        if (part == "..") return false;
        if (part != ".") child = true;
    }
    if (!child) {
        path = root;
        return true;
    }

    std::string rootText = root.generic_string();
    std::string canonText = canon.generic_string();
    if (rootText.empty()) return false;
    if (rootText.back() != '/') rootText.push_back('/');
#ifdef _WIN32
    for (char& ch : rootText) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (char& ch : canonText) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
#endif
    if (canonText.compare(0, rootText.size(), rootText) != 0) return false;
    path = canon;
    return true;
}

ConfinedPath confineUserPath(const std::string& workingDir,
                              const std::string& userPath,
                              bool allowRoot) {
    namespace fs = std::filesystem;
    ConfinedPath result;
    if (workingDir.empty() || userPath.empty() || isControlText(workingDir) ||
        isControlText(userPath)) {
        result.error = PathError::Invalid;
        return result;
    }
    const bool absolute = isAbsoluteInput(userPath);
    const PathError segments = inspectSegments(userPath);
    if (segments != PathError::Ok) {
        result.error = segments;
        return result;
    }

    try {
        std::error_code ec;
        const fs::path absoluteRoot = fs::absolute(fs::path(workingDir), ec);
        if (ec) {
            result.error = PathError::BadRoot;
            return result;
        }
        fs::path root = fs::weakly_canonical(absoluteRoot, ec);
        if (ec || root.empty() || !fs::is_directory(root, ec)) {
            result.error = PathError::BadRoot;
            return result;
        }

        fs::path joined = absolute ? fs::path(userPath) : root / fs::path(userPath);
        if (!lockInside(root, joined)) {
            result.error = absolute ? PathError::Absolute : PathError::Outside;
            return result;
        }
        if (!allowRoot && joined == root) {
            result.error = PathError::Invalid;
            return result;
        }
        result.error = PathError::Ok;
        result.root = root;
        result.path = joined;
        return result;
    } catch (const fs::filesystem_error&) {
        result.error = PathError::Invalid;
        return result;
    }
}

ToolResult pathError(PathError error) {
    const char* message = "invalid path";
    if (error == PathError::BadRoot) message = "invalid working directory";
    else if (error == PathError::Absolute) message = "absolute paths are not allowed";
    else if (error == PathError::Traversal) message = "path traversal is not allowed";
    else if (error == PathError::Outside) message = "path is outside the working directory";
    return ToolResult{message, false, ""};
}

bool hasShellMetaOrNewline(const std::string& command) {
    for (unsigned char c : command) {
        if (c < 0x20 || c == 0x7f) return true;
        switch (c) {
        case ';':
        case '|':
        case '&':
        case '<':
        case '>':
        case '`':
        case '$':
        case '\\':
        case '!':
        case '\'':
        case '"':
        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
        case '*':
        case '?':
        case '~':
        case '#':
        case '%':
        case '^':
            return true;
        default:
            break;
        }
    }
    return false;
}

std::vector<std::string> splitCommand(const std::string& command) {
    std::vector<std::string> args;
    std::string current;
    for (char c : command) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

#ifdef _WIN32
std::string quoteWindowsArg(const std::string& arg) {
    bool needsQuotes = arg.empty();
    if (!needsQuotes) {
        for (unsigned char c : arg) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"') {
                needsQuotes = true;
                break;
            }
        }
    }
    if (!needsQuotes) return arg;

    std::string out;
    out.push_back('"');
    size_t i = 0;
    while (i < arg.size()) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++slashes;
            ++i;
        }
        if (i >= arg.size()) {
            out.append(slashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            out.append(slashes * 2 + 1, '\\');
            out.push_back('"');
            ++i;
        } else {
            out.append(slashes, '\\');
            out.push_back(arg[i]);
            ++i;
        }
    }
    out.push_back('"');
    return out;
}
#endif

CommandOutput runArgv(const std::vector<std::string>& args,
                      const std::filesystem::path& cwd,
                      size_t maxBytes,
                      size_t maxLines,
                      bool mergeStderr) {
    CommandOutput result;
    if (args.empty() || args[0].empty() || maxBytes == 0) return result;
    for (const auto& arg : args) {
        if (arg.find('\0') != std::string::npos) return result;
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &inherit, 0)) return result;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulIn = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nulErr = nullptr;
    if (!mergeStderr) {
        nulErr = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (nulIn == INVALID_HANDLE_VALUE || (!mergeStderr && nulErr == INVALID_HANDLE_VALUE)) {
        if (nulIn != nullptr && nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
        if (nulErr != nullptr && nulErr != INVALID_HANDLE_VALUE) CloseHandle(nulErr);
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return result;
    }

    std::string commandLine;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) commandLine.push_back(' ');
        commandLine += quoteWindowsArg(args[i]);
    }
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nulIn;
    startup.hStdOutput = writePipe;
    startup.hStdError = mergeStderr ? writePipe : nulErr;

    const std::string cwdText = cwd.string();
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr,
                                        cwdText.empty() ? nullptr : cwdText.c_str(), &startup,
                                        &process);
    CloseHandle(writePipe);
    CloseHandle(nulIn);
    if (nulErr != nullptr) CloseHandle(nulErr);
    if (!created) {
        CloseHandle(readPipe);
        return result;
    }

    std::string output;
    char buffer[4096];
    size_t lines = 0;
    bool stop = false;
    bool hitBytes = false;
    DWORD n = 0;
    while (!stop && ReadFile(readPipe, buffer, sizeof(buffer), &n, nullptr) && n > 0) {
        for (DWORD i = 0; i < n; ++i) {
            if (maxLines > 0 && lines >= maxLines) {
                stop = true;
                break;
            }
            if (output.size() >= maxBytes) {
                hitBytes = true;
                stop = true;
                break;
            }
            output.push_back(buffer[i]);
            if (buffer[i] == '\n') ++lines;
        }
    }
    if (stop) TerminateProcess(process.hProcess, 1);
    CloseHandle(readPipe);
    if (hitBytes) output += "\n... (output truncated)";

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result.started = true;
    result.exitCode = static_cast<int>(code);
    result.output = std::move(output);
    return result;
#else
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) return result;

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }
    if (pid == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(127);
        const int inFd = open("/dev/null", O_RDONLY);
        if (inFd < 0) _exit(127);
        if (dup2(inFd, STDIN_FILENO) < 0) _exit(127);
        if (inFd > 2) close(inFd);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        if (mergeStderr) {
            if (dup2(pipefd[1], STDERR_FILENO) < 0) _exit(127);
        } else {
            const int errFd = open("/dev/null", O_WRONLY);
            if (errFd < 0 || dup2(errFd, STDERR_FILENO) < 0) _exit(127);
            if (errFd > 2) close(errFd);
        }
        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[4096];
    size_t lines = 0;
    bool stop = false;
    bool hitBytes = false;
    while (!stop) {
        const ssize_t n = ::read(pipefd[0], buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        for (ssize_t i = 0; i < n; ++i) {
            if (maxLines > 0 && lines >= maxLines) {
                stop = true;
                break;
            }
            if (output.size() >= maxBytes) {
                hitBytes = true;
                stop = true;
                break;
            }
            output.push_back(buffer[i]);
            if (buffer[i] == '\n') ++lines;
        }
    }
    if (stop) ::kill(pid, SIGKILL);
    close(pipefd[0]);
    if (hitBytes) output += "\n... (output truncated)";

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    result.started = true;
    if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exitCode = 128 + WTERMSIG(status);
    result.output = std::move(output);
    return result;
#endif
}

}

BashTool::BashTool(const std::string& workingDir)
    : workingDir_(workingDir) {}

std::string BashTool::name() const { return "bash"; }

std::string BashTool::description() const {
    return "Runs a program with arguments in the working directory and returns its output.";
}

ToolResult BashTool::execute(const std::string& paramsJson) {
    std::string command;

    std::string::size_type cmdPos = paramsJson.find("\"command\"");
    if (cmdPos == std::string::npos) {
        return ToolResult{"command parameter is required", false, ""};
    }
    std::string::size_type valStart = paramsJson.find(':', cmdPos);
    if (valStart == std::string::npos) {
        return ToolResult{"malformed params", false, ""};
    }
    std::string::size_type qStart = paramsJson.find('"', valStart + 1);
    if (qStart == std::string::npos) {
        return ToolResult{"malformed params", false, ""};
    }
    std::string::size_type qEnd = qStart + 1;
    while (qEnd < paramsJson.size()) {
        if (paramsJson[qEnd] == '\\') {
            qEnd += 2;
            continue;
        }
        if (paramsJson[qEnd] == '"') break;
        ++qEnd;
    }
    command = paramsJson.substr(qStart + 1, qEnd - qStart - 1);

    if (command.empty()) {
        return ToolResult{"command is empty", false, ""};
    }
    if (hasShellMetaOrNewline(command)) {
        return ToolResult{"command contains shell metacharacters or newlines", false, ""};
    }

    {
        std::string lower = command;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static const char* blocklist[] = {
            "rm -rf /", "rm -rf /*", "mkfs", "dd if=",
            ":(){", "fork", ">(", "/dev/sd",
            "chmod -r 000", "chown -r",
            "curl ", "wget ", "nc ", "ncat ",
            "ssh ", "scp ", "sftp ", "rsync ",
            "python3 -c", "python -c", "perl -e", "ruby -e",
            ".ssh/", "private_key", "privatekey", "wallet.dat",
            "id_rsa", "id_ed25519", "secret", "mnemonic",
            "passwd", "shadow", "/etc/",
            "xargs", "eval ", "base64",
            "openssl", "gpg ",
        };
        for (const auto& b : blocklist) {
            if (lower.find(b) != std::string::npos) {
                return ToolResult{"blocked: command contains restricted pattern", false, ""};
            }
        }
        if (command.find("..") != std::string::npos) {
            return ToolResult{"blocked: path traversal not allowed", false, ""};
        }
    }

    const std::vector<std::string> args = splitCommand(command);
    if (args.empty()) {
        return ToolResult{"command is empty", false, ""};
    }
    for (const auto& arg : args) {
        if (hasDotDotSegment(arg)) {
            return ToolResult{"path traversal is not allowed", false, ""};
        }
        if (!isAbsoluteInput(arg)) continue;
        const ConfinedPath confinedArg = confineUserPath(workingDir_, arg, true);
        if (confinedArg.error != PathError::Ok) return pathError(confinedArg.error);
    }

    const ConfinedPath cwd = confineUserPath(workingDir_, ".", true);
    if (cwd.error != PathError::Ok) return pathError(cwd.error);

    const CommandOutput ran = runArgv(args, cwd.root, static_cast<size_t>(MaxOutputLength), 0, true);
    if (!ran.started) {
        return ToolResult{"failed to execute command", false, ""};
    }

    std::string output = ran.output;
    if (output.empty()) output = "no output";
    return ToolResult{output, ran.exitCode == 0, ""};
}

}
}
}
