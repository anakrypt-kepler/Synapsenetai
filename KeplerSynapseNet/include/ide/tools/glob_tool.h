#pragma once

#include "ide/tools/tool_base.h"

#include <string>

namespace synapse {
namespace ide {
namespace tools {

class GlobTool : public ITool {
public:
    explicit GlobTool(const std::string& workingDir = ".");

    std::string name() const override;
    std::string description() const override;
    ToolResult execute(const std::string& paramsJson) override;

private:
    std::string workingDir_;
};

}
}
}
