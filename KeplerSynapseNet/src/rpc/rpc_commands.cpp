#include "rpc/rpc_commands.h"

#include "web/rpc_server.h"

namespace synapse::rpc {

namespace {

using RpcHandler = std::string (RpcCommandHandlerProvider::*)(const std::string&);

struct RpcMethodSpec {
    const char* name;
    RpcHandler handler;
    bool requiresAuth;
    int rateLimit;
};

constexpr RpcMethodSpec kRpcMethodSpecs[] = {
    {"poe.submit", &RpcCommandHandlerProvider::handleRpcPoeSubmit, true, 60},
    {"poe.submit_code", &RpcCommandHandlerProvider::handleRpcPoeSubmitCode, true, 60},
    {"poe.list_code", &RpcCommandHandlerProvider::handleRpcPoeListCode, false, 300},
    {"poe.fetch_code", &RpcCommandHandlerProvider::handleRpcPoeFetchCode, false, 300},
    {"poe.vote", &RpcCommandHandlerProvider::handleRpcPoeVote, true, 120},
    {"poe.finalize", &RpcCommandHandlerProvider::handleRpcPoeFinalize, true, 30},
    {"poe.epoch", &RpcCommandHandlerProvider::handleRpcPoeEpoch, false, 200},
    {"poe.export", &RpcCommandHandlerProvider::handleRpcPoeExport, true, 10},
    {"poe.import", &RpcCommandHandlerProvider::handleRpcPoeImport, true, 10},
    {"wallet.address", &RpcCommandHandlerProvider::handleRpcWalletAddress, true, 300},
    {"wallet.balance", &RpcCommandHandlerProvider::handleRpcWalletBalance, true, 300},
    {"model.status", &RpcCommandHandlerProvider::handleRpcModelStatus, false, 300},
    {"model.list", &RpcCommandHandlerProvider::handleRpcModelList, false, 300},
    {"model.load", &RpcCommandHandlerProvider::handleRpcModelLoad, true, 10},
    {"model.unload", &RpcCommandHandlerProvider::handleRpcModelUnload, true, 10},
    {"model.access.get", &RpcCommandHandlerProvider::handleRpcModelAccessGet, false, 300},
    {"model.access.set", &RpcCommandHandlerProvider::handleRpcModelAccessSet, true, 60},
    {"market.listings", &RpcCommandHandlerProvider::handleRpcMarketListings, false, 300},
    {"market.stats", &RpcCommandHandlerProvider::handleRpcMarketStats, false, 300},
    {"model.remote.list", &RpcCommandHandlerProvider::handleRpcModelRemoteList, false, 300},
    {"model.remote.rent", &RpcCommandHandlerProvider::handleRpcModelRemoteRent, true, 30},
    {"model.remote.end", &RpcCommandHandlerProvider::handleRpcModelRemoteEnd, true, 30},
    {"ai.complete", &RpcCommandHandlerProvider::handleRpcAiComplete, true, 60},
    {"ai.stop", &RpcCommandHandlerProvider::handleRpcAiStop, true, 60},
    {"poe.validators", &RpcCommandHandlerProvider::handleRpcPoeValidators, false, 300},
    {"update.manifest.submit", &RpcCommandHandlerProvider::handleRpcUpdateManifestSubmit, true, 30},
    {"update.manifest.fetch", &RpcCommandHandlerProvider::handleRpcUpdateManifestFetch, false, 300},
    {"update.manifest.list", &RpcCommandHandlerProvider::handleRpcUpdateManifestList, false, 300},
    {"update.manifest.approve", &RpcCommandHandlerProvider::handleRpcUpdateManifestApprove, true, 30},
    {"update.manifest.approvals", &RpcCommandHandlerProvider::handleRpcUpdateManifestApprovals, false, 300},
    {"update.install.state", &RpcCommandHandlerProvider::handleRpcUpdateInstallState, false, 300},
    {"update.install.prepare", &RpcCommandHandlerProvider::handleRpcUpdateInstallPrepare, true, 10},
    {"update.install.advance", &RpcCommandHandlerProvider::handleRpcUpdateInstallAdvance, true, 10},
    {"update.install.commit", &RpcCommandHandlerProvider::handleRpcUpdateInstallCommit, true, 10},
    {"update.install.rollback", &RpcCommandHandlerProvider::handleRpcUpdateInstallRollback, true, 10},
    {"implant.update.state", &RpcCommandHandlerProvider::handleRpcImplantUpdateState, false, 300},
    {"implant.update.prepare", &RpcCommandHandlerProvider::handleRpcImplantUpdatePrepare, true, 10},
    {"implant.update.advance", &RpcCommandHandlerProvider::handleRpcImplantUpdateAdvance, true, 10},
    {"implant.update.commit", &RpcCommandHandlerProvider::handleRpcImplantUpdateCommit, true, 10},
    {"implant.update.rollback", &RpcCommandHandlerProvider::handleRpcImplantUpdateRollback, true, 10},
    {"naan.status", &RpcCommandHandlerProvider::handleRpcNaanStatus, false, 300},
    {"naan.observatory.artifacts", &RpcCommandHandlerProvider::handleRpcNaanObservatoryArtifacts, false, 300},
    {"naan.observatory.artifact.get", &RpcCommandHandlerProvider::handleRpcNaanObservatoryArtifactGet, false, 300},
    {"naan.observatory.drafts", &RpcCommandHandlerProvider::handleRpcNaanObservatoryDrafts, false, 300},
    {"naan.observatory.draft.get", &RpcCommandHandlerProvider::handleRpcNaanObservatoryDraftGet, false, 300},
    {"naan.pipeline.dryrun", &RpcCommandHandlerProvider::handleRpcNaanPipelineDryRun, true, 30},
    {"naan.pipeline.drain", &RpcCommandHandlerProvider::handleRpcNaanPipelineDrain, true, 10},
    {"node.status", &RpcCommandHandlerProvider::handleRpcNodeStatus, false, 300},
    {"node.peers", &RpcCommandHandlerProvider::handleRpcNodePeers, false, 300},
    {"node.logs", &RpcCommandHandlerProvider::handleRpcNodeLogs, true, 120},
    {"node.seeds", &RpcCommandHandlerProvider::handleRpcNodeSeeds, false, 300},
    {"node.discovery.stats", &RpcCommandHandlerProvider::handleRpcNodeDiscoveryStats, false, 300},
    {"node.tor.control", &RpcCommandHandlerProvider::handleRpcNodeTorControl, true, 30},
};

} // namespace

void registerCoreRpcMethods(web::RpcServer& server, RpcCommandHandlerProvider& provider) {
    for (const auto& spec : kRpcMethodSpecs) {
        server.registerMethod(
            spec.name,
            [&provider, spec](const std::string& params) {
                return (provider.*(spec.handler))(params);
            },
            spec.requiresAuth,
            spec.rateLimit);
    }
}

} // namespace synapse::rpc
