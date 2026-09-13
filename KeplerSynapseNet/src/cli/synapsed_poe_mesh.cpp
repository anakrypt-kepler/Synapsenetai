// Headless PoE cell for mesh-peer.py. Same votes as the desktop engine.
// Usage: synapsed-poe-mesh <dataDir> <pubkey|ingest-entry|ingest-vote|status> [hex]

#include "core/poe_v1_engine.h"
#include "core/poe_v1_objects.h"
#include "crypto/crypto.h"
#include "crypto/keys.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using synapse::crypto::Keys;
using synapse::core::PoeV1Engine;
using synapse::core::PoeV1Config;

static bool loadKeys(const std::string& dir, synapse::crypto::PrivateKey& sk,
                     synapse::crypto::PublicKey& pk) {
    Keys keys;
    const std::string path = dir + "/wallet.key";
    if (!keys.load(path, "") || !keys.isValid()) {
        keys.generate();
        keys.save(path, "");
    }
    auto pub = keys.getPublicKey();
    auto priv = keys.getPrivateKey();
    if (pub.size() < synapse::crypto::PUBLIC_KEY_SIZE ||
        priv.size() < synapse::crypto::PRIVATE_KEY_SIZE) return false;
    std::memcpy(pk.data(), pub.data(), synapse::crypto::PUBLIC_KEY_SIZE);
    std::memcpy(sk.data(), priv.data(), synapse::crypto::PRIVATE_KEY_SIZE);
    return true;
}

static bool openCell(const std::string& dir, PoeV1Engine& engine,
                     synapse::crypto::PrivateKey& sk, synapse::crypto::PublicKey& pk) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!loadKeys(dir, sk, pk)) return false;
    if (!engine.open(dir + "/poe.db")) return false;
    PoeV1Config cfg;
    cfg.powBits = 12;
    cfg.limits.minPowBits = 12;
    cfg.limits.maxPowBits = 28;
    cfg.adaptiveQuorum = true;
    cfg.adaptiveMajority = true;
    cfg.validatorsN = 0;
    cfg.validatorsM = 0;
    cfg.allowSelfBootstrapValidator = true;
    cfg.minSubmitIntervalSeconds = 5;
    engine.setConfig(cfg);
    engine.setValidatorIdentity(pk, true);
    auto cur = engine.getStaticValidators();
    if (std::find(cur.begin(), cur.end(), pk) == cur.end()) cur.push_back(pk);
    engine.setStaticValidators(cur);
    return true;
}

static void rememberAuthor(PoeV1Engine& engine, const synapse::crypto::PublicKey& author,
                           const synapse::crypto::PublicKey& self) {
    auto cur = engine.getStaticValidators();
    auto add = [&](const synapse::crypto::PublicKey& pk) {
        if (std::find(cur.begin(), cur.end(), pk) == cur.end()) cur.push_back(pk);
        engine.setValidatorIdentity(pk, true);
    };
    add(self);
    add(author);
    engine.setStaticValidators(cur);
}

static bool autoVote(PoeV1Engine& engine, const synapse::crypto::Hash256& sid,
                     const synapse::crypto::PrivateKey& sk, const synapse::crypto::PublicKey& pk,
                     std::string& voteHex) {
    if (engine.isFinalized(sid)) return false;
    auto validators = engine.getDeterministicValidators();
    uint32_t n = engine.effectiveSelectedValidators();
    if (n == 0) return false;
    auto selected = synapse::core::poe_v1::selectValidators(engine.chainSeed(), sid, validators, n);
    if (std::find(selected.begin(), selected.end(), pk) == selected.end()) return false;
    for (const auto& v : engine.getVotesForSubmit(sid)) {
        if (v.validatorPubKey == pk) return false;
    }
    synapse::core::poe_v1::ValidationVoteV1 vote;
    vote.version = 1;
    vote.submitId = sid;
    vote.prevBlockHash = engine.chainSeed();
    vote.flags = 0;
    vote.scores = {100, 100, 100};
    if (!synapse::core::poe_v1::signValidationVoteV1(vote, sk)) return false;
    if (!engine.addVote(vote)) return false;
    voteHex = synapse::crypto::toHex(vote.serialize());
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: synapsed-poe-mesh <dataDir> <cmd> [hex]\n";
        return 2;
    }
    const std::string dir = argv[1];
    const std::string cmd = argv[2];
    PoeV1Engine engine;
    synapse::crypto::PrivateKey sk{};
    synapse::crypto::PublicKey pk{};
    if (!openCell(dir, engine, sk, pk)) {
        std::cerr << "open failed\n";
        return 1;
    }

    if (cmd == "pubkey") {
        std::cout << synapse::crypto::toHex(pk) << "\n";
        return 0;
    }
    if (cmd == "status") {
        std::cout << "{\"entries\":" << engine.totalEntries()
                  << ",\"finalized\":" << engine.totalFinalized()
                  << ",\"selected\":" << engine.effectiveSelectedValidators()
                  << ",\"required\":" << engine.effectiveRequiredVotes()
                  << "}\n";
        return 0;
    }
    if (cmd == "ingest-entry" && argc >= 4) {
        auto bytes = synapse::crypto::fromHex(argv[3]);
        auto entry = synapse::core::poe_v1::KnowledgeEntryV1::deserialize(bytes);
        if (!entry) {
            std::cerr << "bad entry\n";
            return 1;
        }
        rememberAuthor(engine, entry->authorPubKey, pk);
        std::string reason;
        engine.importEntry(*entry, &reason);
        std::string voteHex;
        if (autoVote(engine, entry->submitId(), sk, pk, voteHex))
            std::cout << "VOTE " << voteHex << "\n";
        auto fin = engine.finalize(entry->submitId());
        if (fin) std::cout << "FINALIZED " << synapse::crypto::toHex(entry->submitId()) << "\n";
        return 0;
    }
    if (cmd == "ingest-vote" && argc >= 4) {
        auto bytes = synapse::crypto::fromHex(argv[3]);
        auto vote = synapse::core::poe_v1::ValidationVoteV1::deserialize(bytes);
        if (!vote) {
            std::cerr << "bad vote\n";
            return 1;
        }
        rememberAuthor(engine, vote->validatorPubKey, pk);
        engine.addVote(*vote);
        auto fin = engine.finalize(vote->submitId);
        if (fin) std::cout << "FINALIZED " << synapse::crypto::toHex(vote->submitId) << "\n";
        return 0;
    }
    std::cerr << "unknown cmd\n";
    return 2;
}
