#include "ide/tools/bash_tool.h"

#include <array>
#include <cstdio>
#include <sstream>
#include <set>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synapse {
namespace ide {
namespace tools {

static const std::set<std::string> kAllowedCommands = {
    "ls", "cat", "head", "tail", "grep", "find", "wc", "echo",
    "pwd", "whoami", "date", "uname", "df", "du", "file", "stat",
    "diff", "sort", "uniq", "tr", "cut", "tee", "env", "printenv",
    "git", "make", "cmake", "python", "python3", "node", "npm",
    "cargo", "rustc", "go", "gcc", "g++", "clang", "clang++",
    "test", "mkdir", "cp", "mv", "touch", "basename", "dirname",
    "realpath", "readlink", "sha256sum", "md5sum", "hexdump", "xxd",
    "tar", "gzip", "gunzip", "zip", "unzip", "curl", "wget"
};

static std::string extractBaseCommand(const std::string& cmd) {
    size_t start = 0;
    while (start < cmd.size() && (cmd[start] == ' ' || cmd[start] == '\t')) ++start;

    while (start < cmd.size() && cmd.find('=', start) != std::string::npos) {
        size_t eq = cmd.find('=', start);
        size_t sp = cmd.find(' ', start);
        if (eq < sp) {
            start = sp;
            while (start < cmd.size() && cmd[start] == ' ') ++start;
        } else {
            break;
        }
    }

    size_t end = start;
    while (end < cmd.size() && cmd[end] != ' ' && cmd[end] != '\t' &&
           cmd[end] != ';' && cmd[end] != '|' && cmd[end] != '&') ++end;

    std::string base = cmd.substr(start, end - start);

    size_t slash = base.rfind('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);

    return base;
}

static bool isCommandAllowed(const std::string& command) {
    std::string base = extractBaseCommand(command);
    return kAllowedCommands.count(base) > 0;
}

static std::string shellEscape(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

BashTool::BashTool(const std::string& workingDir)
    : workingDir_(workingDir) {}

std::string BashTool::name() const { return "bash"; }

std::string BashTool::description() const {
    return "Executes a bash command and returns its output.";
}

ToolResult BashTool::execute(const std::string& paramsJson) {
    std::string command;
    std::string workDir = workingDir_;

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

    if (!isCommandAllowed(command)) {
        return ToolResult{"command not allowed: " + extractBaseCommand(command), false, ""};
    }

    std::string fullCmd = "cd " + shellEscape(workDir) + " && " + command + " 2>&1";
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        return ToolResult{"failed to execute command", false, ""};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
        if (static_cast<int>(output.size()) > MaxOutputLength) {
            output = output.substr(0, MaxOutputLength);
            output += "\n... (output truncated)";
            break;
        }
    }
    int rc = pclose(pipe);

#ifndef _WIN32
    if (WIFEXITED(rc)) {
        rc = WEXITSTATUS(rc);
    }
#endif

    if (output.empty()) {
        output = "no output";
    }

    return ToolResult{output, rc == 0, ""};
}

}
}
}
