#include "core/update_installer.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace synapse::core {

namespace {

static constexpr uint32_t INSTALLER_STATE_VERSION = 1;

bool isValidSlot(char slot) {
    return slot == 'A' || slot == 'B';
}

char oppositeSlot(char slot) {
    return slot == 'A' ? 'B' : 'A';
}

bool slotBundleRef(UpdateInstallerState& state, char slot, bool*& hasBundle, crypto::Hash256*& bundle) {
    if (slot == 'A') {
        hasBundle = &state.hasSlotABundle;
        bundle = &state.slotABundle;
        return true;
    }
    if (slot == 'B') {
        hasBundle = &state.hasSlotBBundle;
        bundle = &state.slotBBundle;
        return true;
    }
    return false;
}

bool slotBundleRef(const UpdateInstallerState& state, char slot, const bool*& hasBundle, const crypto::Hash256*& bundle) {
    if (slot == 'A') {
        hasBundle = &state.hasSlotABundle;
        bundle = &state.slotABundle;
        return true;
    }
    if (slot == 'B') {
        hasBundle = &state.hasSlotBBundle;
        bundle = &state.slotBBundle;
        return true;
    }
    return false;
}

bool parseBool(const std::string& value, bool& out) {
    if (value == "1" || value == "true") {
        out = true;
        return true;
    }
    if (value == "0" || value == "false") {
        out = false;
        return true;
    }
    return false;
}

std::string boolToString(bool value) {
    return value ? "1" : "0";
}

bool parseHashHex(const std::string& value, crypto::Hash256& out) {
    auto bytes = crypto::fromHex(value);
    if (bytes.size() != out.size()) return false;
    std::memcpy(out.data(), bytes.data(), out.size());
    return true;
}

std::string trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

}

std::string toString(UpdateRolloutStage stage) {
    switch (stage) {
        case UpdateRolloutStage::CANARY: return "canary";
        case UpdateRolloutStage::WIDE: return "wide";
        case UpdateRolloutStage::COMPLETE: return "complete";
    }
    return "canary";
}

bool parseUpdateRolloutStage(const std::string& value, UpdateRolloutStage& out) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "canary") {
        out = UpdateRolloutStage::CANARY;
        return true;
    }
    if (lower == "wide") {
        out = UpdateRolloutStage::WIDE;
        return true;
    }
    if (lower == "complete") {
        out = UpdateRolloutStage::COMPLETE;
        return true;
    }
    return false;
}

bool UpdateInstaller::open(const std::string& statePath, std::string* reason) {
    if (statePath.empty()) {
        if (reason) *reason = "empty_state_path";
        return false;
    }
    statePath_ = statePath;
    return loadState(reason);
}

const UpdateInstallerState& UpdateInstaller::state() const {
    return state_;
}

bool UpdateInstaller::installManifest(const UpdateManifest& manifest, const UpdatePolicy& policy, std::string* reason) {
    std::string verifyReason;
    if (!manifest.validateStrict(&verifyReason)) {
        if (reason) *reason = verifyReason;
        return false;
    }
    // path traversal checks
    if (manifest.target.find("..") != std::string::npos) {
        if (reason) *reason = "path_traversal";
        return false;
    }
    // URL-encoded path traversal checks
    if (manifest.target.find("%2e") != std::string::npos ||
        manifest.target.find("%2E") != std::string::npos ||
        manifest.target.find("%00") != std::string::npos) {
        if (reason) *reason = "encoded_path_traversal";
        return false;
    }
    // Null byte injection check
    if (manifest.target.find('\0') != std::string::npos) {
        if (reason) *reason = "null_byte_in_path";
        return false;
    }
    if (!manifest.target.empty() && (manifest.target[0] == '/' || manifest.target[0] == '\\')) {
        if (reason) *reason = "absolute_path";
        return false;
    }
    bool validPrefix = false;
    const std::vector<std::string> allowedPrefixes = {"implant/", "node/", "model/"};
    for (const auto& p : allowedPrefixes) {
        if (manifest.target.rfind(p, 0) == 0) { validPrefix = true; break; }
    }
    if (!validPrefix) {
        if (reason) *reason = "invalid_target_prefix";
        return false;
    }

    // signature policy checks
    if (policy.allowedSigners.empty()) {
        if (reason) *reason = "no_allowed_signers";
        return false;
    }
    // collect all signatures from manifest
    std::vector<std::pair<crypto::PublicKey, crypto::Signature>> sigs;
    sigs.emplace_back(manifest.signer, manifest.signature);
    for (const auto& p : manifest.additionalSignatures) sigs.push_back(p);

    uint32_t validCount = 0;
    for (const auto& pr : sigs) {
        const auto& pub = pr.first;
        const auto& sig = pr.second;
        bool allowed = false;
        for (const auto& allowedPub : policy.allowedSigners) {
            if (allowedPub == pub) { allowed = true; break; }
        }
        if (!allowed) continue;
        if (crypto::verify(manifest.payloadHash(), sig, pub)) {
            validCount++;
        }
    }
    if (validCount < policy.minSignatures) {
        if (reason) *reason = "insufficient_signatures";
        return false;
    }
    if (manifest.target.rfind("implant/", 0) == 0 && validCount < 2) {
        if (reason) *reason = "implant_requires_2_signatures";
        return false;
    }
    if (!isValidSlot(state_.activeSlot)) {
        if (reason) *reason = "invalid_active_slot";
        return false;
    }
    if (state_.hasPending) {
        if (reason) *reason = "pending_update_exists";
        return false;
    }

    const bool* activeHasBundle = nullptr;
    const crypto::Hash256* activeBundle = nullptr;
    if (!slotBundleRef(state_, state_.activeSlot, activeHasBundle, activeBundle)) {
        if (reason) *reason = "invalid_active_slot";
        return false;
    }
    if (*activeHasBundle) {
        state_.hasLastKnownGood = true;
        state_.lastKnownGood = *activeBundle;
    }

    char targetSlot = oppositeSlot(state_.activeSlot);
    bool* hasTargetBundle = nullptr;
    crypto::Hash256* targetBundle = nullptr;
    if (!slotBundleRef(state_, targetSlot, hasTargetBundle, targetBundle)) {
        if (reason) *reason = "invalid_target_slot";
        return false;
    }

    *hasTargetBundle = true;
    *targetBundle = manifest.bundleId;
    state_.hasPending = true;
    state_.pendingSlot = targetSlot;
    state_.pendingBundle = manifest.bundleId;
    state_.pendingStage = UpdateRolloutStage::CANARY;

    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = "pending_canary";
    return true;
}

bool UpdateInstaller::advanceRollout(const crypto::Hash256& bundleId, std::string* reason) {
    if (!state_.hasPending) {
        if (reason) *reason = "no_pending_update";
        return false;
    }
    if (state_.pendingBundle != bundleId) {
        if (reason) *reason = "bundle_mismatch";
        return false;
    }

    if (state_.pendingStage == UpdateRolloutStage::CANARY) {
        state_.pendingStage = UpdateRolloutStage::WIDE;
    } else if (state_.pendingStage == UpdateRolloutStage::WIDE) {
        state_.pendingStage = UpdateRolloutStage::COMPLETE;
    } else {
        if (reason) *reason = "already_complete";
        return false;
    }

    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = toString(state_.pendingStage);
    return true;
}

bool UpdateInstaller::commitPending(const crypto::Hash256& bundleId, std::string* reason) {
    if (!state_.hasPending) {
        if (reason) *reason = "no_pending_update";
        return false;
    }
    if (state_.pendingBundle != bundleId) {
        if (reason) *reason = "bundle_mismatch";
        return false;
    }
    if (state_.pendingStage != UpdateRolloutStage::COMPLETE) {
        if (reason) *reason = "rollout_not_complete";
        return false;
    }

    const bool* activeHasBundle = nullptr;
    const crypto::Hash256* activeBundle = nullptr;
    if (!slotBundleRef(state_, state_.activeSlot, activeHasBundle, activeBundle)) {
        if (reason) *reason = "invalid_active_slot";
        return false;
    }
    if (*activeHasBundle) {
        state_.hasLastKnownGood = true;
        state_.lastKnownGood = *activeBundle;
    }

    const bool* pendingHasBundle = nullptr;
    const crypto::Hash256* pendingBundle = nullptr;
    if (!slotBundleRef(state_, state_.pendingSlot, pendingHasBundle, pendingBundle) || !*pendingHasBundle || *pendingBundle != bundleId) {
        if (reason) *reason = "pending_slot_mismatch";
        return false;
    }

    state_.activeSlot = state_.pendingSlot;
    state_.hasPending = false;
    state_.pendingSlot = 'A';
    state_.pendingBundle = crypto::Hash256{};
    state_.pendingStage = UpdateRolloutStage::CANARY;

    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = "committed";
    return true;
}

bool UpdateInstaller::rollback(std::string* reason) {
    if (state_.hasPending) {
        state_.hasPending = false;
        state_.pendingSlot = 'A';
        state_.pendingBundle = crypto::Hash256{};
        state_.pendingStage = UpdateRolloutStage::CANARY;
        if (!saveStateAtomic(reason)) return false;
        if (reason) *reason = "pending_rolled_back";
        return true;
    }

    if (!state_.hasLastKnownGood) {
        if (reason) *reason = "no_last_known_good";
        return false;
    }

    const bool* slotAHasBundle = nullptr;
    const bool* slotBHasBundle = nullptr;
    const crypto::Hash256* slotABundle = nullptr;
    const crypto::Hash256* slotBBundle = nullptr;
    slotBundleRef(state_, 'A', slotAHasBundle, slotABundle);
    slotBundleRef(state_, 'B', slotBHasBundle, slotBBundle);

    if (*slotAHasBundle && *slotABundle == state_.lastKnownGood) {
        state_.activeSlot = 'A';
    } else if (*slotBHasBundle && *slotBBundle == state_.lastKnownGood) {
        state_.activeSlot = 'B';
    } else {
        if (reason) *reason = "last_known_good_missing";
        return false;
    }

    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = "rolled_back";
    return true;
}

bool UpdateInstaller::loadState(std::string* reason) {
    std::ifstream in(statePath_);
    if (!in.good()) {
        state_ = UpdateInstallerState{};
        state_.activeSlot = 'A';
        if (!saveStateAtomic(reason)) return false;
        if (reason) *reason = "created_default_state";
        return true;
    }

    UpdateInstallerState loaded;
    loaded.activeSlot = 'A';
    loaded.pendingSlot = 'A';
    loaded.pendingStage = UpdateRolloutStage::CANARY;

    uint32_t fileVersion = 0;
    bool hasVersion = false;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        if (key == "version") {
            try {
                fileVersion = static_cast<uint32_t>(std::stoul(value));
                hasVersion = true;
            } catch (...) {
                if (reason) *reason = "invalid_version";
                return false;
            }
        } else if (key == "active_slot") {
            if (value.size() != 1 || !isValidSlot(value[0])) {
                if (reason) *reason = "invalid_active_slot";
                return false;
            }
            loaded.activeSlot = value[0];
        } else if (key == "slot_a_has") {
            if (!parseBool(value, loaded.hasSlotABundle)) {
                if (reason) *reason = "invalid_slot_a_has";
                return false;
            }
        } else if (key == "slot_b_has") {
            if (!parseBool(value, loaded.hasSlotBBundle)) {
                if (reason) *reason = "invalid_slot_b_has";
                return false;
            }
        } else if (key == "slot_a_bundle") {
            if (!value.empty() && !parseHashHex(value, loaded.slotABundle)) {
                if (reason) *reason = "invalid_slot_a_bundle";
                return false;
            }
        } else if (key == "slot_b_bundle") {
            if (!value.empty() && !parseHashHex(value, loaded.slotBBundle)) {
                if (reason) *reason = "invalid_slot_b_bundle";
                return false;
            }
        } else if (key == "pending_has") {
            if (!parseBool(value, loaded.hasPending)) {
                if (reason) *reason = "invalid_pending_has";
                return false;
            }
        } else if (key == "pending_slot") {
            if (value.size() != 1 || !isValidSlot(value[0])) {
                if (reason) *reason = "invalid_pending_slot";
                return false;
            }
            loaded.pendingSlot = value[0];
        } else if (key == "pending_bundle") {
            if (!value.empty() && !parseHashHex(value, loaded.pendingBundle)) {
                if (reason) *reason = "invalid_pending_bundle";
                return false;
            }
        } else if (key == "pending_stage") {
            if (!parseUpdateRolloutStage(value, loaded.pendingStage)) {
                if (reason) *reason = "invalid_pending_stage";
                return false;
            }
        } else if (key == "lkg_has") {
            if (!parseBool(value, loaded.hasLastKnownGood)) {
                if (reason) *reason = "invalid_lkg_has";
                return false;
            }
        } else if (key == "lkg_bundle") {
            if (!value.empty() && !parseHashHex(value, loaded.lastKnownGood)) {
                if (reason) *reason = "invalid_lkg_bundle";
                return false;
            }
        }
    }

    if (!hasVersion || fileVersion != INSTALLER_STATE_VERSION) {
        if (reason) *reason = "unsupported_state_version";
        return false;
    }
    if (!isValidSlot(loaded.activeSlot)) {
        if (reason) *reason = "invalid_active_slot";
        return false;
    }
    if (loaded.hasPending && !isValidSlot(loaded.pendingSlot)) {
        if (reason) *reason = "invalid_pending_slot";
        return false;
    }

    state_ = loaded;
    if (reason) *reason = "loaded";
    return true;
}

bool UpdateInstaller::saveStateAtomic(std::string* reason) const {
    std::filesystem::path path(statePath_);
    std::filesystem::path dir = path.parent_path();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            if (reason) *reason = "create_dir_failed";
            return false;
        }
    }

    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out.is_open()) {
            if (reason) *reason = "open_tmp_failed";
            return false;
        }

        out << "version=" << INSTALLER_STATE_VERSION << "\n";
        out << "active_slot=" << state_.activeSlot << "\n";
        out << "slot_a_has=" << boolToString(state_.hasSlotABundle) << "\n";
        out << "slot_b_has=" << boolToString(state_.hasSlotBBundle) << "\n";
        out << "slot_a_bundle=" << (state_.hasSlotABundle ? crypto::toHex(state_.slotABundle) : "") << "\n";
        out << "slot_b_bundle=" << (state_.hasSlotBBundle ? crypto::toHex(state_.slotBBundle) : "") << "\n";
        out << "pending_has=" << boolToString(state_.hasPending) << "\n";
        out << "pending_slot=" << state_.pendingSlot << "\n";
        out << "pending_bundle=" << (state_.hasPending ? crypto::toHex(state_.pendingBundle) : "") << "\n";
        out << "pending_stage=" << toString(state_.pendingStage) << "\n";
        out << "lkg_has=" << boolToString(state_.hasLastKnownGood) << "\n";
        out << "lkg_bundle=" << (state_.hasLastKnownGood ? crypto::toHex(state_.lastKnownGood) : "") << "\n";
        out.flush();
        if (!out.good()) {
            if (reason) *reason = "write_tmp_failed";
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec) return true;

    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        if (reason) *reason = "rename_tmp_failed";
        return false;
    }
    return true;
}

}
