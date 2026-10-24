#include "ide/agent_prompt.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synapse {
namespace ide {

namespace {

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

std::string trimTrailingNewlines(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

std::string firstLines(std::string text, size_t maxLines) {
    size_t lines = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n') continue;
        ++lines;
        if (lines >= maxLines) {
            text.resize(i);
            break;
        }
    }
    return trimTrailingNewlines(std::move(text));
}

// One program, argument vector, no shell. git -C receives the directory as data.
std::string captureProgram(const std::vector<std::string>& args) {
    if (args.empty() || args[0].empty()) return "";
    for (const auto& arg : args) {
        if (arg.find('\0') != std::string::npos) return "";
    }
#ifdef _WIN32
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &inherit, 0)) return "";
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nulIn = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nulErr = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulIn == INVALID_HANDLE_VALUE || nulErr == INVALID_HANDLE_VALUE) {
        if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
        if (nulErr != INVALID_HANDLE_VALUE) CloseHandle(nulErr);
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return "";
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
    startup.hStdError = nulErr;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    CloseHandle(nulIn);
    CloseHandle(nulErr);
    if (!created) {
        CloseHandle(readPipe);
        return "";
    }
    std::string output;
    char buffer[4096];
    DWORD n = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &n, nullptr) && n > 0)
        output.append(buffer, buffer + n);
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return trimTrailingNewlines(std::move(output));
#else
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) return "";
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return "";
    }
    if (pid == 0) {
        const int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
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
    while (true) {
        const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        output.append(buffer, buffer + n);
    }
    close(pipefd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return trimTrailingNewlines(std::move(output));
#endif
}

std::string getRuntimePlatform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

}

AgentPrompt::AgentPrompt() = default;

void AgentPrompt::setName(const std::string& name) { name_ = name; }
void AgentPrompt::setTemplate(const std::string& tmpl) { template_ = tmpl; }
void AgentPrompt::setWorkingDir(const std::string& dir) { workingDir_ = dir; }
void AgentPrompt::setPlatform(const std::string& platform) { platform_ = platform; }
void AgentPrompt::setProvider(const std::string& provider) { provider_ = provider; }
void AgentPrompt::setModel(const std::string& model) { model_ = model; }
void AgentPrompt::setMaxTokenBudget(int64_t budget) { maxTokenBudget_ = budget; }

std::string AgentPrompt::name() const { return name_; }

std::string AgentPrompt::build() const {
    std::string platform = platform_.empty() ? getRuntimePlatform() : platform_;
    std::string workDir = workingDir_.empty() ? "." : workingDir_;

    std::ostringstream out;
    out << "Working directory: " << workDir << "\n";
    out << "Platform: " << platform << "\n";
    out << "Date: " << currentDate() << "\n";

    if (isGitRepo()) {
        std::string status = getGitStatus();
        if (!status.empty()) {
            out << status;
        }
    }

    if (!provider_.empty()) {
        out << "Provider: " << provider_ << "\n";
    }
    if (!model_.empty()) {
        out << "Model: " << model_ << "\n";
    }

    return out.str();
}

bool AgentPrompt::isGitRepo() const {
    std::string dir = workingDir_.empty() ? "." : workingDir_;
    std::string gitDir = dir + "/.git";
    struct stat st{};
    return stat(gitDir.c_str(), &st) == 0;
}

std::string AgentPrompt::getGitStatus() const {
    std::string dir = workingDir_.empty() ? "." : workingDir_;
    std::string result;

    std::string branch = getGitBranch();
    if (!branch.empty()) {
        result += branch;
    }

    std::string status = getGitStatusSummary();
    if (!status.empty()) {
        result += status;
    }

    std::string commits = getGitRecentCommits();
    if (!commits.empty()) {
        result += commits;
    }

    return result;
}

std::string AgentPrompt::getGitBranch() const {
    std::string dir = workingDir_.empty() ? "." : workingDir_;
    std::string branch = captureProgram({"git", "-C", dir, "branch", "--show-current"});
    if (branch.empty()) return "";
    return "Current branch: " + branch + "\n";
}

std::string AgentPrompt::getGitStatusSummary() const {
    std::string dir = workingDir_.empty() ? "." : workingDir_;
    std::string status = firstLines(captureProgram({"git", "-C", dir, "status", "--short"}), 20);
    if (status.empty()) return "Status: clean\n";
    return "Status:\n" + status + "\n";
}

std::string AgentPrompt::getGitRecentCommits() const {
    std::string dir = workingDir_.empty() ? "." : workingDir_;
    std::string commits = captureProgram({"git", "-C", dir, "log", "--oneline", "-n", "3"});
    if (commits.empty()) return "";
    return "Recent commits:\n" + commits + "\n";
}

std::string AgentPrompt::currentDate() const {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d/%d/%d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
    return buf;
}

}
}
