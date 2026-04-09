#pragma once

#include "tui/tui.h"

#include <cstdint>
#include <string>

namespace synapse::tui {

class TuiCommandHandlerProvider {
public:
    virtual ~TuiCommandHandlerProvider() = default;

    virtual uint64_t parseTuiAmountAtoms(const std::string& value) = 0;
    virtual std::string handleTuiSendCommand(const std::string& to, uint64_t amountAtoms) = 0;
    virtual std::string handleTuiPoeSubmit(const std::string& paramsJson) = 0;
    virtual std::string handleTuiPoeSubmitCode(const std::string& paramsJson) = 0;
    virtual std::string handleTuiPoeEpoch(const std::string& paramsJson) = 0;
};

void registerCoreTuiCommandHandler(TUI& ui, TuiCommandHandlerProvider& provider);

} // namespace synapse::tui
