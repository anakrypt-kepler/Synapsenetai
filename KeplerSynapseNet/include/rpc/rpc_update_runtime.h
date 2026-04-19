#pragma once

#include "core/implant_compatibility.h"
#include "crypto/crypto.h"
#include "rpc/rpc_update_views.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace synapse::rpc {

enum class RpcUpdateManifestAccept {
    Rejected = 0,
    Accepted = 1,
    Duplicate = 2,
};

struct RpcUpdateRuntimeInputs {
    std::function<RpcUpdateManifestAccept(const core::UpdateManifest&, bool, std::string*)>
        acceptUpdateManifest;
    std::function<nlohmann::json()> buildPolicyJson;
    std::function<RpcSecurityPolicyHashes()> buildPolicyHashes;
    std::function<nlohmann::json(const crypto::Hash256&)> buildDetachedSignerApprovalsJson;
    std::function<std::vector<core::UpdateManifest>(size_t)> listStoredUpdateManifests;
    std::function<bool(const crypto::Hash256&, core::UpdateManifest&)> fetchStoredUpdateManifest;
    std::function<void(const crypto::Hash256&, const core::DetachedSignerApproval&)>
        upsertDetachedSignerApproval;
    std::function<bool(const core::UpdateManifest&, std::string*)> validateUpdateSignerThreshold;
    std::function<void(
        uint64_t,
        const std::string&,
        const std::string&,
        const std::string&,
        const nlohmann::json&)> emitSecurityEvent;
    std::function<core::UpdateInstaller::UpdatePolicy(const core::UpdateManifest&)>
        installerPolicyForManifest;
    std::function<core::UpdateInstallerState()> readInstallerState;
    std::function<bool(
        const core::UpdateManifest&,
        const core::UpdateInstaller::UpdatePolicy&,
        std::string*,
        core::UpdateInstallerState*)> installManifest;
    std::function<bool(
        const crypto::Hash256&,
        std::string*,
        core::UpdateInstallerState*)> advanceRollout;
    std::function<bool(
        const crypto::Hash256&,
        std::string*,
        core::UpdateInstallerState*)> commitPending;
    std::function<bool(std::string*, core::UpdateInstallerState*)> rollback;
    std::function<bool(const core::UpdateManifest&, bool, std::string*)>
        validateImplantDistributionManifest;
    std::function<bool(const crypto::Hash256&, bool, bool, std::string*)> markImplantPrepare;
    std::function<bool(const crypto::Hash256&, bool, std::string*)> markImplantCanaryHealth;
    std::function<bool(const crypto::Hash256&, bool, std::string*)> markImplantWideHealth;
    std::function<bool(const crypto::Hash256&, std::string*)> canCommitImplant;
    std::function<std::optional<core::ImplantSafetyRecord>(const crypto::Hash256&)>
        getImplantSafetyRecord;
    std::function<bool(const crypto::Hash256&, std::string*)> clearImplantSafetyRecord;
};

std::string runRpcUpdateManifestSubmit(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateManifestFetch(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateManifestList(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateManifestApprove(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateManifestApprovals(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);

std::string runRpcUpdateInstallState(const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateInstallPrepare(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateInstallAdvance(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateInstallCommit(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcUpdateInstallRollback(const RpcUpdateRuntimeInputs& inputs);

std::string runRpcImplantUpdateState(const RpcUpdateRuntimeInputs& inputs);
std::string runRpcImplantUpdatePrepare(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcImplantUpdateAdvance(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcImplantUpdateCommit(
    const std::string& paramsJson,
    const RpcUpdateRuntimeInputs& inputs);
std::string runRpcImplantUpdateRollback(const RpcUpdateRuntimeInputs& inputs);

} // namespace synapse::rpc
