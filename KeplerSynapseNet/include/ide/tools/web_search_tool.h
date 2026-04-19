#pragma once

#include "ide/tools/tool_base.h"

#include <string>

namespace synapse {
namespace ide {
namespace tools {

class WebSearchTool : public ITool {
public:
    WebSearchTool();

    std::string name() const override;
    std::string description() const override;
    ToolResult execute(const std::string& paramsJson) override;
};

}
}
}
