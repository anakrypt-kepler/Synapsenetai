#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ide/agent_event.h"
#include "ide/tools/tool_base.h"

namespace synapse {
namespace ide {

class IAgentCoordinator {
public:
    virtual ~IAgentCoordinator() = default;
    virtual tools::ToolResult run(const std::string& sessionId, const std::string& prompt) = 0;
    virtual void cancel(const std::string& sessionId) = 0;
    virtual void cancelAll() = 0;
    virtual bool isBusy() const = 0;
    virtual bool isSessionBusy(const std::string& sessionId) const = 0;
};

class AgentCoordinator : public IAgentCoordinator {
public:
    AgentCoordinator();
    ~AgentCoordinator() override;

    void registerTool(std::shared_ptr<tools::ITool> tool);
    std::vector<std::string> registeredToolNames() const;
    tools::ToolResult dispatchTool(const std::string& toolName, const std::string& paramsJson);

    tools::ToolResult run(const std::string& sessionId, const std::string& prompt) override;
    void cancel(const std::string& sessionId) override;
    void cancelAll() override;
    bool isBusy() const override;
    bool isSessionBusy(const std::string& sessionId) const override;

    void setEventCallback(std::function<void(const AgentEvent&)> cb);

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<tools::ITool>> tools_;
    std::unordered_map<std::string, bool> busySessions_;
    std::atomic<bool> busy_{false};
    std::function<void(const AgentEvent&)> eventCallback_;
};

}
}
