#pragma once

#include "node/node_config.h"
#include <cstdint>
#include <string>

namespace synapse {

void ensureDirectories(const NodeConfig& config);
bool checkDiskSpace(const std::string& path, uint64_t requiredBytes);
bool checkSystemRequirements();
void registerSignalHandlers(void (*handler)(int));
void daemonize();

} // namespace synapse
