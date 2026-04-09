#pragma once

#include "node/node_config.h"
#include <optional>

namespace synapse {
namespace rpc {
class RpcCommandHandlerProvider;
}

std::optional<int> runCliViaRpc(const NodeConfig& config);
int runCliLocally(const NodeConfig& config, rpc::RpcCommandHandlerProvider& provider);

} // namespace synapse
