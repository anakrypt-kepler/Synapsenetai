#pragma once

#include <stdexcept>
#include <string>

namespace synapse {
namespace ide {

class AgentError : public std::runtime_error {
public:
    explicit AgentError(const std::string& msg) : std::runtime_error(msg) {}
};

class RequestCancelledError : public AgentError {
public:
    RequestCancelledError() : AgentError("request canceled by user") {}
};

class SessionBusyError : public AgentError {
public:
    SessionBusyError() : AgentError("session is currently processing another request") {}
};

class EmptyPromptError : public AgentError {
public:
    EmptyPromptError() : AgentError("prompt is empty") {}
};

class SessionMissingError : public AgentError {
public:
    SessionMissingError() : AgentError("session id is missing") {}
};

}
}
