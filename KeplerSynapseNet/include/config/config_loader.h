#pragma once

#include "node/node_config.h"
#include <string>

namespace synapse {

std::string resolveRpcCookiePath(const std::string& dataDir, const std::string& cookieFile);
std::string makeBasicAuthorizationValue(const std::string& user, const std::string& password);
std::string buildRpcClientAuthHeader(const NodeConfig& config);

} // namespace synapse
