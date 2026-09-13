// Mesh PoE: desktop cell submits, gossips, and votes over the onion protocol
// (POE_ENTRY / POE_VOTE). Thin mesh-peers without poe_pk stay mailboxes.

#include "ide/synapsed_engine.h"
#include "core/poe_v1_objects.h"
#include "crypto/keys.h"
#include "crypto/crypto.h"
#include "../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

int64_t nowMillis();

namespace synapse {
namespace ide {
namespace {

bool isHexChars(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string trimCopy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

bool SynapsedEngine::initPoeEngine() {
    crypto::Keys keys;
    const std::string walletPath = dataDir_ + "/wallet.key";
    if (!keys.load(walletPath, "")) return false;
    if (!walletMnemonic_.empty()) keys.fromMnemonic(walletMnemonic_);
    if (!keys.isValid()) return false;
    auto pub = keys.getPublicKey();
    auto priv = keys.getPrivateKey();
    if (pub.size() < crypto::PUBLIC_KEY_SIZE || priv.size() < crypto::PRIVATE_KEY_SIZE)
        return false;
    std::memcpy(poePk_.data(), pub.data(), crypto::PUBLIC_KEY_SIZE);
    std::memcpy(poeSk_.data(), priv.data(), crypto::PRIVATE_KEY_SIZE);

    std::error_code ec;
    std::filesystem::create_directories(dataDir_ + "/poe", ec);
    poeV1_ = std::make_unique<core::PoeV1Engine>();
    if (!poeV1_->open(dataDir_ + "/poe/poe.db")) {
        poeV1_.reset();
        return false;
    }

    core::PoeV1Config cfg;
    cfg.powBits = 12;
    cfg.limits.minPowBits = 12;
    cfg.limits.maxPowBits = 28;
    cfg.adaptiveQuorum = true;
    cfg.adaptiveMajority = true;
    cfg.validatorsN = 0;
    cfg.validatorsM = 0;
    cfg.allowSelfBootstrapValidator = true;
    cfg.minSubmitIntervalSeconds = 5;
    poeV1_->setConfig(cfg);
    poeV1_->setValidatorIdentity(poePk_, true);
    poeReady_.store(true);
    refreshPoeValidators();
    return true;
}

void SynapsedEngine::refreshPoeValidators() const {
    if (!poeReady_.load() || !poeV1_) return;
    std::vector<crypto::PublicKey> vals;
    vals.push_back(poePk_);
    {
        std::lock_guard<std::mutex> lock(knownPeersMtx_);
        for (const auto& kv : knownPeers_) {
            const std::string& hex = kv.second.poePk;
            if (!isHexChars(hex, crypto::PUBLIC_KEY_SIZE * 2)) continue;
            auto bytes = crypto::fromHex(hex);
            if (bytes.size() != crypto::PUBLIC_KEY_SIZE) continue;
            crypto::PublicKey pk{};
            std::memcpy(pk.data(), bytes.data(), pk.size());
            if (pk == poePk_) continue;
            vals.push_back(pk);
        }
    }
    std::lock_guard<std::mutex> lock(poeMtx_);
    if (!poeV1_) return;
    poeV1_->setStaticValidators(vals);
    for (const auto& pk : vals) poeV1_->setValidatorIdentity(pk, true);
}

void SynapsedEngine::gossipPoeLine(const std::string& line) const {
    std::vector<std::string> dests;
    {
        std::lock_guard<std::mutex> lock(knownPeersMtx_);
        dests.reserve(knownPeers_.size());
        for (const auto& kv : knownPeers_) dests.push_back(kv.first);
    }
    for (const auto& onion : dests) {
        meshSend(onion, line);
    }
}

void SynapsedEngine::appendKnowledgeJsonl(const std::string& submitHex, const std::string& kind,
                                         const std::string& title, bool finalized) const {
    nlohmann::json row;
    row["id"] = submitHex.size() >= 16 ? submitHex.substr(0, 16) : submitHex;
    row["submitId"] = submitHex;
    row["kind"] = kind;
    row["title"] = title;
    row["status"] = finalized ? "finalized" : "pending";
    row["ngt_earned"] = "0.00";
    row["hash"] = submitHex.size() >= 32 ? submitHex.substr(0, 32) : submitHex;
    row["ts"] = nowMillis();
    std::ofstream kf(dataDir_ + "/knowledge.jsonl", std::ios::app);
    if (kf.good()) kf << row.dump() << "\n";
}

void SynapsedEngine::markKnowledgeFinalized(const std::string& submitHex) const {
    const std::string path = dataDir_ + "/knowledge.jsonl";
    std::ifstream in(path);
    if (!in.good()) return;
    std::vector<std::string> lines;
    std::string line;
    bool changed = false;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (!j.is_discarded() && j.is_object()) {
            const std::string sid = j.value("submitId", j.value("hash", std::string()));
            if (sid.rfind(submitHex.substr(0, std::min<size_t>(sid.size(), submitHex.size())), 0) == 0
                || submitHex.rfind(sid, 0) == 0) {
                j["status"] = "finalized";
                line = j.dump();
                changed = true;
            }
        }
        lines.push_back(line);
    }
    in.close();
    if (!changed) return;
    std::ofstream out(path, std::ios::trunc);
    for (const auto& l : lines) out << l << "\n";
}

void SynapsedEngine::maybePoeAutoVote(const crypto::Hash256& submitId) const {
    if (!poeReady_.load() || !poeV1_) return;
    std::lock_guard<std::mutex> lock(poeMtx_);
    if (!poeV1_ || poeV1_->isFinalized(submitId)) return;
    auto entry = poeV1_->getEntry(submitId);
    if (!entry) return;
    auto validators = poeV1_->getDeterministicValidators();
    if (validators.empty()) return;
    uint32_t selectedCount = poeV1_->effectiveSelectedValidators();
    if (selectedCount == 0) return;
    auto selected = core::poe_v1::selectValidators(poeV1_->chainSeed(), submitId, validators, selectedCount);
    if (std::find(selected.begin(), selected.end(), poePk_) == selected.end()) return;
    for (const auto& v : poeV1_->getVotesForSubmit(submitId)) {
        if (v.validatorPubKey == poePk_) return;
    }
    core::poe_v1::ValidationVoteV1 vote;
    vote.version = 1;
    vote.submitId = submitId;
    vote.prevBlockHash = poeV1_->chainSeed();
    vote.flags = 0;
    vote.scores = {100, 100, 100};
    if (!core::poe_v1::signValidationVoteV1(vote, poeSk_)) return;
    if (!poeV1_->addVote(vote)) return;
    gossipPoeLine("POE_VOTE " + crypto::toHex(vote.serialize()) + "\n");
    auto fin = poeV1_->finalize(submitId);
    if (fin) markKnowledgeFinalized(crypto::toHex(submitId));
}

void SynapsedEngine::ingestPoeEntryHex(const std::string& hexRaw) const {
    const std::string hex = trimCopy(hexRaw);
    auto bytes = crypto::fromHex(hex);
    if (bytes.empty()) return;
    auto entry = core::poe_v1::KnowledgeEntryV1::deserialize(bytes);
    if (!entry) return;
    refreshPoeValidators();
    if (!poeReady_.load() || !poeV1_) return;
    std::string reason;
    bool added = false;
    {
        std::lock_guard<std::mutex> lock(poeMtx_);
        if (!poeV1_) return;
        added = poeV1_->importEntry(*entry, &reason);
        if (!added && reason != "duplicate_submit") return;
    }
    if (added) {
        gossipPoeLine("POE_ENTRY " + hex + "\n");
        appendKnowledgeJsonl(crypto::toHex(entry->submitId()), "mesh", entry->title, false);
    }
    maybePoeAutoVote(entry->submitId());
}

void SynapsedEngine::ingestPoeVoteHex(const std::string& hexRaw) const {
    const std::string hex = trimCopy(hexRaw);
    auto bytes = crypto::fromHex(hex);
    if (bytes.empty()) return;
    auto vote = core::poe_v1::ValidationVoteV1::deserialize(bytes);
    if (!vote) return;
    refreshPoeValidators();
    if (!poeReady_.load() || !poeV1_) return;
    bool added = false;
    {
        std::lock_guard<std::mutex> lock(poeMtx_);
        if (!poeV1_) return;
        added = poeV1_->addVote(*vote);
    }
    if (added) gossipPoeLine("POE_VOTE " + hex + "\n");
    std::lock_guard<std::mutex> lock(poeMtx_);
    if (!poeV1_) return;
    auto fin = poeV1_->finalize(vote->submitId);
    if (fin) markKnowledgeFinalized(crypto::toHex(vote->submitId));
}

std::string SynapsedEngine::submitPoeKnowledge(const std::string& title, const std::string& body,
                                              core::poe_v1::ContentType type) {
    if (!poeReady_.load() || !poeV1_)
        return "{\"error\":\"poe not ready\"}";
    refreshPoeValidators();
    core::PoeSubmitResult res;
    std::vector<uint8_t> entryBlob;
    std::vector<uint8_t> voteBlob;
    {
        std::lock_guard<std::mutex> lock(poeMtx_);
        if (!poeV1_) return "{\"error\":\"poe not ready\"}";
        res = poeV1_->submit(type, title, body, {}, poeSk_, true);
        if (res.ok) {
            auto e = poeV1_->getEntry(res.submitId);
            if (e) entryBlob = e->serialize();
            auto votes = poeV1_->getVotesForSubmit(res.submitId);
            if (!votes.empty()) voteBlob = votes.front().serialize();
        }
    }
    if (!res.ok) {
        nlohmann::json err;
        err["error"] = res.error.empty() ? "poe submit failed" : res.error;
        return err.dump();
    }
    const std::string sidHex = crypto::toHex(res.submitId);
    if (!entryBlob.empty())
        gossipPoeLine("POE_ENTRY " + crypto::toHex(entryBlob) + "\n");
    if (!voteBlob.empty())
        gossipPoeLine("POE_VOTE " + crypto::toHex(voteBlob) + "\n");
    appendKnowledgeJsonl(sidHex, type == core::poe_v1::ContentType::CODE ? "code" : "knowledge",
                         title, res.finalized);
    appendLocalChainBlock("poe_entry", sidHex.size() >= 32 ? sidHex.substr(0, 32) : sidHex);
    nlohmann::json out;
    out["ok"] = true;
    out["id"] = sidHex.size() >= 16 ? sidHex.substr(0, 16) : sidHex;
    out["submitId"] = sidHex;
    out["hash"] = sidHex.size() >= 32 ? sidHex.substr(0, 32) : sidHex;
    out["status"] = res.finalized ? "finalized" : "pending";
    out["finalized"] = res.finalized;
    out["creditedAtoms"] = 0;
    out["requiredVotes"] = poeV1_ ? poeV1_->effectiveRequiredVotes() : 0;
    out["selectedValidators"] = poeV1_ ? poeV1_->effectiveSelectedValidators() : 0;
    out["message"] = res.finalized
        ? "PoE finalized. NGT still pays on epoch coinbase, not here."
        : "PoE submitted. Waiting for the other full cell to vote.";
    return out.dump();
}

} // namespace ide
} // namespace synapse
