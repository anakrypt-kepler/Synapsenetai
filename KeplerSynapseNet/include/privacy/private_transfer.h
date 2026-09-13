#pragma once

// Private NGT spend: stealth dest + MLSAG-2 RingCT over (P, C) + range proofs.
// Public blobs must not include sender, recipient wallet, amount, blinding,
// signer index, or real/decoy tags. Wallet notes stay local (0600).
// Each vin publishes a ring of (P, C), a pseudo-out Ctilde, and MLSAG.
// Verifiers check MLSAG, sum(Ctilde) == sum(C_out), and range proofs.

#include "crypto/ring_signature.h"
#include "privacy/privacy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace synapse {
namespace privacy {

constexpr size_t kPrivateRingSize = 11;
constexpr uint64_t kNgtAtoms = 100000000ULL;

struct OwnedOutput {
    std::vector<uint8_t> oneTime;
    std::vector<uint8_t> ephemeralPub;
    std::vector<uint8_t> spendScalar;
    std::vector<uint8_t> commitment;
    std::vector<uint8_t> blinding;
    uint64_t amountAtoms = 0;
    bool spent = false;
};

struct DecoyMember {
    std::vector<uint8_t> P;
    std::vector<uint8_t> C;
};

struct PrivateTxVin {
    std::vector<std::vector<uint8_t>> ringP;
    std::vector<std::vector<uint8_t>> ringC;
    std::vector<uint8_t> ctilde;
    crypto::MlsagSignature sig;
};

struct PrivateTxOut {
    std::vector<uint8_t> oneTime;
    std::vector<uint8_t> ephemeralPub;
    std::vector<uint8_t> commitment;
    std::vector<uint8_t> ecdh;
    std::vector<uint8_t> rangeProof;
};

struct PrivateTx {
    std::vector<uint8_t> txid;
    std::vector<PrivateTxVin> vins;
    std::vector<PrivateTxOut> vouts;
    // Outer HybridSig (ed25519 + ML-DSA-65) over privateTxMessage||txid.
    // Empty on v2 chain blobs; required on v3 when dilithiumReal.
    std::vector<uint8_t> pqcSig;
    // JSON / engine version. Ledger reconstruction leaves this at 2 so
    // MLSAG-only verify still accepts wrapped ecdh without pqc_sig.
    int version = 2;
    int64_t ts = 0;
};

struct PrivateSendResult {
    PrivateTx tx;
    std::vector<OwnedOutput> change;
    std::vector<std::string> spentOneTimeHex;
    uint64_t amountAtoms = 0;
    uint64_t changeAtoms = 0;
};

std::vector<uint8_t> privateTxMessage(const std::vector<PrivateTxOut>& vouts,
                                      const std::vector<std::vector<uint8_t>>& ctildes);

// Test/dev helper. Production NAAN must not mint stealth outputs.
bool mintOwnedOutput(const StealthAddress& self, uint64_t amountAtoms, OwnedOutput& out);

bool selectSpendable(const std::vector<OwnedOutput>& wallet,
                     uint64_t amountAtoms,
                     std::vector<size_t>& indices,
                     uint64_t& totalAtoms);

bool buildPrivateSend(const StealthAddress& self,
                      const std::string& recipientStealth,
                      uint64_t amountAtoms,
                      std::vector<OwnedOutput>& wallet,
                      const std::vector<DecoyMember>& decoyPool,
                      PrivateSendResult& result,
                      std::string& err);

bool verifyPrivateTx(const PrivateTx& tx, std::string& err);

bool scanOutput(const StealthAddress& self,
                const PrivateTxOut& outp,
                OwnedOutput& owned);

std::vector<uint8_t> randomValidPoint();

}
}
