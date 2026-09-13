#include "privacy/privacy.h"
#include "privacy/private_transfer.h"
#include "crypto/confidential_tx.h"
#include "core/ringct.h"
#include "core/transfer.h"
#include "crypto/crypto.h"
#include "crypto/address.h"
#include "quantum/quantum_security.h"

#include <sodium.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

static int fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

static std::string addressFromPubKey(const synapse::crypto::PublicKey& pubKey) {
    return synapse::crypto::canonicalWalletAddressFromPublicKey(pubKey);
}

int main() {
    if (sodium_init() < 0) return fail("sodium");

    auto uniq = std::to_string(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_ringct_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    synapse::core::TransferManager tm;
    if (!tm.open((tmpDir / "transfer.db").string())) return fail("open");
    if (tm.confidentialOutputCount() < 16) return fail("genesis decoys missing");

    auto kp = synapse::crypto::generateKeyPair();
    std::string addr = addressFromPubKey(kp.publicKey);
    synapse::crypto::Hash256 rewardId = synapse::crypto::sha256(std::string("ringct_reward"));
    if (!tm.creditRewardDeterministic(addr, rewardId, 5000000)) return fail("credit");

    synapse::privacy::StealthAddress bob;
    synapse::privacy::StealthAddress sender;
    if (!bob.generateKeys() || !sender.generateKeys()) return fail("stealth keys");

    const uint64_t sendAtoms = 120000;
    synapse::privacy::StealthPayment pay;
    if (!sender.createPayment(bob.getViewPublicKey(), bob.getSpendPublicKey(), sendAtoms, pay)) {
        return fail("createPayment");
    }
    auto range = synapse::crypto::ConfidentialTx::proveRange(sendAtoms, pay.blinding);
    if (range.empty()) return fail("proveRange");
    if (!synapse::crypto::ConfidentialTx::verifyRange(pay.commitment, range)) return fail("verifyRange");
    auto bal = synapse::crypto::ConfidentialTx::proveKnownAmount(pay.commitment, sendAtoms, pay.blinding);
    if (bal.size() != 64) return fail("proveKnownAmount");
    if (!synapse::crypto::ConfidentialTx::verifyKnownAmount(pay.commitment, sendAtoms, bal)) {
        return fail("verifyKnownAmount");
    }

    auto utxos = tm.getUTXOs(addr);
    if (utxos.empty()) return fail("no utxo");

    synapse::core::Transaction shield;
    shield.timestamp = static_cast<uint64_t>(std::time(nullptr));
    shield.status = synapse::core::TxStatus::PENDING;
    shield.balanceProof = bal;
    synapse::core::TxInput inp;
    inp.prevTxHash = utxos[0].txHash;
    inp.outputIndex = utxos[0].outputIndex;
    shield.inputs.push_back(inp);

    synapse::core::TxOutput hidden;
    hidden.amount = 0;
    hidden.address = "rct1" + synapse::crypto::toHex(pay.oneTimeAddress);
    hidden.commitment = pay.commitment;
    hidden.ephemeralPub = pay.ephemeralPub;
    hidden.rangeProof = range;
    hidden.ecdh = pay.ecdh;
    shield.outputs.push_back(hidden);

    uint64_t fee = tm.estimateFee(0);
    for (int i = 0; i < 6; ++i) {
        shield.fee = fee;
        uint64_t change = utxos[0].amount - sendAtoms - fee;
        shield.outputs.resize(1);
        if (utxos[0].amount < sendAtoms + fee) return fail("credit too small for fee");
        if (change > 0) {
            synapse::core::TxOutput ch;
            ch.amount = change;
            ch.address = addr;
            shield.outputs.push_back(ch);
        }
        shield.txid = shield.computeHash();
        uint64_t need = tm.estimateFee(shield.serialize().size());
        if (need <= fee) break;
        fee = need;
    }
    synapse::quantum::HybridSig hs;
    auto hybrid = hs.generateKeyPair();
    if (!tm.signTransaction(shield, kp.privateKey, hybrid)) return fail("sign shield");
    if (shield.quantumSignature.empty()) return fail("shield missing dilithium");
    if (!tm.submitTransaction(shield)) return fail("submit shield");

    auto pending = tm.getPending();
    if (pending.empty()) return fail("shield not pending");
    synapse::crypto::Hash256 blockHash = synapse::crypto::sha256(std::string("ringct_block1"));
    if (!tm.applyBlockTransactionsFromBlock(pending, 1, blockHash)) return fail("apply shield");

    synapse::privacy::PrivateTxOut scanOut;
    scanOut.oneTime = pay.oneTimeAddress;
    scanOut.ephemeralPub = pay.ephemeralPub;
    scanOut.commitment = pay.commitment;
    scanOut.ecdh = pay.ecdh;
    scanOut.rangeProof = range;
    synapse::privacy::OwnedOutput owned;
    if (!synapse::privacy::scanOutput(bob, scanOut, owned)) return fail("bob cannot open shield");
    if (owned.amountAtoms != sendAtoms) return fail("wrong shielded amount");

    std::vector<synapse::privacy::OwnedOutput> wallet;
    wallet.push_back(owned);
    auto mix = tm.confidentialMixins(32);
    std::vector<synapse::privacy::DecoyMember> decoys;
    std::string realP = synapse::crypto::toHex(owned.oneTime);
    for (const auto& m : mix) {
        if (synapse::crypto::toHex(m.first) == realP) continue;
        synapse::privacy::DecoyMember d;
        d.P = m.first;
        d.C = m.second;
        decoys.push_back(d);
    }

    synapse::privacy::StealthAddress carol;
    if (!carol.generateKeys()) return fail("carol keys");
    synapse::privacy::PrivateSendResult spent;
    std::string err;
    if (!synapse::privacy::buildPrivateSend(bob, carol.encodeAddress(), 50000, wallet, decoys, spent, err)) {
        return fail(std::string("buildPrivateSend: ") + err);
    }
    if (!synapse::privacy::verifyPrivateTx(spent.tx, err)) {
        return fail(std::string("verifyPrivateTx: ") + err);
    }

    synapse::core::Transaction ringct;
    if (!synapse::core::fillTransactionFromPrivateTx(
            spent.tx, ringct, static_cast<uint64_t>(std::time(nullptr)), 0)) {
        return fail("fill ringct");
    }
    // Same HybridSig as the shield so IdentityRegistry TOFU matches.
    if (!tm.signTransaction(ringct, kp.privateKey, hybrid)) return fail("sign ringct hybrid");
    if (synapse::quantum::getPQCBackendStatus().dilithiumReal && ringct.quantumSignature.empty()) {
        return fail("ringct missing dilithium");
    }
    if (!tm.submitTransaction(ringct)) return fail("submit ringct");
    auto pending2 = tm.getPending();
    bool found = false;
    for (const auto& p : pending2) {
        if (p.txid == ringct.txid) found = true;
    }
    if (!found) return fail("ringct not pending");
    synapse::crypto::Hash256 blockHash2 = synapse::crypto::sha256(std::string("ringct_block2"));
    if (!tm.applyBlockTransactionsFromBlock({ringct}, 2, blockHash2)) return fail("apply ringct");

    auto miner = synapse::crypto::generateKeyPair();
    synapse::crypto::Hash256 coinbaseId = synapse::crypto::sha256(std::string("ringct_coinbase_reward"));
    const uint64_t coined = 777000;
    uint64_t supplyBefore = tm.totalSupply();
    if (!tm.creditRewardConfidential(miner.publicKey, coinbaseId, coined)) return fail("confidential credit");
    if (tm.getBalance(addressFromPubKey(miner.publicKey)) != 0) return fail("reward leaked as transparent utxo");
    if (tm.getClaimableBalance(miner.publicKey) != coined) return fail("claimable missing");
    if (tm.totalSupply() != supplyBefore + coined) return fail("supply");
    synapse::core::Transaction coinedTx = tm.getTransaction(coinbaseId);
    if (coinedTx.outputs.empty() || coinedTx.outputs[0].amount != 0) return fail("plaintext amount on coinbase");
    if (coinedTx.outputs[0].address.rfind("rct1", 0) != 0) return fail("coinbase dest not rct");
    if (!coinedTx.isRewardCoinbase()) return fail("not coinbase");

    synapse::privacy::StealthAddress minerStealth;
    if (!minerStealth.generateKeys()) return fail("miner stealth");
    synapse::privacy::OwnedOutput claimed;
    if (!tm.claimMintToStealth(coinbaseId, miner.privateKey, minerStealth, &claimed)) return fail("claim");
    if (claimed.amountAtoms != coined) return fail("claimed amount");
    if (claimed.spendScalar.size() != 32) return fail("spend scalar");
    if (tm.getClaimableBalance(miner.publicKey) != 0) return fail("claimable after claim");

    std::cout << "RingCtLedgerTests OK\n";
    return 0;
}
