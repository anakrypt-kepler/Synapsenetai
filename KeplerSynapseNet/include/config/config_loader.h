#pragma once

#include "node/node_config.h"
#include <string>

// RPC cookie / basic-auth helpers. Cookie file lives under dataDir so a local
// desktop app can call RPC without putting a password in argv.

namespace synapse {

std::string resolveRpcCookiePath(const std::string& dataDir, const std::string& cookieFile);
std::string makeBasicAuthorizationValue(const std::string& user, const std::string& password);
std::string buildRpcClientAuthHeader(const NodeConfig& config);

} // namespace synapse
