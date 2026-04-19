#pragma once

#include "ide/tools/tool_base.h"

#include <string>

namespace synapse {
namespace ide {
namespace tools {

class DownloadTool : public ITool {
public:
    explicit DownloadTool(const std::string& workingDir = ".");

    std::string name() const override;
    std::string description() const override;
    ToolResult execute(const std::string& paramsJson) override;

private:
    std::string workingDir_;
};

}
}
}
