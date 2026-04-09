#pragma once

#include <cstdint>
#include <string>

namespace synapse {
namespace ide {

enum class AgentEventType {
    PromptSent,
    PromptResponded,
    TokensUsed,
    ToolCalled,
    ToolCompleted,
    Error
};

struct TokenUsage {
    int64_t inputTokens = 0;
    int64_t outputTokens = 0;
    int64_t cacheReadTokens = 0;
    int64_t cacheCreationTokens = 0;

    int64_t total() const {
        return inputTokens + outputTokens + cacheReadTokens + cacheCreationTokens;
    }
};

struct AgentEvent {
    AgentEventType type;
    std::string sessionId;
    std::string provider;
    std::string model;
    std::string reasoningEffort;
    bool thinkingMode = false;
    double durationSeconds = 0.0;
    TokenUsage usage;
    double cost = 0.0;
};

}
}
