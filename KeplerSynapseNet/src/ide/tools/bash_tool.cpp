#include "ide/tools/bash_tool.h"

#include <array>
#include <cstdio>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synapse {
namespace ide {
namespace tools {

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

    std::string fullCmd = "cd " + workDir + " && " + command + " 2>&1";
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
