// Convert between ledger Transaction and private RingCT blobs.

#include "core/ringct.h"
#include "crypto/crypto.h"
#include "crypto/confidential_tx.h"
#include "crypto/ring_signature.h"

#include <cstring>
#include <sodium.h>

namespace synapse {
namespace core {

namespace {

std::string rctAddressFromP(const std::vector<uint8_t>& P) {
    return "rct1" + crypto::toHex(P);
}

bool pFromRctAddress(const std::string& addr, std::vector<uint8_t>& P) {
    if (addr.size() < 5 || addr.rfind("rct1", 0) != 0) return false;
    P = crypto::fromHex(addr.substr(4));
    return P.size() == 32;
}

} // namespace

bool fillTransactionFromPrivateTx(const privacy::PrivateTx& in, Transaction& out, uint64_t timestamp, uint64_t fee) {
    out = Transaction{};
    out.timestamp = timestamp;
    out.fee = fee;
    out.status = TxStatus::PENDING;
    for (const auto& vin : in.vins) {
        TxInput inp;
        inp.ringP = vin.ringP;
        inp.ringC = vin.ringC;
        inp.ctilde = vin.ctilde;
        inp.mlsag = vin.sig.serialize();
        out.inputs.push_back(std::move(inp));
    }
    for (const auto& o : in.vouts) {
        TxOutput outp;
        outp.amount = 0;
        outp.address = rctAddressFromP(o.oneTime);
        outp.commitment = o.commitment;
        outp.ephemeralPub = o.ephemeralPub;
        outp.rangeProof = o.rangeProof;
        outp.ecdh = o.ecdh;
        out.outputs.push_back(std::move(outp));
    }
    out.txid = out.computeHash();
    return !out.inputs.empty() && !out.outputs.empty();
}

bool privateTxFromTransaction(const Transaction& in, privacy::PrivateTx& out) {
    out = privacy::PrivateTx{};
    out.ts = static_cast<int64_t>(in.timestamp);
    out.txid.assign(in.txid.begin(), in.txid.end());
    for (const auto& inp : in.inputs) {
        if (!inp.isRingCt()) return false;
        privacy::PrivateTxVin vin;
        vin.ringP = inp.ringP;
        vin.ringC = inp.ringC;
        vin.ctilde = inp.ctilde;
        try {
            vin.sig = crypto::MlsagSignature::deserialize(inp.mlsag);
        } catch (...) {
            return false;
        }
        out.vins.push_back(std::move(vin));
    }
    for (const auto& o : in.outputs) {
        privacy::PrivateTxOut outp;
        if (!pFromRctAddress(o.address, outp.oneTime)) {
            outp.oneTime = crypto::fromHex(o.address);
        }
        outp.ephemeralPub = o.ephemeralPub;
        outp.commitment = o.commitment;
        outp.ecdh = o.ecdh;
        outp.rangeProof = o.rangeProof;
        out.vouts.push_back(std::move(outp));
    }
    return !out.vins.empty() && !out.vouts.empty();
}

bool verifyRingCtCrypto(const Transaction& tx, std::string& err) {
    privacy::PrivateTx ptx;
    if (!privateTxFromTransaction(tx, ptx)) {
        err = "not a ringct transaction";
        return false;
    }
    return privacy::verifyPrivateTx(ptx, err);
}

namespace {

void hashToScalar(const std::vector<uint8_t>& data, std::vector<uint8_t>& scalar) {
    unsigned char wide[crypto_core_ed25519_NONREDUCEDSCALARBYTES];
    crypto_hash_sha512(wide, data.data(), data.size());
    scalar.assign(crypto_core_ed25519_SCALARBYTES, 0);
    crypto_core_ed25519_scalar_reduce(scalar.data(), wide);
    sodium_memzero(wide, sizeof(wide));
}

bool hashToPoint(const std::vector<uint8_t>& data, std::vector<uint8_t>& point) {
    unsigned char hash[crypto_hash_sha512_BYTES];
    crypto_hash_sha512(hash, data.data(), data.size());
    point.assign(crypto_core_ed25519_BYTES, 0);
    crypto_core_ed25519_from_hash(point.data(), hash);
    sodium_memzero(hash, sizeof(hash));
    return crypto_core_ed25519_is_valid_point(point.data()) == 1;
}

void appendDomain(std::vector<uint8_t>& out, const char* domain) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(domain);
    out.insert(out.end(), p, p + std::strlen(domain));
}

} // namespace

bool buildRewardCoinbaseOutput(const crypto::PublicKey& author,
                               const crypto::Hash256& rewardId,
                               uint64_t amount,
                               TxOutput& out,
                               std::vector<uint8_t>& knownAmountProof) {
    out = TxOutput{};
    knownAmountProof.clear();
    if (amount == 0) return false;
    if (sodium_init() < 0) return false;

    std::vector<uint8_t> pSeed;
    appendDomain(pSeed, "SynapseNet_reward_P_v1");
    pSeed.insert(pSeed.end(), rewardId.begin(), rewardId.end());
    pSeed.insert(pSeed.end(), author.begin(), author.end());
    std::vector<uint8_t> P;
    if (!hashToPoint(pSeed, P)) return false;

    std::vector<uint8_t> bSeed;
    appendDomain(bSeed, "SynapseNet_reward_b_v1");
    bSeed.insert(bSeed.end(), rewardId.begin(), rewardId.end());
    bSeed.insert(bSeed.end(), author.begin(), author.end());
    std::vector<uint8_t> blinding;
    hashToScalar(bSeed, blinding);

    auto committed = crypto::ConfidentialTx::commit(amount, blinding);
    if (committed.commitment.size() != 32) {
        sodium_memzero(blinding.data(), blinding.size());
        return false;
    }
    auto range = crypto::ConfidentialTx::proveRange(amount, blinding);
    auto known = crypto::ConfidentialTx::proveKnownAmount(committed.commitment, amount, blinding);
    if (range.empty() || known.size() != 64) {
        sodium_memzero(blinding.data(), blinding.size());
        return false;
    }
    if (!crypto::ConfidentialTx::verifyRange(committed.commitment, range) ||
        !crypto::ConfidentialTx::verifyKnownAmount(committed.commitment, amount, known)) {
        sodium_memzero(blinding.data(), blinding.size());
        return false;
    }

    std::vector<uint8_t> rSeed;
    appendDomain(rSeed, "SynapseNet_reward_R_v1");
    rSeed.insert(rSeed.end(), rewardId.begin(), rewardId.end());
    rSeed.insert(rSeed.end(), author.begin(), author.end());
    std::vector<uint8_t> R;
    if (!hashToPoint(rSeed, R)) {
        sodium_memzero(blinding.data(), blinding.size());
        return false;
    }

    out.amount = 0;
    out.address = rctAddressFromP(P);
    out.commitment = committed.commitment;
    out.ephemeralPub = std::move(R);
    out.rangeProof = std::move(range);
    out.ecdh.clear();
    knownAmountProof = std::move(known);
    sodium_memzero(blinding.data(), blinding.size());
    return true;
}

}
}
