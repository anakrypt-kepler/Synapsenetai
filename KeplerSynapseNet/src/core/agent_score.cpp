#include "core/agent_score.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace synapse::core {

namespace {

int64_t clampI64(int64_t v, int64_t lo, int64_t hi) {
    return std::max(lo, std::min(v, hi));
}

int64_t floorMulRatio(int64_t value, uint32_t numerator, uint32_t denominator) {
    if (denominator == 0) return value;
    const __int128 scaled = static_cast<__int128>(value) * static_cast<__int128>(numerator);
    if (scaled >= 0) {
        return static_cast<int64_t>(scaled / denominator);
    }
    const __int128 pos = -scaled;
    const __int128 q = (pos + denominator - 1) / denominator;
    return -static_cast<int64_t>(q);
}

uint64_t clampU64(uint64_t value, uint64_t minValue, uint64_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

AgentScorePolicy sanitizePolicy(const AgentScorePolicy& in) {
    AgentScorePolicy policy = in;
    if (policy.scoreMax < policy.scoreMin) {
        std::swap(policy.scoreMax, policy.scoreMin);
    }
    policy.initialScore = clampI64(policy.initialScore, policy.scoreMin, policy.scoreMax);

    if (policy.decayDenominator == 0) {
        policy.decayDenominator = 100;
        policy.decayNumerator = 95;
    }
    if (policy.decayNumerator > policy.decayDenominator) {
        policy.decayNumerator = policy.decayDenominator;
    }

    policy.throttledBelowOrEqual = clampI64(policy.throttledBelowOrEqual, policy.scoreMin, policy.scoreMax);
    policy.reviewOnlyBelowOrEqual = clampI64(policy.reviewOnlyBelowOrEqual, policy.scoreMin, policy.scoreMax);
    policy.localDraftOnlyBelowOrEqual = clampI64(policy.localDraftOnlyBelowOrEqual, policy.scoreMin, policy.scoreMax);
    policy.localDraftRecoveryAbove = clampI64(policy.localDraftRecoveryAbove, policy.scoreMin, policy.scoreMax);

    if (policy.reviewOnlyBelowOrEqual > policy.throttledBelowOrEqual) {
        policy.reviewOnlyBelowOrEqual = policy.throttledBelowOrEqual;
    }
    if (policy.localDraftOnlyBelowOrEqual > policy.reviewOnlyBelowOrEqual) {
        policy.localDraftOnlyBelowOrEqual = policy.reviewOnlyBelowOrEqual;
    }
    if (policy.localDraftRecoveryAbove < policy.localDraftOnlyBelowOrEqual) {
        policy.localDraftRecoveryAbove = policy.localDraftOnlyBelowOrEqual;
    }

    policy.localDraftRecoveryCleanSteps = static_cast<uint32_t>(
        clampU64(policy.localDraftRecoveryCleanSteps, 0, 1000000));
    return policy;
}

bool parseI64(const std::string& token, int64_t* out) {
    if (!out || token.empty()) return false;
    size_t i = 0;
    bool negative = false;
    if (token[0] == '-') {
        negative = true;
        i = 1;
    } else if (token[0] == '+') {
        i = 1;
    }
    if (i >= token.size()) return false;

    uint64_t acc = 0;
    for (; i < token.size(); ++i) {
        char c = token[i];
        if (c < '0' || c > '9') return false;
        uint64_t digit = static_cast<uint64_t>(c - '0');
        if (acc > (std::numeric_limits<uint64_t>::max() - digit) / 10ULL) return false;
        acc = acc * 10ULL + digit;
    }

    if (negative) {
        const uint64_t maxMag = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ULL;
        if (acc > maxMag) return false;
        if (acc == maxMag) {
            *out = std::numeric_limits<int64_t>::min();
        } else {
            *out = -static_cast<int64_t>(acc);
        }
        return true;
    }

    if (acc > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return false;
    *out = static_cast<int64_t>(acc);
    return true;
}

bool parseU64(const std::string& token, uint64_t* out) {
    if (!out || token.empty()) return false;
    uint64_t acc = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
        uint64_t digit = static_cast<uint64_t>(c - '0');
        if (acc > (std::numeric_limits<uint64_t>::max() - digit) / 10ULL) return false;
        acc = acc * 10ULL + digit;
    }
    *out = acc;
    return true;
}

bool parseU32(const std::string& token, uint32_t* out) {
    uint64_t v = 0;
    if (!parseU64(token, &v)) return false;
    if (v > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

} // namespace

std::string agentScoreBandToString(AgentScoreBand band) {
    switch (band) {
        case AgentScoreBand::FULL: return "FULL";
        case AgentScoreBand::THROTTLED: return "THROTTLED";
        case AgentScoreBand::REVIEW_ONLY: return "REVIEW_ONLY";
        case AgentScoreBand::LOCAL_DRAFT_ONLY: return "LOCAL_DRAFT_ONLY";
    }
    return "FULL";
}

AgentScoreBand AgentScoreTracker::bandForScore(const AgentScorePolicy& policy, int64_t score) {
    if (score <= policy.localDraftOnlyBelowOrEqual) return AgentScoreBand::LOCAL_DRAFT_ONLY;
    if (score <= policy.reviewOnlyBelowOrEqual) return AgentScoreBand::REVIEW_ONLY;
    if (score <= policy.throttledBelowOrEqual) return AgentScoreBand::THROTTLED;
    return AgentScoreBand::FULL;
}

AgentScoreTracker::AgentScoreTracker(const AgentScorePolicy& policy)
    : policy_(sanitizePolicy(policy)),
      score_(policy_.initialScore),
      band_(bandForScore(policy_, policy_.initialScore)) {}

AgentScorePolicy AgentScoreTracker::policy() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return policy_;
}

void AgentScoreTracker::setPolicy(const AgentScorePolicy& policy) {
    std::lock_guard<std::mutex> lock(mtx_);
    policy_ = sanitizePolicy(policy);
    score_ = clampI64(score_, policy_.scoreMin, policy_.scoreMax);
    updateBandAfterStep();
}

void AgentScoreTracker::updateBandAfterStep() {
    AgentScoreBand rawBand = bandForScore(policy_, score_);
    if (band_ == AgentScoreBand::LOCAL_DRAFT_ONLY && rawBand != AgentScoreBand::LOCAL_DRAFT_ONLY) {
        if (score_ < policy_.localDraftRecoveryAbove ||
            cleanSteps_ < static_cast<uint64_t>(policy_.localDraftRecoveryCleanSteps)) {
            rawBand = AgentScoreBand::LOCAL_DRAFT_ONLY;
        }
    }

    if (rawBand == AgentScoreBand::LOCAL_DRAFT_ONLY && band_ != AgentScoreBand::LOCAL_DRAFT_ONLY) {
        cleanSteps_ = 0;
    }
    band_ = rawBand;
}

AgentScoreSnapshot AgentScoreTracker::snapshotLocked() const {
    AgentScoreSnapshot out;
    out.score = score_;
    out.band = band_;
    out.throttled = (band_ != AgentScoreBand::FULL);
    out.reviewOnly = (band_ == AgentScoreBand::REVIEW_ONLY);
    out.localDraftOnly = (band_ == AgentScoreBand::LOCAL_DRAFT_ONLY);
    out.quarantined = out.localDraftOnly;
    switch (band_) {
        case AgentScoreBand::FULL:
            out.batchLimit = policy_.normalBatchLimit;
            break;
        case AgentScoreBand::THROTTLED:
            out.batchLimit = policy_.throttledBatchLimit;
            break;
        case AgentScoreBand::REVIEW_ONLY:
            out.batchLimit = policy_.reviewOnlyBatchLimit;
            break;
        case AgentScoreBand::LOCAL_DRAFT_ONLY:
            out.batchLimit = policy_.localDraftOnlyBatchLimit;
            break;
    }
    out.cleanSteps = cleanSteps_;
    out.steps = steps_;
    out.acceptedTotal = acceptedTotal_;
    out.rejectedTotal = rejectedTotal_;
    out.violationsTotal = violationsTotal_;
    return out;
}

AgentScoreSnapshot AgentScoreTracker::snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return snapshotLocked();
}

void AgentScoreTracker::applyStep(uint32_t accepted, uint32_t rejected, uint32_t violations) {
    std::lock_guard<std::mutex> lock(mtx_);

    const int64_t decayed = floorMulRatio(score_, policy_.decayNumerator, policy_.decayDenominator);
    const int64_t delta =
        static_cast<int64_t>(accepted) * policy_.acceptWeight -
        static_cast<int64_t>(rejected) * policy_.rejectWeight -
        static_cast<int64_t>(violations) * policy_.violationWeight;

    score_ = clampI64(decayed + delta, policy_.scoreMin, policy_.scoreMax);
    if (rejected == 0 && violations == 0) {
        cleanSteps_ += 1;
    } else {
        cleanSteps_ = 0;
    }
    updateBandAfterStep();

    steps_ += 1;
    acceptedTotal_ += accepted;
    rejectedTotal_ += rejected;
    violationsTotal_ += violations;
}

bool AgentScoreTracker::loadState(const std::string& path, std::string* reason) {
    std::lock_guard<std::mutex> lock(mtx_);

    std::ifstream in(path);
    if (!in.good()) {
        if (reason) *reason = "not_found";
        return true;
    }

    std::string line;
    if (!std::getline(in, line)) {
        if (reason) *reason = "empty";
        return false;
    }

    std::stringstream ss(line);
    std::string token;
    std::vector<std::string> parts;
    while (std::getline(ss, token, ',')) {
        parts.push_back(token);
    }
    if (parts.size() != 8 || parts[0] != "v2") {
        if (reason) *reason = "invalid_format";
        return false;
    }

    int64_t loadedScore = 0;
    uint32_t loadedBandRaw = 0;
    uint64_t loadedClean = 0;
    uint64_t loadedSteps = 0;
    uint64_t loadedAccepted = 0;
    uint64_t loadedRejected = 0;
    uint64_t loadedViolations = 0;

    if (!parseI64(parts[1], &loadedScore) ||
        !parseU32(parts[2], &loadedBandRaw) ||
        !parseU64(parts[3], &loadedClean) ||
        !parseU64(parts[4], &loadedSteps) ||
        !parseU64(parts[5], &loadedAccepted) ||
        !parseU64(parts[6], &loadedRejected) ||
        !parseU64(parts[7], &loadedViolations)) {
        if (reason) *reason = "invalid_numbers";
        return false;
    }

    score_ = clampI64(loadedScore, policy_.scoreMin, policy_.scoreMax);
    cleanSteps_ = loadedClean;
    steps_ = loadedSteps;
    acceptedTotal_ = loadedAccepted;
    rejectedTotal_ = loadedRejected;
    violationsTotal_ = loadedViolations;

    if (loadedBandRaw <= static_cast<uint32_t>(AgentScoreBand::LOCAL_DRAFT_ONLY)) {
        band_ = static_cast<AgentScoreBand>(loadedBandRaw);
    } else {
        band_ = bandForScore(policy_, score_);
    }
    updateBandAfterStep();

    if (reason) *reason = "ok";
    return true;
}

bool AgentScoreTracker::saveState(const std::string& path, std::string* reason) const {
    std::lock_guard<std::mutex> lock(mtx_);

    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            if (reason) *reason = "mkdir_failed";
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) {
        if (reason) *reason = "open_failed";
        return false;
    }

    out << "v2,"
        << score_ << ","
        << static_cast<uint32_t>(band_) << ","
        << cleanSteps_ << ","
        << steps_ << ","
        << acceptedTotal_ << ","
        << rejectedTotal_ << ","
        << violationsTotal_;
    if (!out.good()) {
        if (reason) *reason = "write_failed";
        return false;
    }

    if (reason) *reason = "ok";
    return true;
}

} // namespace synapse::core
