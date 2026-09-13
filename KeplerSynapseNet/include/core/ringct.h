#pragma once

#include "core/transfer.h"
#include "privacy/private_transfer.h"

#include <string>

namespace synapse {
namespace core {

bool fillTransactionFromPrivateTx(const privacy::PrivateTx& in, Transaction& out, uint64_t timestamp, uint64_t fee);
bool privateTxFromTransaction(const Transaction& in, privacy::PrivateTx& out);
bool verifyRingCtCrypto(const Transaction& tx, std::string& err);

// Deterministic coinbase lock: NUMS one-time P, Pedersen C, range + known-amount.
// Amount is auditable (PoE already publishes it). Destination is not an ngt1 address.
bool buildRewardCoinbaseOutput(const crypto::PublicKey& author,
                               const crypto::Hash256& rewardId,
                               uint64_t amount,
                               TxOutput& out,
                               std::vector<uint8_t>& knownAmountProof);

}
}
