#pragma once

#include "ide/tools/tool_base.h"

#include <string>

namespace synapse {
namespace ide {
namespace tools {

class BashTool : public ITool {
public:
    explicit BashTool(const std::string& workingDir = ".");

    std::string name() const override;
    std::string description() const override;
    ToolResult execute(const std::string& paramsJson) override;

private:
    std::string workingDir_;

    static const int MaxOutputLength = 30000;
};

}
}
}
