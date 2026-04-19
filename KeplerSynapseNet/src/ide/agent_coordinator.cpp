#include "ide/agent_coordinator.h"

#include <algorithm>

namespace synapse {
namespace ide {

AgentCoordinator::AgentCoordinator() = default;
AgentCoordinator::~AgentCoordinator() = default;

void AgentCoordinator::registerTool(std::shared_ptr<tools::ITool> tool) {
    if (!tool) return;
    std::lock_guard<std::mutex> lock(mtx_);
    tools_[tool->name()] = std::move(tool);
}

std::vector<std::string> AgentCoordinator::registeredToolNames() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& kv : tools_) {
        names.push_back(kv.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

tools::ToolResult AgentCoordinator::dispatchTool(const std::string& toolName,
                                                  const std::string& paramsJson) {
    std::shared_ptr<tools::ITool> tool;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = tools_.find(toolName);
        if (it == tools_.end()) {
            return tools::ToolResult{"unknown tool: " + toolName, false, ""};
        }
        tool = it->second;
    }
    return tool->execute(paramsJson);
}

tools::ToolResult AgentCoordinator::run(const std::string& sessionId,
                                         const std::string& prompt) {
    if (sessionId.empty()) {
        return tools::ToolResult{"session id is missing", false, ""};
    }
    if (prompt.empty()) {
        return tools::ToolResult{"prompt is empty", false, ""};
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (busySessions_.count(sessionId) && busySessions_[sessionId]) {
            return tools::ToolResult{
                "session is currently processing another request", false, ""};
        }
        busySessions_[sessionId] = true;
    }
    busy_.store(true);

    if (eventCallback_) {
        AgentEvent ev;
        ev.type = AgentEventType::PromptSent;
        ev.sessionId = sessionId;
        eventCallback_(ev);
    }

    tools::ToolResult result;
    result.output = "prompt received: " + prompt;
    result.success = true;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        busySessions_[sessionId] = false;
        bool anyBusy = false;
        for (const auto& kv : busySessions_) {
            if (kv.second) { anyBusy = true; break; }
        }
        busy_.store(anyBusy);
    }

    if (eventCallback_) {
        AgentEvent ev;
        ev.type = AgentEventType::PromptResponded;
        ev.sessionId = sessionId;
        eventCallback_(ev);
    }

    return result;
}

void AgentCoordinator::cancel(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mtx_);
    busySessions_[sessionId] = false;
    bool anyBusy = false;
    for (const auto& kv : busySessions_) {
        if (kv.second) { anyBusy = true; break; }
    }
    busy_.store(anyBusy);
}

void AgentCoordinator::cancelAll() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& kv : busySessions_) {
        kv.second = false;
    }
    busy_.store(false);
}

bool AgentCoordinator::isBusy() const {
    return busy_.load();
}

bool AgentCoordinator::isSessionBusy(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = busySessions_.find(sessionId);
    if (it == busySessions_.end()) return false;
    return it->second;
}

void AgentCoordinator::setEventCallback(std::function<void(const AgentEvent&)> cb) {
    std::lock_guard<std::mutex> lock(mtx_);
    eventCallback_ = std::move(cb);
}

}
}
