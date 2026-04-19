#include "ide/agent_prompt.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synapse {
namespace ide {

namespace {

std::string execCommand(const std::string& cmd, const std::string& workDir) {
    std::string fullCmd = "cd " + workDir + " && " + cmd + " 2>/dev/null";
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) return "";
    std::string result;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
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
    std::string branch = execCommand("git branch --show-current", dir);
    if (branch.empty()) return "";
    return "Current branch: " + branch + "\n";
}

std::string AgentPrompt::getGitStatusSummary() const {
    std::string dir = workingDir_.empty() ? "." : workingDir_;
    std::string status = execCommand("git status --short | head -20", dir);
    if (status.empty()) return "Status: clean\n";
    return "Status:\n" + status + "\n";
}

std::string AgentPrompt::getGitRecentCommits() const {
    std::string dir = workingDir_.empty() ? "." : workingDir_;
    std::string commits = execCommand("git log --oneline -n 3", dir);
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
