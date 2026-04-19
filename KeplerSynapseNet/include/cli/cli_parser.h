#pragma once

#include "node/node_config.h"

namespace synapse {

void printHelp(const char* progName);
void printVersion();
bool parseArgs(int argc, char* argv[], NodeConfig& config);

} // namespace synapse
