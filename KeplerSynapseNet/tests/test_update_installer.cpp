#include "core/update_installer.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

static std::string uniqueStatePath() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto base = std::filesystem::temp_directory_path() /
                ("synapsenet_update_installer_" + std::to_string(static_cast<unsigned long long>(now)) + ".state");
    return base.string();
}

static synapse::core::UpdateManifest makeManifest(const std::string& target, const std::string& seedA, const std::string& seedB) {
    auto keyPair = synapse::crypto::generateKeyPair();
    auto secondKeyPair = synapse::crypto::generateKeyPair();
    synapse::core::UpdateManifest manifest;
    synapse::core::UpdateChunk c1;
    c1.hash = synapse::crypto::sha256(seedA);
    c1.size = 1024;
    synapse::core::UpdateChunk c2;
    c2.hash = synapse::crypto::sha256(seedB);
    c2.size = 2048;
    manifest.chunks = {c1, c2};
    manifest.target = target;
    manifest.protocolMin = 1;
    manifest.protocolMax = 2;
    std::string reason;
    assert(synapse::core::signUpdateManifest(manifest, keyPair.privateKey, &reason));
    manifest.additionalSignatures.push_back({
        secondKeyPair.publicKey,
        synapse::crypto::sign(manifest.payloadHash(), secondKeyPair.privateKey)
    });
    return manifest;
}

static synapse::core::UpdateInstaller::UpdatePolicy makePolicy(const synapse::core::UpdateManifest& manifest) {
    synapse::core::UpdateInstaller::UpdatePolicy policy;
    policy.minSignatures = 1;
    policy.allowedSigners.push_back(manifest.signer);
    for (const auto& extra : manifest.additionalSignatures) {
        if (std::find(policy.allowedSigners.begin(), policy.allowedSigners.end(), extra.first) == policy.allowedSigners.end()) {
            policy.allowedSigners.push_back(extra.first);
        }
    }
    return policy;
}

static void testInstallAdvanceCommitAndRollback() {
    std::string statePath = uniqueStatePath();
    synapse::core::UpdateInstaller installer;
    std::string reason;
    assert(installer.open(statePath, &reason));

    auto first = makeManifest("implant/driver", "chunk_first_a", "chunk_first_b");
    auto firstPolicy = makePolicy(first);
    assert(installer.installManifest(first, firstPolicy, &reason));
    assert(installer.state().hasPending);
    assert(installer.state().pendingBundle == first.bundleId);
    assert(installer.state().pendingStage == synapse::core::UpdateRolloutStage::CANARY);

    assert(installer.advanceRollout(first.bundleId, &reason));
    assert(installer.state().pendingStage == synapse::core::UpdateRolloutStage::WIDE);
    assert(installer.advanceRollout(first.bundleId, &reason));
    assert(installer.state().pendingStage == synapse::core::UpdateRolloutStage::COMPLETE);

    assert(installer.commitPending(first.bundleId, &reason));
    assert(!installer.state().hasPending);
    assert(installer.state().hasSlotBBundle);
    assert(installer.state().slotBBundle == first.bundleId);

    auto second = makeManifest("implant/driver", "chunk_second_a", "chunk_second_b");
    auto secondPolicy = makePolicy(second);
    assert(installer.installManifest(second, secondPolicy, &reason));
    assert(installer.state().hasLastKnownGood);
    assert(installer.state().lastKnownGood == first.bundleId);

    assert(installer.advanceRollout(second.bundleId, &reason));
    assert(installer.advanceRollout(second.bundleId, &reason));
    assert(installer.commitPending(second.bundleId, &reason));
    assert(installer.state().activeSlot == 'A');

    assert(installer.rollback(&reason));
    assert(installer.state().activeSlot == 'B');
    assert(installer.state().hasSlotBBundle);
    assert(installer.state().slotBBundle == first.bundleId);

    std::filesystem::remove(statePath);
}

static void testPersistenceAndInvalidTransitions() {
    std::string statePath = uniqueStatePath();
    synapse::core::UpdateInstaller installer;
    std::string reason;
    assert(installer.open(statePath, &reason));

    auto manifest = makeManifest("implant/app", "chunk_invalid_a", "chunk_invalid_b");
    auto manifestPolicy = makePolicy(manifest);
    assert(installer.installManifest(manifest, manifestPolicy, &reason));

    auto wrong = makeManifest("implant/app", "chunk_wrong_a", "chunk_wrong_b");
    assert(!installer.advanceRollout(wrong.bundleId, &reason));
    assert(reason == "bundle_mismatch");
    assert(!installer.commitPending(wrong.bundleId, &reason));
    assert(reason == "bundle_mismatch");
    assert(!installer.commitPending(manifest.bundleId, &reason));
    assert(reason == "rollout_not_complete");

    synapse::core::UpdateInstaller reloaded;
    assert(reloaded.open(statePath, &reason));
    assert(reloaded.state().hasPending);
    assert(reloaded.state().pendingBundle == manifest.bundleId);
    assert(reloaded.state().pendingStage == synapse::core::UpdateRolloutStage::CANARY);

    assert(reloaded.rollback(&reason));
    assert(!reloaded.state().hasPending);
    assert(!reloaded.commitPending(manifest.bundleId, &reason));
    assert(reason == "no_pending_update");

    std::filesystem::remove(statePath);
}

int main() {
    testInstallAdvanceCommitAndRollback();
    testPersistenceAndInvalidTransitions();
    return 0;
}
