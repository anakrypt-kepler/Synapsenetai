#pragma once

#include <string>

namespace synapse::web {
class RpcServer;
}

namespace synapse::rpc {

class RpcCommandHandlerProvider {
public:
    virtual ~RpcCommandHandlerProvider() = default;

    virtual std::string handleRpcPoeSubmit(const std::string& params) = 0;
    virtual std::string handleRpcPoeSubmitCode(const std::string& params) = 0;
    virtual std::string handleRpcPoeListCode(const std::string& params) = 0;
    virtual std::string handleRpcPoeFetchCode(const std::string& params) = 0;
    virtual std::string handleRpcPoeVote(const std::string& params) = 0;
    virtual std::string handleRpcPoeFinalize(const std::string& params) = 0;
    virtual std::string handleRpcPoeEpoch(const std::string& params) = 0;
    virtual std::string handleRpcPoeExport(const std::string& params) = 0;
    virtual std::string handleRpcPoeImport(const std::string& params) = 0;
    virtual std::string handleRpcWalletAddress(const std::string& params) = 0;
    virtual std::string handleRpcWalletBalance(const std::string& params) = 0;
    virtual std::string handleRpcModelStatus(const std::string& params) = 0;
    virtual std::string handleRpcModelList(const std::string& params) = 0;
    virtual std::string handleRpcModelLoad(const std::string& params) = 0;
    virtual std::string handleRpcModelUnload(const std::string& params) = 0;
    virtual std::string handleRpcModelAccessGet(const std::string& params) = 0;
    virtual std::string handleRpcModelAccessSet(const std::string& params) = 0;
    virtual std::string handleRpcMarketListings(const std::string& params) = 0;
    virtual std::string handleRpcMarketStats(const std::string& params) = 0;
    virtual std::string handleRpcModelRemoteList(const std::string& params) = 0;
    virtual std::string handleRpcModelRemoteRent(const std::string& params) = 0;
    virtual std::string handleRpcModelRemoteEnd(const std::string& params) = 0;
    virtual std::string handleRpcAiComplete(const std::string& params) = 0;
    virtual std::string handleRpcAiStop(const std::string& params) = 0;
    virtual std::string handleRpcPoeValidators(const std::string& params) = 0;
    virtual std::string handleRpcUpdateManifestSubmit(const std::string& params) = 0;
    virtual std::string handleRpcUpdateManifestFetch(const std::string& params) = 0;
    virtual std::string handleRpcUpdateManifestList(const std::string& params) = 0;
    virtual std::string handleRpcUpdateManifestApprove(const std::string& params) = 0;
    virtual std::string handleRpcUpdateManifestApprovals(const std::string& params) = 0;
    virtual std::string handleRpcUpdateInstallState(const std::string& params) = 0;
    virtual std::string handleRpcUpdateInstallPrepare(const std::string& params) = 0;
    virtual std::string handleRpcUpdateInstallAdvance(const std::string& params) = 0;
    virtual std::string handleRpcUpdateInstallCommit(const std::string& params) = 0;
    virtual std::string handleRpcUpdateInstallRollback(const std::string& params) = 0;
    virtual std::string handleRpcImplantUpdateState(const std::string& params) = 0;
    virtual std::string handleRpcImplantUpdatePrepare(const std::string& params) = 0;
    virtual std::string handleRpcImplantUpdateAdvance(const std::string& params) = 0;
    virtual std::string handleRpcImplantUpdateCommit(const std::string& params) = 0;
    virtual std::string handleRpcImplantUpdateRollback(const std::string& params) = 0;
    virtual std::string handleRpcNaanStatus(const std::string& params) = 0;
    virtual std::string handleRpcNaanObservatoryArtifacts(const std::string& params) = 0;
    virtual std::string handleRpcNaanObservatoryArtifactGet(const std::string& params) = 0;
    virtual std::string handleRpcNaanObservatoryDrafts(const std::string& params) = 0;
    virtual std::string handleRpcNaanObservatoryDraftGet(const std::string& params) = 0;
    virtual std::string handleRpcNaanPipelineDryRun(const std::string& params) = 0;
    virtual std::string handleRpcNaanPipelineDrain(const std::string& params) = 0;
    virtual std::string handleRpcNodeStatus(const std::string& params) = 0;
    virtual std::string handleRpcNodePeers(const std::string& params) = 0;
    virtual std::string handleRpcNodeLogs(const std::string& params) = 0;
    virtual std::string handleRpcNodeSeeds(const std::string& params) = 0;
    virtual std::string handleRpcNodeDiscoveryStats(const std::string& params) = 0;
    virtual std::string handleRpcNodeTorControl(const std::string& params) = 0;
};

void registerCoreRpcMethods(web::RpcServer& server, RpcCommandHandlerProvider& provider);

} // namespace synapse::rpc
