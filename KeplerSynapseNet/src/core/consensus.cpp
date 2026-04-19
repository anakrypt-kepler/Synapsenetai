#include "core/consensus.h"
#include "database/database.h"
#include "utils/logger.h"
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <random>
#include <cmath>
#include <fstream>

namespace synapse {
namespace core {

static void writeU64(std::vector<uint8_t>& out, uint64_t val) {
    for (int i = 0; i < 8; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static uint64_t readU64(const uint8_t* p) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val |= static_cast<uint64_t>(p[i]) << (i * 8);
    return val;
}

static void writeU32(std::vector<uint8_t>& out, uint32_t val) {
    for (int i = 0; i < 4; i++) out.push_back((val >> (i * 8)) & 0xff);
}

static uint32_t readU32(const uint8_t* p) {
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) val |= static_cast<uint32_t>(p[i]) << (i * 8);
    return val;
}

static uint64_t addSaturatingU64(uint64_t a, uint64_t b) {
    return (UINT64_MAX - a < b) ? UINT64_MAX : (a + b);
}

static uint64_t mulSaturatingU64(uint64_t a, uint64_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
}

std::vector<uint8_t> Vote::serialize() const {
    std::vector<uint8_t> out;
    writeU64(out, eventId);
    out.insert(out.end(), validator.begin(), validator.end());
    out.push_back(static_cast<uint8_t>(type));
    uint64_t scoreBits;
    std::memcpy(&scoreBits, &scoreGiven, sizeof(double));
    writeU64(out, scoreBits);
    writeU64(out, timestamp);
    out.insert(out.end(), signature.begin(), signature.end());
    return out;
}

Vote Vote::deserialize(const std::vector<uint8_t>& data) {
    Vote v;
    if (data.size() < 8 + 33 + 1 + 8 + 8 + 64) return v;
    const uint8_t* p = data.data();
    v.eventId = readU64(p); p += 8;
    std::memcpy(v.validator.data(), p, 33); p += 33;
    v.type = static_cast<VoteType>(*p++);
    uint64_t scoreBits = readU64(p); p += 8;
    std::memcpy(&v.scoreGiven, &scoreBits, sizeof(double));
    v.timestamp = readU64(p); p += 8;
    std::memcpy(v.signature.data(), p, 64);
    return v;
}

crypto::Hash256 Vote::computeHash() const {
    std::vector<uint8_t> buf;
    writeU64(buf, eventId);
    buf.insert(buf.end(), validator.begin(), validator.end());
    buf.push_back(static_cast<uint8_t>(type));
    uint64_t scoreBits;
    std::memcpy(&scoreBits, &scoreGiven, sizeof(double));
    writeU64(buf, scoreBits);
    writeU64(buf, timestamp);
    return crypto::doubleSha256(buf.data(), buf.size());
}

bool Vote::verify() const {
    crypto::Hash256 hash = computeHash();
    return crypto::verify(hash, signature, validator);
}

// Validator/result persistence helpers
static void writeString(std::vector<uint8_t>& out, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    writeU32(out, len);
    out.insert(out.end(), s.begin(), s.end());
}

static std::string readString(const uint8_t* p, size_t& off, size_t maxLen) {
    uint32_t len = readU32(p + off);
    off += 4;
    if (len > maxLen) return std::string();
    std::string s(reinterpret_cast<const char*>(p + off), len);
    off += len;
    return s;
}

static std::vector<uint8_t> serializeValidator(const Validator& v) {
    std::vector<uint8_t> out;
    out.insert(out.end(), v.pubKey.begin(), v.pubKey.end());
    writeString(out, v.address);
    writeU64(out, v.stake);
    uint64_t repBits = 0;
    std::memcpy(&repBits, &v.reputation, sizeof(double));
    writeU64(out, repBits);
    writeU32(out, v.validationsCompleted);
    writeU64(out, v.lastActive);
    out.push_back(v.eligible ? 1 : 0);
    out.push_back(v.active ? 1 : 0);
    writeU64(out, v.totalRewards);
    return out;
}

static Validator deserializeValidator(const std::vector<uint8_t>& data) {
    Validator v;
    if (data.size() < crypto::PUBLIC_KEY_SIZE + 4 + 8 + 8 + 4 + 8 + 1 + 1 + 8) return v;
    size_t off = 0;
    std::memcpy(v.pubKey.data(), data.data() + off, crypto::PUBLIC_KEY_SIZE); off += crypto::PUBLIC_KEY_SIZE;
    v.address = readString(data.data(), off, 1024);
    v.stake = readU64(data.data() + off); off += 8;
    uint64_t repBits = readU64(data.data() + off); off += 8;
    std::memcpy(&v.reputation, &repBits, sizeof(double));
    v.validationsCompleted = readU32(data.data() + off); off += 4;
    v.lastActive = readU64(data.data() + off); off += 8;
    v.eligible = data[off++] != 0;
    v.active = data[off++] != 0;
    v.totalRewards = readU64(data.data() + off); off += 8;
    return v;
}

static std::vector<uint8_t> serializeResult(const ValidationResult& r) {
    std::vector<uint8_t> out;
    writeU64(out, r.eventId);
    out.push_back(static_cast<uint8_t>(r.state));
    writeU32(out, r.approveVotes);
    writeU32(out, r.rejectVotes);
    writeU32(out, r.totalVotes);
    writeU32(out, r.requiredVotes);
    uint64_t avgBits = 0; std::memcpy(&avgBits, &r.averageScore, sizeof(double)); writeU64(out, avgBits);
    writeU64(out, r.startTime);
    writeU64(out, r.endTime);
    writeU64(out, r.reward);
    return out;
}

static ValidationResult deserializeResult(const std::vector<uint8_t>& data) {
    ValidationResult r;
    if (data.size() < 8 + 1 + 4*4 + 8 + 8 + 8) return r;
    size_t off = 0;
    r.eventId = readU64(data.data() + off); off += 8;
    r.state = static_cast<ConsensusState>(data[off++]);
    r.approveVotes = readU32(data.data() + off); off += 4;
    r.rejectVotes = readU32(data.data() + off); off += 4;
    r.totalVotes = readU32(data.data() + off); off += 4;
    r.requiredVotes = readU32(data.data() + off); off += 4;
    uint64_t avgBits = readU64(data.data() + off); off += 8; std::memcpy(&r.averageScore, &avgBits, sizeof(double));
    r.startTime = readU64(data.data() + off); off += 8;
    r.endTime = readU64(data.data() + off); off += 8;
    r.reward = readU64(data.data() + off); off += 8;
    return r;
}

struct Consensus::Impl {
    database::Database db;
    std::unordered_map<uint64_t, ValidationResult> results;
    std::unordered_map<std::string, Validator> validators;
    ConsensusConfig config;
    mutable std::mutex mtx;
    std::function<void(uint64_t, ConsensusState)> stateCallback;
    std::function<void(const ValidationResult&)> completeCallback;
    std::function<void(const std::string&)> onProposalCallback;
    std::function<void(const std::string&, bool)> onVoteCallback;
    std::function<void(const std::string&)> onValidatorJoined;
    std::function<void(const std::string&)> onValidatorLeft;
    Ledger* ledger = nullptr;
    uint64_t validationCounter = 0;
};

template <typename ImplT>
static void saveValidatorToDb(ImplT* impl, const std::string& addr, const Validator& v) {
    std::vector<uint8_t> data = serializeValidator(v);
    impl->db.put(std::string("consensus:validator:") + addr, data);
}

template <typename ImplT>
static void saveResultToDb(ImplT* impl, uint64_t eventId, const ValidationResult& r) {
    std::vector<uint8_t> data = serializeResult(r);
    impl->db.put(std::string("consensus:result:") + std::to_string(eventId), data);
}

Consensus::Consensus() : impl_(std::make_unique<Impl>()) {}
Consensus::~Consensus() { close(); }

bool Consensus::open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (!impl_->db.open(dbPath)) return false;
    
    auto counterData = impl_->db.get("meta:validationCounter");
    if (!counterData.empty()) {
        impl_->validationCounter = readU64(counterData.data());
    }
    // load persisted validators
    auto vkeys = impl_->db.keys("consensus:validator:");
    for (const auto& k : vkeys) {
        auto data = impl_->db.get(k);
        if (data.empty()) continue;
        Validator v = deserializeValidator(data);
        std::string addr = crypto::toHex(v.pubKey);
        impl_->validators[addr] = v;
    }
    // load persisted results
    auto rkeys = impl_->db.keys("consensus:result:");
    for (const auto& k : rkeys) {
        auto data = impl_->db.get(k);
        if (data.empty()) continue;
        ValidationResult r = deserializeResult(data);
        impl_->results[r.eventId] = r;
    }
    // ensure loaded validators are persisted under canonical key (in case)
    for (const auto& [addr, v] : impl_->validators) {
        saveValidatorToDb(impl_.get(), addr, v);
    }
    return true;
}

void Consensus::close() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->db.close();
}

void Consensus::setConfig(const ConsensusConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config = config;
}

uint64_t Consensus::submitForValidation(uint64_t eventId, const crypto::PublicKey& submitter) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    ValidationResult result;
    result.eventId = eventId;
    result.state = ConsensusState::PENDING;
    result.approveVotes = 0;
    result.rejectVotes = 0;
    result.totalVotes = 0;
    result.requiredVotes = impl_->config.minValidators;
    result.averageScore = 0.0;
    result.startTime = std::time(nullptr);
    result.endTime = 0;
    result.reward = 0;
    
    impl_->results[eventId] = result;
    saveResultToDb(impl_.get(), eventId, result);
    impl_->validationCounter++;
    
    std::vector<uint8_t> counterBuf;
    writeU64(counterBuf, impl_->validationCounter);
    impl_->db.put("meta:validationCounter", counterBuf);
    
    if (impl_->stateCallback) {
        impl_->stateCallback(eventId, ConsensusState::PENDING);
    }
    
    return eventId;
}

bool Consensus::vote(const Vote& vote) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->results.find(vote.eventId);
    if (it == impl_->results.end()) return false;
    
    if (it->second.state != ConsensusState::PENDING && 
        it->second.state != ConsensusState::VALIDATING) {
        return false;
    }
    
    if (!vote.verify()) return false;
    
    std::string validatorAddr = crypto::toHex(vote.validator);
    auto valIt = impl_->validators.find(validatorAddr);
    if (valIt == impl_->validators.end() || !valIt->second.eligible) {
        return false;
    }
    
    for (const auto& v : it->second.votes) {
        if (v.validator == vote.validator) return false;
    }
    
    it->second.votes.push_back(vote);
    it->second.totalVotes++;
    
    if (vote.type == VoteType::APPROVE) {
        it->second.approveVotes++;
        double totalScore = it->second.averageScore * (it->second.totalVotes - 1) + vote.scoreGiven;
        it->second.averageScore = totalScore / it->second.totalVotes;
    } else if (vote.type == VoteType::REJECT) {
        it->second.rejectVotes++;
    }
    
    if (it->second.state == ConsensusState::PENDING) {
        it->second.state = ConsensusState::VALIDATING;
        if (impl_->stateCallback) {
            impl_->stateCallback(vote.eventId, ConsensusState::VALIDATING);
        }
    }
    
    valIt->second.validationsCompleted++;
    valIt->second.lastActive = std::time(nullptr);
    // persist validator
    saveValidatorToDb(impl_.get(), valIt->first, valIt->second);
    // persist result
    saveResultToDb(impl_.get(), it->first, it->second);
    
    if (it->second.totalVotes >= it->second.requiredVotes) {
        finalizeValidation(vote.eventId);
    }
    
    return true;
}

bool Consensus::finalizeValidation(uint64_t eventId) {
    auto it = impl_->results.find(eventId);
    if (it == impl_->results.end()) return false;
    
    ValidationResult& result = it->second;
    result.endTime = std::time(nullptr);
    
    double approveRatio = static_cast<double>(result.approveVotes) / result.totalVotes;
    
    if (approveRatio >= impl_->config.majorityThreshold) {
        result.state = ConsensusState::APPROVED;
        result.reward = calculateReward(result);
        
        for (const auto& vote : result.votes) {
            if (vote.type == VoteType::APPROVE) {
                std::string addr = crypto::toHex(vote.validator);
                auto valIt = impl_->validators.find(addr);
                if (valIt != impl_->validators.end()) {
                    valIt->second.reputation = std::min(1.0, valIt->second.reputation + 0.01);
                }
            }
        }
    } else {
        result.state = ConsensusState::REJECTED;
        
        for (const auto& vote : result.votes) {
            if (vote.type == VoteType::REJECT) {
                std::string addr = crypto::toHex(vote.validator);
                auto valIt = impl_->validators.find(addr);
                if (valIt != impl_->validators.end()) {
                    valIt->second.reputation = std::min(1.0, valIt->second.reputation + 0.01);
                }
            }
        }
    }
    
    if (impl_->stateCallback) {
        impl_->stateCallback(eventId, result.state);
    }
    
    if (impl_->completeCallback) {
        impl_->completeCallback(result);
    }
    // persist result and updated validators
    saveResultToDb(impl_.get(), eventId, result);
    for (const auto& vpair : impl_->validators) {
        saveValidatorToDb(impl_.get(), vpair.first, vpair.second);
    }
    
    return true;
}

ConsensusState Consensus::getState(uint64_t eventId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->results.find(eventId);
    return it != impl_->results.end() ? it->second.state : ConsensusState::PENDING;
}

ValidationResult Consensus::getResult(uint64_t eventId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->results.find(eventId);
    return it != impl_->results.end() ? it->second : ValidationResult{};
}

std::vector<uint64_t> Consensus::getPending() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<uint64_t> pending;
    for (const auto& [id, result] : impl_->results) {
        if (result.state == ConsensusState::PENDING) {
            pending.push_back(id);
        }
    }
    return pending;
}

std::vector<uint64_t> Consensus::getValidating() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<uint64_t> validating;
    for (const auto& [id, result] : impl_->results) {
        if (result.state == ConsensusState::VALIDATING) {
            validating.push_back(id);
        }
    }
    return validating;
}

std::vector<Vote> Consensus::getVotesFor(uint64_t eventId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->results.find(eventId);
    return it != impl_->results.end() ? it->second.votes : std::vector<Vote>{};
}

bool Consensus::registerValidator(const Validator& validator) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    if (validator.stake < impl_->config.minStake) return false;
    if (validator.reputation < impl_->config.minReputation) return false;
    
    std::string addr = crypto::toHex(validator.pubKey);
    impl_->validators[addr] = validator;
    impl_->validators[addr].eligible = true;
    // persist validator
    saveValidatorToDb(impl_.get(), addr, impl_->validators[addr]);
    if (impl_->onValidatorJoined) impl_->onValidatorJoined(addr);
    
    return true;
}

bool Consensus::updateValidatorStake(const crypto::PublicKey& pubKey, uint64_t newStake) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string addr = crypto::toHex(pubKey);
    auto it = impl_->validators.find(addr);
    if (it == impl_->validators.end()) return false;
    
    it->second.stake = newStake;
    it->second.eligible = (newStake >= impl_->config.minStake && 
                           it->second.reputation >= impl_->config.minReputation);
    // persist validator
    saveValidatorToDb(impl_.get(), addr, it->second);
    return true;
}

bool Consensus::updateValidatorReputation(const crypto::PublicKey& pubKey, double delta) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string addr = crypto::toHex(pubKey);
    auto it = impl_->validators.find(addr);
    if (it == impl_->validators.end()) return false;
    
    it->second.reputation = std::max(0.0, std::min(1.0, it->second.reputation + delta));
    it->second.eligible = (it->second.stake >= impl_->config.minStake && 
                           it->second.reputation >= impl_->config.minReputation);
    // persist validator
    saveValidatorToDb(impl_.get(), addr, it->second);
    return true;
}

Validator Consensus::getValidator(const crypto::PublicKey& pubKey) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string addr = crypto::toHex(pubKey);
    auto it = impl_->validators.find(addr);
    return it != impl_->validators.end() ? it->second : Validator{};
}

std::vector<Validator> Consensus::getEligibleValidators() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Validator> eligible;
    for (const auto& [addr, val] : impl_->validators) {
        if (val.eligible) eligible.push_back(val);
    }
    return eligible;
}

std::vector<Validator> Consensus::selectValidators(uint64_t eventId, uint32_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    std::vector<Validator> eligible;
    for (const auto& [addr, val] : impl_->validators) {
        if (val.eligible) eligible.push_back(val);
    }

    std::sort(eligible.begin(), eligible.end(), [](const Validator& a, const Validator& b) {
        if (a.pubKey != b.pubKey) {
            return std::lexicographical_compare(a.pubKey.begin(), a.pubKey.end(), b.pubKey.begin(), b.pubKey.end());
        }
        return a.address < b.address;
    });

    if (eligible.size() <= count) return eligible;

    std::vector<uint64_t> weights;
    weights.reserve(eligible.size());
    uint64_t totalWeight = 0;
    for (const auto& val : eligible) {
        double rep = val.reputation;
        if (!(rep > 0.0)) rep = 0.0;
        long double repScaledLd = static_cast<long double>(rep) * 1000000.0L;
        if (repScaledLd < 1.0L) repScaledLd = 1.0L;
        if (repScaledLd > static_cast<long double>(UINT64_MAX)) repScaledLd = static_cast<long double>(UINT64_MAX);
        uint64_t repScaled = static_cast<uint64_t>(repScaledLd);

        uint64_t stakeWeight = std::max<uint64_t>(1, val.stake);
        unsigned __int128 combined = static_cast<unsigned __int128>(stakeWeight) * static_cast<unsigned __int128>(repScaled);
        uint64_t weight = combined > static_cast<unsigned __int128>(UINT64_MAX)
            ? UINT64_MAX
            : static_cast<uint64_t>(combined);
        if (weight == 0) weight = 1;
        weights.push_back(weight);
        if (UINT64_MAX - totalWeight < weight) totalWeight = UINT64_MAX;
        else totalWeight += weight;
    }

    if (totalWeight == 0) return {};

    std::vector<Validator> selected;
    selected.reserve(count);
    std::vector<bool> used(eligible.size(), false);

    for (uint32_t i = 0; i < count && i < eligible.size(); ++i) {
        if (totalWeight == 0) break;

        std::vector<uint8_t> entropy;
        entropy.reserve(56);
        writeU64(entropy, eventId);
        writeU32(entropy, i);
        writeU64(entropy, static_cast<uint64_t>(eligible.size()));
        // Mix in additional entropy from validator set state to prevent prediction
        writeU64(entropy, totalWeight);
        for (size_t k = 0; k < eligible.size() && k < 3; ++k) {
            entropy.insert(entropy.end(), eligible[k].pubKey.begin(), eligible[k].pubKey.end());
        }
        auto seedHash = crypto::sha256(entropy.data(), entropy.size());
        crypto::Hash256 pickHash = crypto::sha256(seedHash.data(), seedHash.size());

        uint64_t r = 0;
        for (int b = 0; b < 8; ++b) r |= static_cast<uint64_t>(pickHash[static_cast<size_t>(b)]) << (8 * b);
        uint64_t target = r % totalWeight;

        size_t chosen = eligible.size();
        uint64_t cumulative = 0;
        for (size_t j = 0; j < eligible.size(); ++j) {
            if (used[j]) continue;
            uint64_t w = weights[j];
            if (UINT64_MAX - cumulative < w) cumulative = UINT64_MAX;
            else cumulative += w;
            if (target < cumulative) {
                chosen = j;
                break;
            }
        }

        if (chosen == eligible.size()) {
            for (size_t j = 0; j < eligible.size(); ++j) {
                if (!used[j]) {
                    chosen = j;
                    break;
                }
            }
        }

        if (chosen == eligible.size()) break;

        selected.push_back(eligible[chosen]);
        used[chosen] = true;
        if (weights[chosen] >= totalWeight) totalWeight = 0;
        else totalWeight -= weights[chosen];
    }

    std::sort(selected.begin(), selected.end(), [](const Validator& a, const Validator& b) {
        if (a.pubKey != b.pubKey) {
            return std::lexicographical_compare(a.pubKey.begin(), a.pubKey.end(), b.pubKey.begin(), b.pubKey.end());
        }
        return a.address < b.address;
    });

    return selected;
}

bool Consensus::isEligibleValidator(const crypto::PublicKey& pubKey) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::string addr = crypto::toHex(pubKey);
    auto it = impl_->validators.find(addr);
    return it != impl_->validators.end() && it->second.eligible;
}

uint64_t Consensus::calculateReward(const ValidationResult& result) const {
    // Keep consensus rewards in integer atom units, mirroring PoE v1 reward shape:
    // base + bonus - penalty with min/max clamps.
    constexpr uint64_t kSubmissionStakeToAtoms = 1000000ULL;
    constexpr uint64_t kMinRewardDivisor = 10ULL;
    constexpr uint64_t kMaxRewardMultiplier = 10ULL;

    uint64_t submissionStakeUnits = std::max<uint64_t>(1, impl_->config.submissionStake);
    uint64_t baseReward = mulSaturatingU64(submissionStakeUnits, kSubmissionStakeToAtoms);
    if (baseReward == 0) baseReward = kSubmissionStakeToAtoms;

    uint64_t reward = baseReward;
    uint64_t approvingStake = 0;
    uint64_t rejectingStake = 0;
    uint64_t qualifyingReputationApprovals = 0;

    for (const auto& vote : result.votes) {
        std::string addr = crypto::toHex(vote.validator);
        auto valIt = impl_->validators.find(addr);
        if (valIt == impl_->validators.end()) continue;
        const Validator& validator = valIt->second;

        if (vote.type == VoteType::APPROVE) {
            approvingStake = addSaturatingU64(approvingStake, validator.stake);
            if (validator.reputation >= impl_->config.minReputation) {
                qualifyingReputationApprovals = addSaturatingU64(qualifyingReputationApprovals, 1);
            }
        } else if (vote.type == VoteType::REJECT) {
            rejectingStake = addSaturatingU64(rejectingStake, validator.stake);
        }
    }

    uint64_t minStakeUnit = std::max<uint64_t>(1, impl_->config.minStake);
    uint64_t stakeBonusPerUnit = baseReward / minStakeUnit;
    if (stakeBonusPerUnit == 0) stakeBonusPerUnit = 1;
    if (approvingStake > minStakeUnit) {
        uint64_t extraStake = approvingStake - minStakeUnit;
        uint64_t stakeBonus = mulSaturatingU64(extraStake, stakeBonusPerUnit);
        reward = addSaturatingU64(reward, stakeBonus);
    }

    uint64_t requiredVotes = std::max<uint64_t>(1, static_cast<uint64_t>(result.requiredVotes));
    uint64_t reputationBonusPerApproval = baseReward / requiredVotes;
    if (reputationBonusPerApproval == 0) reputationBonusPerApproval = 1;
    if (qualifyingReputationApprovals > 0) {
        uint64_t reputationBonus = mulSaturatingU64(qualifyingReputationApprovals, reputationBonusPerApproval);
        reward = addSaturatingU64(reward, reputationBonus);
    }

    if (rejectingStake > 0) {
        uint64_t penaltyChunks = rejectingStake / minStakeUnit;
        uint64_t penalty = mulSaturatingU64(penaltyChunks, reputationBonusPerApproval);
        reward = reward > penalty ? (reward - penalty) : 0;
    }

    uint64_t minReward = baseReward / kMinRewardDivisor;
    if (minReward == 0) minReward = 1;
    uint64_t maxReward = mulSaturatingU64(baseReward, kMaxRewardMultiplier);
    if (maxReward < minReward) maxReward = minReward;
    if (reward < minReward) reward = minReward;
    if (reward > maxReward) reward = maxReward;
    return reward;
}

uint64_t Consensus::calculatePenalty(const ValidationResult& result) const {
    if (result.state == ConsensusState::REJECTED) {
        constexpr uint64_t kSubmissionStakeToAtoms = 1000000ULL;
        return mulSaturatingU64(impl_->config.submissionStake, kSubmissionStakeToAtoms);
    }
    return 0;
}

void Consensus::onStateChange(std::function<void(uint64_t, ConsensusState)> callback) {
    impl_->stateCallback = callback;
}

void Consensus::onValidationComplete(std::function<void(const ValidationResult&)> callback) {
    impl_->completeCallback = callback;
}

void Consensus::processTimeouts() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    
    for (auto& [id, result] : impl_->results) {
        if (result.state == ConsensusState::PENDING || 
            result.state == ConsensusState::VALIDATING) {
            if (now - result.startTime > impl_->config.validationTimeout) {
                result.state = ConsensusState::EXPIRED;
                result.endTime = now;
                if (impl_->stateCallback) {
                    impl_->stateCallback(id, ConsensusState::EXPIRED);
                }
            }
        }
    }
}

size_t Consensus::pendingCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    size_t count = 0;
    for (const auto& [id, result] : impl_->results) {
        if (result.state == ConsensusState::PENDING || 
            result.state == ConsensusState::VALIDATING) {
            count++;
        }
    }
    return count;
}

size_t Consensus::validatorCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->validators.size();
}

Consensus::ConsensusStats Consensus::getStats() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    Consensus::ConsensusStats stats{};
    stats.totalValidations = impl_->validationCounter;
    stats.pendingValidations = 0;
    stats.approvedValidations = 0;
    stats.rejectedValidations = 0;
    stats.totalValidators = impl_->validators.size();
    stats.activeValidators = 0;
    
    for (const auto& [id, result] : impl_->results) {
        if (result.state == ConsensusState::PENDING || 
            result.state == ConsensusState::VALIDATING) {
            stats.pendingValidations++;
        } else if (result.state == ConsensusState::APPROVED) {
            stats.approvedValidations++;
        } else if (result.state == ConsensusState::REJECTED) {
            stats.rejectedValidations++;
        }
    }
    
    uint64_t now = std::time(nullptr);
    for (const auto& [addr, val] : impl_->validators) {
        if (now - val.lastActive < 3600) stats.activeValidators++;
    }
    
    return stats;
}

std::vector<Validator> Consensus::getValidators() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<Validator> result;
    for (const auto& [addr, val] : impl_->validators) {
        result.push_back(val);
    }
    return result;
}

std::vector<Validator> Consensus::getTopValidators(size_t count) const {
    auto validators = getValidators();
    std::sort(validators.begin(), validators.end(), 
              [](const Validator& a, const Validator& b) {
                  return a.stake > b.stake;
              });
    if (validators.size() > count) {
        validators.resize(count);
    }
    return validators;
}

ValidationResult Consensus::getValidationResult(uint64_t validationId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->results.find(validationId);
    if (it != impl_->results.end()) {
        return it->second;
    }
    return ValidationResult{};
}

std::vector<ValidationResult> Consensus::getRecentResults(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<ValidationResult> results;
    
    std::vector<std::pair<uint64_t, ValidationResult>> sorted;
    for (const auto& [id, result] : impl_->results) {
        sorted.emplace_back(result.startTime, result);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    for (size_t i = 0; i < count && i < sorted.size(); i++) {
        results.push_back(sorted[i].second);
    }
    return results;
}

ConsensusConfig Consensus::getConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->config;
}

uint64_t Consensus::getTotalStake() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t total = 0;
    for (const auto& [addr, val] : impl_->validators) {
        total += val.stake;
    }
    return total;
}

double Consensus::getApprovalRate() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    size_t approved = 0, total = 0;
    for (const auto& [id, result] : impl_->results) {
        if (result.state == ConsensusState::APPROVED) approved++;
        if (result.state != ConsensusState::PENDING && 
            result.state != ConsensusState::VALIDATING) total++;
    }
    return total > 0 ? static_cast<double>(approved) / total : 0.0;
}

bool Consensus::slashValidator(const std::string& address, uint64_t amount) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->validators.find(address);
    if (it == impl_->validators.end()) return false;
    
    if (it->second.stake <= amount) {
        impl_->validators.erase(it);
        impl_->db.del(std::string("consensus:validator:") + address);
        if (impl_->onValidatorLeft) impl_->onValidatorLeft(address);
    } else {
        it->second.stake -= amount;
        saveValidatorToDb(impl_.get(), address, it->second);
    }
    return true;
}

bool Consensus::rewardValidator(const std::string& address, uint64_t amount) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->validators.find(address);
    if (it == impl_->validators.end()) return false;
    
    it->second.stake += amount;
    it->second.totalRewards += amount;
    saveValidatorToDb(impl_.get(), address, it->second);
    return true;
}

std::vector<std::string> Consensus::getActiveValidatorAddresses() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> result;
    uint64_t now = std::time(nullptr);
    
    for (const auto& [addr, val] : impl_->validators) {
        if (now - val.lastActive < 3600) {
            result.push_back(addr);
        }
    }
    return result;
}

uint64_t Consensus::getValidatorStake(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->validators.find(address);
    if (it == impl_->validators.end()) return 0;
    return it->second.stake;
}

void Consensus::setMinStake(uint64_t amount) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.minStake = amount;
}

void Consensus::setQuorum(double quorum) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.quorum = std::max(0.5, std::min(quorum, 1.0));
}

bool Consensus::exportValidators(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::ofstream f(path);
    if (!f.is_open()) return false;
    
    f << "[\n";
    bool first = true;
    for (const auto& [addr, val] : impl_->validators) {
        if (!first) f << ",\n";
        f << "  {\"address\": \"" << addr << "\", \"stake\": " << val.stake << "}";
        first = false;
    }
    f << "\n]";
    return true;
}

void Consensus::clearExpiredResults() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    uint64_t now = std::time(nullptr);
    
    for (auto it = impl_->results.begin(); it != impl_->results.end(); ) {
        if (it->second.state == ConsensusState::EXPIRED &&
            now - it->second.endTime > 86400) {
            it = impl_->results.erase(it);
        } else {
            ++it;
        }
    }
}

bool Consensus::isValidatorActive(const std::string& address) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->validators.find(address);
    if (it == impl_->validators.end()) return false;
    
    uint64_t now = std::time(nullptr);
    return now - it->second.lastActive < 3600;
}

void Consensus::updateValidatorActivity(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->validators.find(address);
    if (it != impl_->validators.end()) {
        it->second.lastActive = std::time(nullptr);
    }
}

std::string Consensus::selectProposer() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->validators.empty()) return "";
    
    uint64_t totalStake = 0;
    for (const auto& [addr, val] : impl_->validators) {
        totalStake += val.stake;
    }
    // Deterministic proposer selection: use sha256(seed||height) entropy.
    // If no seed provided, fallback to lexicographic selection.
    if (impl_->config.seed.empty()) {
        // fallback deterministic: highest stake
        std::string best = impl_->validators.begin()->first;
        uint64_t bestStake = 0;
        for (const auto& [addr, val] : impl_->validators) {
            if (val.stake > bestStake) { bestStake = val.stake; best = addr; }
        }
        return best;
    }

    // Use currentHeight stored in config? We will use validationCounter as a proxy for deterministic rotation.
    uint64_t height = impl_->validationCounter;
    // If totalStake is zero, choose a deterministic proposer to avoid divide/modulo by zero.
    if (totalStake == 0) {
        // Prefer highest reputation, tie-breaker: lexicographic address
        std::string bestAddr = impl_->validators.begin()->first;
        double bestRep = -1.0;
        for (const auto& [addr, val] : impl_->validators) {
            if (val.reputation > bestRep) {
                bestRep = val.reputation;
                bestAddr = addr;
            } else if (val.reputation == bestRep) {
                if (addr < bestAddr) bestAddr = addr;
            }
        }
        return bestAddr;
    }

    std::vector<uint8_t> entropy;
    entropy.reserve(impl_->config.seed.size() + 8);
    entropy.insert(entropy.end(), impl_->config.seed.begin(), impl_->config.seed.end());
    writeU64(entropy, height);
    crypto::Hash256 h = crypto::sha256(entropy);
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= static_cast<uint64_t>(h[i]) << (8 * i);
    uint64_t target = r % totalStake; // safe: totalStake > 0

    uint64_t cumulative = 0;
    for (const auto& [addr, val] : impl_->validators) {
        cumulative += val.stake;
        if (target < cumulative) return addr;
    }
    return impl_->validators.begin()->first;
}

void Consensus::setVotingTimeout(uint32_t seconds) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.votingTimeout = seconds;
}

bool Consensus::importValidators(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        utils::Logger::warn("Consensus::importValidators failed to open validator file: " + path);
        return false;
    }

    utils::Logger::warn(
        "Consensus::importValidators is not implemented: validator JSON import skipped for file: " + path);
    return false;
}

void Consensus::setOnProposalCallback(std::function<void(const std::string&)> callback) {
    impl_->onProposalCallback = callback;
}

void Consensus::setOnVoteCallback(std::function<void(const std::string&, bool)> callback) {
    impl_->onVoteCallback = callback;
}

bool Consensus::proposeBlock(const Block& block, const std::string& proposer) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->validators.find(proposer);
    if (it == impl_->validators.end()) return false;
    // Use block height as event id for validation proposal
    uint64_t eventId = block.height;
    submitForValidation(eventId, it->second.pubKey);
    if (impl_->onProposalCallback) {
        impl_->onProposalCallback(std::to_string(eventId));
    }
    return true;
}

bool Consensus::voteOnProposal(const std::string& proposalId, 
                                const std::string& validator, 
                                bool approve) {
    uint64_t eventId = 0;
    try { eventId = std::stoull(proposalId); } catch (...) { return false; }

    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->validators.find(validator);
    if (it == impl_->validators.end()) return false;

    Vote v;
    v.eventId = eventId;
    v.validator = it->second.pubKey;
    v.type = approve ? VoteType::APPROVE : VoteType::REJECT;
    v.scoreGiven = 1.0;
    v.timestamp = std::time(nullptr);
    // leave signature empty (internal vote), vote() allows unsigned internal votes

    bool ok = vote(v);
    if (impl_->onVoteCallback) impl_->onVoteCallback(proposalId, approve);
    return ok;
}

void Consensus::setOnValidatorJoined(std::function<void(const std::string&)> callback) {
    impl_->onValidatorJoined = callback;
}

void Consensus::setOnValidatorLeft(std::function<void(const std::string&)> callback) {
    impl_->onValidatorLeft = callback;
}

void Consensus::setLedger(Ledger* ledger) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->ledger = ledger;
}

std::vector<std::string> Consensus::getActiveValidators() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> result;
    for (const auto& [addr, val] : impl_->validators) {
        if (val.active) {
            result.push_back(addr);
        }
    }
    return result;
}

void Consensus::setBlockFinality(uint32_t blocks) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->config.blockFinality = blocks;
}

uint32_t Consensus::getBlockFinality() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->config.blockFinality;
}

bool Consensus::isBlockFinalized(uint64_t height) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->ledger) {
        uint64_t lh = impl_->ledger->height();
        if (lh >= height) {
            return (lh - height) >= impl_->config.blockFinality;
        }
        return false;
    }
    return false;
}

}
}
