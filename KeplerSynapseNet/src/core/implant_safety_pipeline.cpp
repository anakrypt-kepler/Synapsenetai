#include "core/implant_safety_pipeline.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace synapse::core {

namespace {

static constexpr uint32_t IMPLANT_SAFETY_STATE_VERSION = 1;

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

std::vector<std::string> splitComma(const std::string& value) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : value) {
        if (c == ',') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    return parts;
}

}

bool ImplantSafetyPipeline::open(const std::string& statePath, std::string* reason) {
    if (statePath.empty()) {
        if (reason) *reason = "empty_state_path";
        return false;
    }
    statePath_ = statePath;
    return loadState(reason);
}

bool ImplantSafetyPipeline::markPrepare(
    const crypto::Hash256& bundleId,
    bool deterministicTestsPassed,
    bool sandboxBoundariesPassed,
    std::string* reason
) {
    if (!deterministicTestsPassed) {
        if (reason) *reason = "deterministic_tests_failed";
        return false;
    }
    if (!sandboxBoundariesPassed) {
        if (reason) *reason = "sandbox_boundaries_failed";
        return false;
    }

    std::string idHex = crypto::toHex(bundleId);
    ImplantSafetyRecord rec{};
    rec.deterministicTestsPassed = true;
    rec.sandboxBoundariesPassed = true;
    rec.canaryHealthPassed = false;
    rec.wideHealthPassed = false;
    rec.updatedAt = static_cast<uint64_t>(std::time(nullptr));
    recordsByBundleId_[idHex] = rec;

    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = "prepare_passed";
    return true;
}

bool ImplantSafetyPipeline::markCanaryHealth(
    const crypto::Hash256& bundleId,
    bool canaryHealthPassed,
    std::string* reason
) {
    std::string idHex = crypto::toHex(bundleId);
    auto it = recordsByBundleId_.find(idHex);
    if (it == recordsByBundleId_.end() || !it->second.deterministicTestsPassed || !it->second.sandboxBoundariesPassed) {
        if (reason) *reason = "prepare_not_passed";
        return false;
    }
    if (!canaryHealthPassed) {
        if (reason) *reason = "canary_health_failed";
        return false;
    }

    it->second.canaryHealthPassed = true;
    it->second.updatedAt = static_cast<uint64_t>(std::time(nullptr));
    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = "canary_passed";
    return true;
}

bool ImplantSafetyPipeline::markWideHealth(
    const crypto::Hash256& bundleId,
    bool wideHealthPassed,
    std::string* reason
) {
    std::string idHex = crypto::toHex(bundleId);
    auto it = recordsByBundleId_.find(idHex);
    if (it == recordsByBundleId_.end() || !it->second.deterministicTestsPassed || !it->second.sandboxBoundariesPassed) {
        if (reason) *reason = "prepare_not_passed";
        return false;
    }
    if (!it->second.canaryHealthPassed) {
        if (reason) *reason = "canary_not_passed";
        return false;
    }
    if (!wideHealthPassed) {
        if (reason) *reason = "wide_health_failed";
        return false;
    }

    it->second.wideHealthPassed = true;
    it->second.updatedAt = static_cast<uint64_t>(std::time(nullptr));
    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = "wide_passed";
    return true;
}

bool ImplantSafetyPipeline::canCommit(const crypto::Hash256& bundleId, std::string* reason) const {
    std::string idHex = crypto::toHex(bundleId);
    auto it = recordsByBundleId_.find(idHex);
    if (it == recordsByBundleId_.end()) {
        if (reason) *reason = "safety_record_missing";
        return false;
    }
    if (!it->second.deterministicTestsPassed || !it->second.sandboxBoundariesPassed) {
        if (reason) *reason = "prepare_not_passed";
        return false;
    }
    if (!it->second.canaryHealthPassed) {
        if (reason) *reason = "canary_not_passed";
        return false;
    }
    if (!it->second.wideHealthPassed) {
        if (reason) *reason = "wide_not_passed";
        return false;
    }
    if (reason) *reason = "ok";
    return true;
}

std::optional<ImplantSafetyRecord> ImplantSafetyPipeline::getRecord(const crypto::Hash256& bundleId) const {
    std::string idHex = crypto::toHex(bundleId);
    auto it = recordsByBundleId_.find(idHex);
    if (it == recordsByBundleId_.end()) return std::nullopt;
    return it->second;
}

bool ImplantSafetyPipeline::clearRecord(const crypto::Hash256& bundleId, std::string* reason) {
    std::string idHex = crypto::toHex(bundleId);
    recordsByBundleId_.erase(idHex);
    if (!saveStateAtomic(reason)) return false;
    if (reason) *reason = "cleared";
    return true;
}

bool ImplantSafetyPipeline::loadState(std::string* reason) {
    std::ifstream in(statePath_);
    if (!in.good()) {
        recordsByBundleId_.clear();
        if (!saveStateAtomic(reason)) return false;
        if (reason) *reason = "created_default_state";
        return true;
    }

    uint32_t fileVersion = 0;
    bool hasVersion = false;
    std::unordered_map<std::string, ImplantSafetyRecord> loaded;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("version=", 0) == 0) {
            try {
                fileVersion = static_cast<uint32_t>(std::stoul(line.substr(8)));
                hasVersion = true;
            } catch (...) {
                if (reason) *reason = "invalid_version";
                return false;
            }
            continue;
        }
        if (line.rfind("record=", 0) != 0) continue;

        auto parts = splitComma(line.substr(7));
        if (parts.size() != 6) {
            if (reason) *reason = "invalid_record_format";
            return false;
        }

        crypto::Hash256 id{};
        auto idBytes = crypto::fromHex(parts[0]);
        if (idBytes.size() != id.size()) {
            if (reason) *reason = "invalid_record_bundle_id";
            return false;
        }

        bool deterministicTestsPassed = false;
        bool sandboxBoundariesPassed = false;
        bool canaryHealthPassed = false;
        bool wideHealthPassed = false;
        if (!parseBool(parts[1], deterministicTestsPassed)) {
            if (reason) *reason = "invalid_record_deterministic";
            return false;
        }
        if (!parseBool(parts[2], sandboxBoundariesPassed)) {
            if (reason) *reason = "invalid_record_sandbox";
            return false;
        }
        if (!parseBool(parts[3], canaryHealthPassed)) {
            if (reason) *reason = "invalid_record_canary";
            return false;
        }
        if (!parseBool(parts[4], wideHealthPassed)) {
            if (reason) *reason = "invalid_record_wide";
            return false;
        }

        uint64_t updatedAt = 0;
        try {
            updatedAt = static_cast<uint64_t>(std::stoull(parts[5]));
        } catch (...) {
            if (reason) *reason = "invalid_record_updated_at";
            return false;
        }

        ImplantSafetyRecord rec{};
        rec.deterministicTestsPassed = deterministicTestsPassed;
        rec.sandboxBoundariesPassed = sandboxBoundariesPassed;
        rec.canaryHealthPassed = canaryHealthPassed;
        rec.wideHealthPassed = wideHealthPassed;
        rec.updatedAt = updatedAt;
        loaded[parts[0]] = rec;
    }

    if (!hasVersion || fileVersion != IMPLANT_SAFETY_STATE_VERSION) {
        if (reason) *reason = "unsupported_state_version";
        return false;
    }

    recordsByBundleId_ = std::move(loaded);
    if (reason) *reason = "loaded";
    return true;
}

bool ImplantSafetyPipeline::saveStateAtomic(std::string* reason) const {
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

        out << "version=" << IMPLANT_SAFETY_STATE_VERSION << "\n";

        std::vector<std::string> bundleIds;
        bundleIds.reserve(recordsByBundleId_.size());
        for (const auto& [id, _] : recordsByBundleId_) bundleIds.push_back(id);
        std::sort(bundleIds.begin(), bundleIds.end());

        for (const auto& id : bundleIds) {
            const auto& rec = recordsByBundleId_.at(id);
            out << "record=" << id
                << "," << boolToString(rec.deterministicTestsPassed)
                << "," << boolToString(rec.sandboxBoundariesPassed)
                << "," << boolToString(rec.canaryHealthPassed)
                << "," << boolToString(rec.wideHealthPassed)
                << "," << rec.updatedAt
                << "\n";
        }

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
