#pragma once

#include <memory>
#include <string>

namespace synapse {
namespace ide {
namespace tools {

struct ToolResult {
    std::string output;
    bool success = false;
    std::string metadata;
};

class ITool {
public:
    virtual ~ITool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual ToolResult execute(const std::string& paramsJson) = 0;
};

}
}
}
