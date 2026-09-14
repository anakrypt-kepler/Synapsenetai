// Build and verify RingCT NGT spends. Do not serialize amounts, wallet
// addresses, blinding factors, or signer indices into the public blob.

#include "privacy/private_transfer.h"
#include "crypto/confidential_tx.h"
#include "crypto/crypto.h"
#include "quantum/quantum_security.h"

#include <sodium.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace synapse {
namespace privacy {

namespace {

constexpr uint8_t kMsgVersion = 2;

void appendBytes(std::vector<uint8_t>& buf, const std::vector<uint8_t>& data) {
    uint32_t n = static_cast<uint32_t>(data.size());
    buf.push_back(static_cast<uint8_t>(n & 0xff));
    buf.push_back(static_cast<uint8_t>((n >> 8) & 0xff));
    buf.push_back(static_cast<uint8_t>((n >> 16) & 0xff));
    buf.push_back(static_cast<uint8_t>((n >> 24) & 0xff));
    buf.insert(buf.end(), data.begin(), data.end());
}

std::string toHex(const std::vector<uint8_t>& v) {
    return crypto::toHex(v);
}

void shuffleIndexPairs(size_t n,
                       size_t& signerIndex,
                       std::vector<std::vector<uint8_t>>& ringP,
                       std::vector<std::vector<uint8_t>>& ringC) {
    if (n == 0) return;
    unsigned char b[4];
    randombytes_buf(b, sizeof(b));
    uint32_t r = static_cast<uint32_t>(b[0]) |
                 (static_cast<uint32_t>(b[1]) << 8) |
                 (static_cast<uint32_t>(b[2]) << 16) |
                 (static_cast<uint32_t>(b[3]) << 24);
    size_t target = static_cast<size_t>(r) % n;
    if (target != signerIndex) {
        std::swap(ringP[signerIndex], ringP[target]);
        std::swap(ringC[signerIndex], ringC[target]);
        signerIndex = target;
    }
}

std::vector<DecoyMember> pickDecoys(const std::vector<DecoyMember>& pool,
                                    const std::set<std::string>& exclude,
                                    size_t need) {
    std::vector<DecoyMember> out;
    std::set<std::string> used = exclude;
    for (const auto& m : pool) {
        if (out.size() >= need) break;
        if (m.P.size() != crypto_core_ed25519_BYTES) continue;
        if (m.C.size() != crypto_core_ed25519_BYTES) continue;
        if (crypto_core_ed25519_is_valid_point(m.P.data()) != 1) continue;
        if (crypto_core_ed25519_is_valid_point(m.C.data()) != 1) continue;
        std::string h = toHex(m.P);
        if (used.count(h)) continue;
        used.insert(h);
        out.push_back(m);
    }
    while (out.size() < need) {
        DecoyMember d;
        d.P = randomValidPoint();
        auto blind = crypto::ConfidentialTx::generateBlindingFactor();
        d.C = crypto::ConfidentialTx::commit(0, blind).commitment;
        sodium_memzero(blind.data(), blind.size());
        if (d.C.size() != crypto_core_ed25519_BYTES) continue;
        std::string h = toHex(d.P);
        if (used.count(h)) continue;
        used.insert(h);
        out.push_back(std::move(d));
    }
    return out;
}

void shuffleOutputs(std::vector<PrivateTxOut>& vouts) {
    if (vouts.size() < 2) return;
    for (size_t i = vouts.size(); i > 1; --i) {
        unsigned char b[4];
        randombytes_buf(b, sizeof(b));
        uint32_t r = static_cast<uint32_t>(b[0]) |
                     (static_cast<uint32_t>(b[1]) << 8) |
                     (static_cast<uint32_t>(b[2]) << 16) |
                     (static_cast<uint32_t>(b[3]) << 24);
        size_t j = static_cast<size_t>(r) % i;
        std::swap(vouts[j], vouts[i - 1]);
    }
}

bool fillStealthOut(const StealthPayment& pay, uint64_t atoms, PrivateTxOut& dest, std::string& err) {
    dest.oneTime = pay.oneTimeAddress;
    dest.ephemeralPub = pay.ephemeralPub;
    dest.commitment = pay.commitment;
    dest.ecdh = pay.ecdh;
    dest.rangeProof = crypto::ConfidentialTx::proveRange(atoms, pay.blinding);
    if (dest.rangeProof.empty()) {
        err = "range proof failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> pointSum(const std::vector<std::vector<uint8_t>>& pts) {
    if (pts.empty()) return {};
    std::vector<uint8_t> acc = pts[0];
    for (size_t i = 1; i < pts.size(); ++i) {
        acc = crypto::RingSign::pointAddPublic(acc, pts[i]);
    }
    return acc;
}

} // namespace

std::vector<uint8_t> randomValidPoint() {
    std::vector<uint8_t> scalar(crypto_core_ed25519_SCALARBYTES);
    crypto_core_ed25519_scalar_random(scalar.data());
    std::vector<uint8_t> point(crypto_core_ed25519_BYTES);
    if (crypto_scalarmult_ed25519_base_noclamp(point.data(), scalar.data()) != 0) {
        throw std::runtime_error("failed to make decoy point");
    }
    sodium_memzero(scalar.data(), scalar.size());
    return point;
}

std::vector<uint8_t> privateTxMessage(const std::vector<PrivateTxOut>& vouts,
                                      const std::vector<std::vector<uint8_t>>& ctildes) {
    std::vector<uint8_t> msg;
    msg.push_back(kMsgVersion);
    uint32_t n = static_cast<uint32_t>(vouts.size());
    msg.push_back(static_cast<uint8_t>(n & 0xff));
    msg.push_back(static_cast<uint8_t>((n >> 8) & 0xff));
    msg.push_back(static_cast<uint8_t>((n >> 16) & 0xff));
    msg.push_back(static_cast<uint8_t>((n >> 24) & 0xff));
    for (const auto& o : vouts) {
        appendBytes(msg, o.oneTime);
        appendBytes(msg, o.ephemeralPub);
        appendBytes(msg, o.commitment);
        appendBytes(msg, o.ecdh);
        appendBytes(msg, o.rangeProof);
    }
    uint32_t nc = static_cast<uint32_t>(ctildes.size());
    msg.push_back(static_cast<uint8_t>(nc & 0xff));
    msg.push_back(static_cast<uint8_t>((nc >> 8) & 0xff));
    msg.push_back(static_cast<uint8_t>((nc >> 16) & 0xff));
    msg.push_back(static_cast<uint8_t>((nc >> 24) & 0xff));
    for (const auto& c : ctildes) {
        appendBytes(msg, c);
    }
    return msg;
}

bool mintOwnedOutput(const StealthAddress& self, uint64_t amountAtoms, OwnedOutput& out) {
    out = OwnedOutput{};
    if (!self.hasKeys() || amountAtoms == 0) return false;
    StealthPayment pay;
    if (!self.createPayment(self.getViewPublicKey(), self.getSpendPublicKey(), amountAtoms, pay)) {
        return false;
    }
    out.oneTime = pay.oneTimeAddress;
    out.ephemeralPub = pay.ephemeralPub;
    out.commitment = pay.commitment;
    out.blinding = pay.blinding;
    out.amountAtoms = amountAtoms;
    out.spent = false;
    out.spendScalar = self.deriveSpendingKey(pay.ephemeralPub);
    return out.spendScalar.size() == crypto_core_ed25519_SCALARBYTES;
}

bool buildPoeStealthCoinbase(const StealthAddress& self,
                             uint64_t amountAtoms,
                             const std::vector<uint8_t>& txid,
                             PrivateTx& tx,
                             OwnedOutput& owned,
                             std::string& err) {
    tx = PrivateTx{};
    owned = OwnedOutput{};
    err.clear();
    if (!self.hasKeys()) {
        err = "stealth keys missing";
        return false;
    }
    if (amountAtoms == 0) {
        err = "invalid amount";
        return false;
    }
    if (txid.size() != 32) {
        err = "bad reward id";
        return false;
    }

    StealthPayment pay;
    if (!self.createPayment(self.getViewPublicKey(), self.getSpendPublicKey(), amountAtoms, pay)) {
        err = "stealth payment failed";
        return false;
    }
    PrivateTxOut vout;
    if (!fillStealthOut(pay, amountAtoms, vout, err)) {
        sodium_memzero(pay.blinding.data(), pay.blinding.size());
        return false;
    }
    if (!scanOutput(self, vout, owned) || owned.amountAtoms != amountAtoms) {
        err = "coinbase scan failed";
        sodium_memzero(pay.blinding.data(), pay.blinding.size());
        return false;
    }

    tx.coinbase = true;
    tx.version = 2;
    tx.txid = txid;
    tx.vouts.push_back(std::move(vout));
    if (!pay.ecdh.empty() && pay.ecdh[0] == 0x03 && pay.ecdh.size() > 80) tx.version = 3;
    sodium_memzero(pay.blinding.data(), pay.blinding.size());
    return true;
}

bool selectSpendable(const std::vector<OwnedOutput>& wallet,
                     uint64_t amountAtoms,
                     std::vector<size_t>& indices,
                     uint64_t& totalAtoms) {
    indices.clear();
    totalAtoms = 0;
    if (amountAtoms == 0) return false;

    std::vector<size_t> order;
    for (size_t i = 0; i < wallet.size(); ++i) {
        if (!wallet[i].spent && wallet[i].amountAtoms > 0 &&
            wallet[i].spendScalar.size() == crypto_core_ed25519_SCALARBYTES &&
            wallet[i].oneTime.size() == crypto_core_ed25519_BYTES &&
            wallet[i].commitment.size() == crypto_core_ed25519_BYTES &&
            wallet[i].blinding.size() == crypto_core_ed25519_SCALARBYTES) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return wallet[a].amountAtoms > wallet[b].amountAtoms;
    });

    for (size_t idx : order) {
        indices.push_back(idx);
        totalAtoms += wallet[idx].amountAtoms;
        if (totalAtoms >= amountAtoms) return true;
    }
    return false;
}

bool buildPrivateSend(const StealthAddress& self,
                      const std::string& recipientStealth,
                      uint64_t amountAtoms,
                      std::vector<OwnedOutput>& wallet,
                      const std::vector<DecoyMember>& decoyPool,
                      PrivateSendResult& result,
                      std::string& err) {
    result = PrivateSendResult{};
    err.clear();
    if (sodium_init() < 0) {
        err = "sodium init failed";
        return false;
    }
    if (!self.hasKeys()) {
        err = "stealth keys missing";
        return false;
    }
    if (amountAtoms == 0) {
        err = "invalid amount";
        return false;
    }

    std::vector<uint8_t> viewPub;
    std::vector<uint8_t> spendPub;
    std::vector<uint8_t> recipKem;
    if (!StealthAddress::decodeAddress(recipientStealth, viewPub, spendPub, recipKem)) {
        err = "recipient must be an SN stealth address";
        return false;
    }

    std::vector<size_t> spendIdx;
    uint64_t totalIn = 0;
    if (!selectSpendable(wallet, amountAtoms, spendIdx, totalIn)) {
        err = "insufficient private balance";
        return false;
    }

    StealthPayment destPay;
    if (!self.createPayment(viewPub, spendPub, amountAtoms, destPay, recipKem)) {
        err = "destination payment failed";
        return false;
    }

    result.changeAtoms = totalIn - amountAtoms;
    StealthPayment changePay;
    bool hasChange = result.changeAtoms > 0;
    if (hasChange) {
        if (!self.createPayment(self.getViewPublicKey(), self.getSpendPublicKey(),
                                result.changeAtoms, changePay)) {
            err = "change payment failed";
            return false;
        }
    }

    PrivateTxOut destOut;
    if (!fillStealthOut(destPay, amountAtoms, destOut, err)) return false;
    result.tx.vouts.push_back(destOut);
    if (hasChange) {
        PrivateTxOut changeOut;
        if (!fillStealthOut(changePay, result.changeAtoms, changeOut, err)) return false;
        result.tx.vouts.push_back(changeOut);
    }
    shuffleOutputs(result.tx.vouts);

    std::vector<uint8_t> sumOutBlind(crypto_core_ed25519_SCALARBYTES, 0);
    crypto_core_ed25519_scalar_add(sumOutBlind.data(), sumOutBlind.data(), destPay.blinding.data());
    if (hasChange) {
        crypto_core_ed25519_scalar_add(sumOutBlind.data(), sumOutBlind.data(), changePay.blinding.data());
    }

    const size_t nIn = spendIdx.size();
    std::vector<std::vector<uint8_t>> rtilde(nIn);
    std::vector<std::vector<uint8_t>> ctildes(nIn);
    std::vector<uint8_t> accR(crypto_core_ed25519_SCALARBYTES, 0);
    for (size_t i = 0; i < nIn; ++i) {
        rtilde[i].assign(crypto_core_ed25519_SCALARBYTES, 0);
        if (i + 1 < nIn) {
            rtilde[i] = crypto::ConfidentialTx::generateBlindingFactor();
            crypto_core_ed25519_scalar_add(accR.data(), accR.data(), rtilde[i].data());
        } else {
            crypto_core_ed25519_scalar_sub(rtilde[i].data(), sumOutBlind.data(), accR.data());
        }
        auto pc = crypto::ConfidentialTx::commit(wallet[spendIdx[i]].amountAtoms, rtilde[i]);
        if (pc.commitment.size() != crypto_core_ed25519_BYTES) {
            err = "pseudo-out commit failed";
            return false;
        }
        ctildes[i] = pc.commitment;
    }

    std::vector<uint8_t> message = privateTxMessage(result.tx.vouts, ctildes);

    std::set<std::string> exclude;
    for (size_t idx : spendIdx) {
        exclude.insert(toHex(wallet[idx].oneTime));
    }

    try {
        for (size_t i = 0; i < nIn; ++i) {
            OwnedOutput& utxo = wallet[spendIdx[i]];
            size_t decoyNeed = kPrivateRingSize - 1;
            auto decoys = pickDecoys(decoyPool, exclude, decoyNeed);

            PrivateTxVin vin;
            vin.ringP.push_back(utxo.oneTime);
            vin.ringC.push_back(utxo.commitment);
            for (const auto& d : decoys) {
                vin.ringP.push_back(d.P);
                vin.ringC.push_back(d.C);
                exclude.insert(toHex(d.P));
            }
            size_t signerIndex = 0;
            shuffleIndexPairs(vin.ringP.size(), signerIndex, vin.ringP, vin.ringC);

            std::vector<uint8_t> z(crypto_core_ed25519_SCALARBYTES);
            crypto_core_ed25519_scalar_sub(z.data(), utxo.blinding.data(), rtilde[i].data());

            std::vector<std::vector<uint8_t>> ringOffset;
            ringOffset.reserve(vin.ringC.size());
            for (const auto& c : vin.ringC) {
                ringOffset.push_back(crypto::RingSign::pointSub(c, ctildes[i]));
            }

            vin.ctilde = ctildes[i];
            vin.sig = crypto::RingSign::signMlsag(
                message, vin.ringP, ringOffset, utxo.spendScalar, z, signerIndex);
            result.tx.vins.push_back(std::move(vin));
            result.spentOneTimeHex.push_back(toHex(utxo.oneTime));
            utxo.spent = true;
            sodium_memzero(utxo.spendScalar.data(), utxo.spendScalar.size());
            sodium_memzero(z.data(), z.size());
        }
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }

    if (hasChange) {
        OwnedOutput ch;
        ch.oneTime = changePay.oneTimeAddress;
        ch.ephemeralPub = changePay.ephemeralPub;
        ch.commitment = changePay.commitment;
        ch.blinding = changePay.blinding;
        ch.amountAtoms = result.changeAtoms;
        ch.spent = false;
        ch.spendScalar = self.deriveSpendingKey(changePay.ephemeralPub);
        result.change.push_back(ch);
        wallet.push_back(ch);
    }

    std::vector<uint8_t> idSrc = message;
    for (const auto& vin : result.tx.vins) {
        idSrc.insert(idSrc.end(), vin.sig.keyImage.begin(), vin.sig.keyImage.end());
    }
    auto idHash = crypto::sha256(idSrc.data(), idSrc.size());
    result.tx.txid.assign(idHash.begin(), idHash.end());

    // Outer Dilithium envelope. MLSAG-2 still authorizes the spend; this is
    // a PQ wrapper over privateTxMessage||txid, derived from the stealth
    // spend scalar (wallet hybrid keys are not in this layer).
    if (quantum::getPQCBackendStatus().dilithiumReal) {
        std::vector<uint8_t> pqcMsg = message;
        pqcMsg.insert(pqcMsg.end(), result.tx.txid.begin(), result.tx.txid.end());
        if (!self.signHybrid(pqcMsg, result.tx.pqcSig) || result.tx.pqcSig.empty()) {
            err = "pqc_sig failed";
            return false;
        }
        result.tx.version = 3;
    }
    for (const auto& o : result.tx.vouts) {
        if (!o.ecdh.empty() && o.ecdh[0] == 0x03 && o.ecdh.size() > 80) {
            result.tx.version = 3;
            break;
        }
    }

    result.amountAtoms = amountAtoms;
    sodium_memzero(destPay.blinding.data(), destPay.blinding.size());
    sodium_memzero(changePay.blinding.data(), changePay.blinding.size());
    sodium_memzero(sumOutBlind.data(), sumOutBlind.size());
    for (auto& r : rtilde) sodium_memzero(r.data(), r.size());
    return true;
}

bool verifyPrivateTx(const PrivateTx& tx, std::string& err) {
    err.clear();
    if (tx.vins.empty() || tx.vouts.empty()) {
        err = "empty vin/vout";
        return false;
    }
    for (const auto& o : tx.vouts) {
        if (o.oneTime.size() != crypto_core_ed25519_BYTES ||
            o.ephemeralPub.size() != crypto_core_ed25519_BYTES ||
            o.commitment.size() != crypto_core_ed25519_BYTES ||
            o.rangeProof.empty()) {
            err = "malformed output";
            return false;
        }
        const bool wrapped = !o.ecdh.empty() && o.ecdh[0] == 0x03 && o.ecdh.size() > 80;
        if (wrapped) {
            if (o.ecdh.size() < 3) {
                err = "malformed output";
                return false;
            }
        } else if (o.ecdh.size() < 40) {
            err = "malformed output";
            return false;
        }
        if (crypto_core_ed25519_is_valid_point(o.oneTime.data()) != 1 ||
            crypto_core_ed25519_is_valid_point(o.ephemeralPub.data()) != 1 ||
            crypto_core_ed25519_is_valid_point(o.commitment.data()) != 1) {
            err = "invalid curve point";
            return false;
        }
        if (!crypto::ConfidentialTx::verifyRange(o.commitment, o.rangeProof)) {
            err = "range proof failed";
            return false;
        }
    }

    std::vector<std::vector<uint8_t>> ctildes;
    std::vector<std::vector<uint8_t>> outCs;
    ctildes.reserve(tx.vins.size());
    outCs.reserve(tx.vouts.size());
    for (const auto& vin : tx.vins) ctildes.push_back(vin.ctilde);
    for (const auto& o : tx.vouts) outCs.push_back(o.commitment);

    try {
        auto sumIn = pointSum(ctildes);
        auto sumOut = pointSum(outCs);
        if (sumIn.size() != crypto_core_ed25519_BYTES ||
            sumOut.size() != crypto_core_ed25519_BYTES ||
            sodium_memcmp(sumIn.data(), sumOut.data(), crypto_core_ed25519_BYTES) != 0) {
            err = "commitment conservation failed";
            return false;
        }
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }

    std::vector<uint8_t> message = privateTxMessage(tx.vouts, ctildes);
    std::set<std::string> images;
    for (const auto& vin : tx.vins) {
        if (vin.ringP.size() < 2 || vin.ringP.size() != vin.ringC.size()) {
            err = "ring too small";
            return false;
        }
        if (vin.ctilde.size() != crypto_core_ed25519_BYTES) {
            err = "missing pseudo-out";
            return false;
        }
        std::vector<std::vector<uint8_t>> ringOffset;
        try {
            for (const auto& c : vin.ringC) {
                ringOffset.push_back(crypto::RingSign::pointSub(c, vin.ctilde));
            }
        } catch (const std::exception& e) {
            err = e.what();
            return false;
        }
        if (!crypto::RingSign::verifyMlsag(message, vin.ringP, ringOffset, vin.sig)) {
            err = "mlsag invalid";
            return false;
        }
        std::string ki = toHex(vin.sig.keyImage);
        if (images.count(ki)) {
            err = "duplicate key image";
            return false;
        }
        images.insert(ki);
    }

    const auto pqc = quantum::getPQCBackendStatus();
    // v2 (default, including ledger reconstruction): MLSAG only, even if an
    // output carries a 0x03 ecdh wrap. v3 engine blobs must present a valid
    // Dilithium envelope when the real backend is on.
    if (pqc.dilithiumReal && tx.version >= 3) {
        if (tx.pqcSig.empty()) {
            err = "v3 missing pqc_sig";
            return false;
        }
        std::vector<uint8_t> pqcMsg = message;
        pqcMsg.insert(pqcMsg.end(), tx.txid.begin(), tx.txid.end());
        if (!StealthAddress::verifyHybrid(pqcMsg, tx.pqcSig)) {
            err = "pqc_sig invalid";
            return false;
        }
    } else if (pqc.dilithiumReal && !tx.pqcSig.empty()) {
        std::vector<uint8_t> pqcMsg = message;
        pqcMsg.insert(pqcMsg.end(), tx.txid.begin(), tx.txid.end());
        if (!StealthAddress::verifyHybrid(pqcMsg, tx.pqcSig)) {
            err = "pqc_sig invalid";
            return false;
        }
    }
    return true;
}

bool scanOutput(const StealthAddress& self,
                const PrivateTxOut& outp,
                OwnedOutput& owned) {
    owned = OwnedOutput{};
    if (!self.hasKeys()) return false;
    if (!self.checkOwnership(outp.oneTime, outp.ephemeralPub)) return false;

    // ECDH blob: first byte != 0x03 is classical (v2). 0x03 is ML-KEM wrap;
    // tryOpenPayment fail-closes if decaps does not match.
    uint64_t atoms = 0;
    std::vector<uint8_t> blinding;
    if (!self.tryOpenPayment(outp.ephemeralPub, outp.ecdh, atoms, blinding)) {
        return false;
    }
    crypto::PedersenCommitment c;
    c.commitment = outp.commitment;
    c.blinding = blinding;
    if (!c.verify(atoms)) {
        return false;
    }

    owned.oneTime = outp.oneTime;
    owned.ephemeralPub = outp.ephemeralPub;
    owned.commitment = outp.commitment;
    owned.blinding = blinding;
    owned.amountAtoms = atoms;
    owned.spent = false;
    owned.spendScalar = self.deriveSpendingKey(outp.ephemeralPub);
    return owned.spendScalar.size() == crypto_core_ed25519_SCALARBYTES;
}

}
}
