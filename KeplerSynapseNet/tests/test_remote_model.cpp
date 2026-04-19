#include "core/transfer.h"
#include "crypto/crypto.h"
#include "infrastructure/messages.h"
#include <cassert>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static uint32_t floatBits(float f) {
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
    std::memcpy(&bits, &f, sizeof(uint32_t));
    return bits;
}

static std::string addressFromPubKey(const synapse::crypto::PublicKey& pubKey) {
    std::string hex = synapse::crypto::toHex(pubKey);
    if (hex.size() < 52) return {};
    return "ngt1" + hex.substr(0, 52);
}

static synapse::core::Transaction createTxWithMinFee(
    synapse::core::TransferManager& tm,
    const std::string& from,
    const std::string& to,
    uint64_t amount,
    uint64_t feeHint = 0) {
    uint64_t fee = feeHint ? feeHint : tm.estimateFee(0);
    synapse::core::Transaction tx;
    for (int i = 0; i < 5; ++i) {
        tx = tm.createTransaction(from, to, amount, fee);
        uint64_t requiredFee = tm.estimateFee(tx.serialize().size());
        if (requiredFee <= fee) break;
        fee = requiredFee;
    }
    return tx;
}

static void testRemoteMessagesRoundTrip() {
    synapse::RemoteModelOfferMessage offer;
    offer.offerId = "offer123";
    offer.modelId = "llama";
    offer.providerAddress = "ngt1deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbe";
    offer.pricePerRequestAtoms = 12345;
    offer.maxSlots = 3;
    offer.usedSlots = 1;
    offer.expiresAt = 999;

    auto offer2 = synapse::RemoteModelOfferMessage::deserialize(offer.serialize());
    assert(offer2.offerId == offer.offerId);
    assert(offer2.modelId == offer.modelId);
    assert(offer2.providerAddress == offer.providerAddress);
    assert(offer2.pricePerRequestAtoms == offer.pricePerRequestAtoms);
    assert(offer2.maxSlots == offer.maxSlots);
    assert(offer2.usedSlots == offer.usedSlots);
    assert(offer2.expiresAt == offer.expiresAt);

    synapse::RemoteModelRentOkMessage ok;
    ok.offerId = offer.offerId;
    ok.sessionId = "sess123";
    ok.providerAddress = offer.providerAddress;
    ok.pricePerRequestAtoms = offer.pricePerRequestAtoms;
    ok.expiresAt = 1111;

    auto ok2 = synapse::RemoteModelRentOkMessage::deserialize(ok.serialize());
    assert(ok2.offerId == ok.offerId);
    assert(ok2.sessionId == ok.sessionId);
    assert(ok2.providerAddress == ok.providerAddress);
    assert(ok2.pricePerRequestAtoms == ok.pricePerRequestAtoms);
    assert(ok2.expiresAt == ok.expiresAt);

    synapse::RemoteModelInferMessage inf;
    inf.sessionId = ok.sessionId;
    inf.requestId = "req123";
    inf.prompt = "Hello";
    inf.maxTokens = 64;
    inf.temperature = 0.33f;
    inf.topP = 0.77f;
    inf.topK = 50;
    inf.paymentTxidHex = "00";
    inf.timestamp = 2222;
    inf.renterPubKey.fill(7);

    auto inf2 = synapse::RemoteModelInferMessage::deserialize(inf.serialize());
    assert(inf2.sessionId == inf.sessionId);
    assert(inf2.requestId == inf.requestId);
    assert(inf2.prompt == inf.prompt);
    assert(inf2.maxTokens == inf.maxTokens);
    assert(floatBits(inf2.temperature) == floatBits(inf.temperature));
    assert(floatBits(inf2.topP) == floatBits(inf.topP));
    assert(inf2.topK == inf.topK);
    assert(inf2.paymentTxidHex == inf.paymentTxidHex);
    assert(inf2.renterPubKey == inf.renterPubKey);
    assert(inf2.timestamp == inf.timestamp);

    synapse::RemoteModelOutMessage out;
    out.requestId = inf.requestId;
    out.text = "World";
    out.tokensUsed = 42;
    out.latencyMs = 12;

    auto out2 = synapse::RemoteModelOutMessage::deserialize(out.serialize());
    assert(out2.requestId == out.requestId);
    assert(out2.text == out.text);
    assert(out2.tokensUsed == out.tokensUsed);
    assert(out2.latencyMs == out.latencyMs);
}

static void testPaymentOutputsContainProviderAmount() {
    auto uniq = std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto tmpDir = std::filesystem::temp_directory_path() / ("synapsenet_remote_pay_" + uniq);
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);
    std::string dbPath = (tmpDir / "transfer.db").string();

    synapse::core::TransferManager tm;
    assert(tm.open(dbPath));

    auto renter = synapse::crypto::generateKeyPair();
    auto provider = synapse::crypto::generateKeyPair();
    std::string renterAddr = addressFromPubKey(renter.publicKey);
    std::string providerAddr = addressFromPubKey(provider.publicKey);
    assert(!renterAddr.empty());
    assert(!providerAddr.empty());

    synapse::crypto::Hash256 rewardId = synapse::crypto::sha256(std::string("reward_remote_pay"));
    assert(tm.creditRewardDeterministic(renterAddr, rewardId, 1000000));

    const uint64_t price = 5000;
    auto tx = createTxWithMinFee(tm, renterAddr, providerAddr, price);
    assert(tm.signTransaction(tx, renter.privateKey));
    assert(tm.submitTransaction(tx));

    // Check outputs include providerAddr >= price.
    uint64_t paid = 0;
    for (const auto& o : tx.outputs) {
        if (o.address == providerAddr) paid += o.amount;
    }
    assert(paid >= price);
}

int main() {
    testRemoteMessagesRoundTrip();
    testPaymentOutputsContainProviderAmount();
    return 0;
}
