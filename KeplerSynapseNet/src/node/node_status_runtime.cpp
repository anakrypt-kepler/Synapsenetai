#include "node/node_status_runtime.h"

#include <stdexcept>

namespace synapse::node {

std::string quantumSecurityLevelToString(quantum::SecurityLevel level) {
    switch (level) {
        case quantum::SecurityLevel::STANDARD: return "standard";
        case quantum::SecurityLevel::HIGH: return "high";
        case quantum::SecurityLevel::PARANOID: return "paranoid";
        case quantum::SecurityLevel::QUANTUM_READY: return "quantum-ready";
    }
    return "standard";
}

std::string quantumAlgorithmToString(quantum::CryptoAlgorithm algo) {
    switch (algo) {
        case quantum::CryptoAlgorithm::CLASSIC_ED25519: return "classic_ed25519";
        case quantum::CryptoAlgorithm::CLASSIC_X25519: return "classic_x25519";
        case quantum::CryptoAlgorithm::CLASSIC_AES256GCM: return "classic_aes256gcm";
        case quantum::CryptoAlgorithm::LATTICE_KYBER768: return "lattice_kyber768";
        case quantum::CryptoAlgorithm::LATTICE_DILITHIUM65: return "lattice_dilithium65";
        case quantum::CryptoAlgorithm::HASH_SPHINCS128S: return "hash_sphincs128s";
        case quantum::CryptoAlgorithm::OTP_VERNAM: return "otp_vernam";
        case quantum::CryptoAlgorithm::QKD_BB84: return "qkd_bb84";
        case quantum::CryptoAlgorithm::HYBRID_KEM: return "hybrid_kem";
        case quantum::CryptoAlgorithm::HYBRID_SIG: return "hybrid_sig";
    }
    return "classic_ed25519";
}

bool quantumLiboqsEnabled() {
#ifdef USE_LIBOQS
    return true;
#else
    return false;
#endif
}

std::string quantumKyberImplementationMode() {
    return quantum::getPQCBackendStatus().kyberReal ? "liboqs" : "simulation";
}

std::string quantumDilithiumImplementationMode() {
    return quantum::getPQCBackendStatus().dilithiumReal ? "liboqs" : "simulation";
}

std::string quantumSphincsImplementationMode() {
    return quantum::getPQCBackendStatus().sphincsReal ? "liboqs" : "simulation";
}

std::string quantumCapabilityMode() {
    const auto pqc = quantum::getPQCBackendStatus();
    if (pqc.kyberReal && pqc.dilithiumReal && pqc.sphincsReal) {
        return "real_pqc";
    }
    if (pqc.kyberReal || pqc.dilithiumReal || pqc.sphincsReal) {
        return "mixed_real_and_simulated";
    }
    return quantumLiboqsEnabled() ? "compiled_real_backend_unavailable"
                                  : "development_simulation";
}

namespace {

void validateQuantumRuntimeInputs(const NodeQuantumStatusRuntimeInputs& runtime) {
    if (runtime.requestedLevel.empty()) {
        throw std::runtime_error("invalid_node_quantum_status_runtime_inputs");
    }
}

void validateNetworkHealthRuntimeInputs(const NodeNetworkHealthRuntimeInputs& runtime) {
    if (!runtime.networkHeight || !runtime.invBackpressureDrops ||
        !runtime.getDataBackpressureDrops || !runtime.gossipSuppressed ||
        !runtime.gossipSubsetRouted) {
        throw std::runtime_error("invalid_node_network_health_runtime_inputs");
    }
}

} // namespace

rpc::RpcNodeQuantumInputs collectNodeQuantumInputs(
    const NodeQuantumStatusRuntimeInputs& runtime) {
    validateQuantumRuntimeInputs(runtime);

    rpc::RpcNodeQuantumInputs inputs;
    inputs.enabled = runtime.enabled;
    inputs.requestedLevel = runtime.requestedLevel;
    inputs.capabilityMode = quantumCapabilityMode();
    inputs.liboqsEnabled = quantumLiboqsEnabled();

    const auto pqcStatus = quantum::getPQCBackendStatus();
    inputs.kyberReal = pqcStatus.kyberReal;
    inputs.dilithiumReal = pqcStatus.dilithiumReal;
    inputs.sphincsReal = pqcStatus.sphincsReal;
    inputs.kyberImplementation = quantumKyberImplementationMode();
    inputs.dilithiumImplementation = quantumDilithiumImplementationMode();
    inputs.sphincsImplementation = quantumSphincsImplementationMode();

    if (runtime.quantumManager) {
        const auto status = runtime.quantumManager->getRuntimeStatus();
        const auto counters = runtime.quantumManager->getStats();
        inputs.managerInitialized = status.initialized;
        inputs.isQuantumSafe = runtime.quantumManager->isQuantumSafe();
        inputs.effectiveLevel = quantumSecurityLevelToString(status.level);
        inputs.qkdConnected = status.qkdConnected;
        inputs.qkdSessionActive = status.qkdSessionActive;
        inputs.selectedKEM = quantumAlgorithmToString(status.selectedKEM);
        inputs.selectedSignature = quantumAlgorithmToString(status.selectedSignature);
        inputs.selectedEncryption = quantumAlgorithmToString(status.selectedEncryption);
        inputs.qkdEncryptOperations = status.qkdEncryptOperations;
        inputs.hybridEncryptOperations = status.hybridEncryptOperations;
        inputs.qkdDecryptOperations = status.qkdDecryptOperations;
        inputs.hybridDecryptOperations = status.hybridDecryptOperations;
        inputs.qkdFallbackDecryptOperations = status.qkdFallbackDecryptOperations;
        inputs.hybridOperations = counters.hybridOperations;
        inputs.qkdSessionsEstablished = counters.qkdSessionsEstablished;
        inputs.kyberEncapsulations = counters.kyberEncapsulations;
        inputs.kyberDecapsulations = counters.kyberDecapsulations;
        inputs.dilithiumSignatures = counters.dilithiumSignatures;
        inputs.dilithiumVerifications = counters.dilithiumVerifications;
        inputs.sphincsSignatures = counters.sphincsSignatures;
        inputs.sphincsVerifications = counters.sphincsVerifications;
        inputs.otpBytesUsed = counters.otpBytesUsed;
    }

    return inputs;
}

rpc::RpcNodeNetworkHealthInputs collectNodeNetworkHealthInputs(
    const NodeNetworkHealthRuntimeInputs& runtime) {
    validateNetworkHealthRuntimeInputs(runtime);

    rpc::RpcNodeNetworkHealthInputs inputs;
    if (!runtime.network) {
        return inputs;
    }

    const auto stats = runtime.network->getStats();
    const uint64_t peerPressure =
        runtime.maxPeers == 0 ? 0 : (stats.totalPeers * 100ULL / runtime.maxPeers);
    const uint64_t inboundPressure =
        runtime.maxInbound == 0 ? 0 : (stats.inboundPeers * 100ULL / runtime.maxInbound);
    const uint64_t outboundPressure =
        runtime.maxOutbound == 0 ? 0 : (stats.outboundPeers * 100ULL / runtime.maxOutbound);
    const uint64_t netHeight = runtime.networkHeight->load();
    const uint64_t consensusLag =
        netHeight > runtime.ledgerHeight ? (netHeight - runtime.ledgerHeight) : 0;

    inputs.available = true;
    inputs.totalPeers = stats.totalPeers;
    inputs.inboundPeers = stats.inboundPeers;
    inputs.outboundPeers = stats.outboundPeers;
    inputs.peerPressurePercent = peerPressure;
    inputs.inboundPressurePercent = inboundPressure;
    inputs.outboundPressurePercent = outboundPressure;
    inputs.bytesSent = stats.bytesSent;
    inputs.bytesReceived = stats.bytesReceived;
    inputs.messagesSent = stats.messagesSent;
    inputs.messagesReceived = stats.messagesReceived;
    inputs.overloadMode = stats.overloadMode;
    inputs.bufferedRxBytes = stats.bufferedRxBytes;
    inputs.rejectedConnections = stats.rejectedConnections;
    inputs.evictedPeers = stats.evictedPeers;
    inputs.tempBans = stats.tempBans;
    inputs.malformedMessages = stats.malformedMessages;
    inputs.rateLimitedEvents = stats.rateLimitedEvents;
    inputs.overloadTransitions = stats.overloadTransitions;
    inputs.activeBans = runtime.network->getBannedPeers().size();
    inputs.invBackpressureDrops = runtime.invBackpressureDrops->load();
    inputs.getdataBackpressureDrops = runtime.getDataBackpressureDrops->load();
    inputs.gossipSuppressed = runtime.gossipSuppressed->load();
    inputs.gossipSubsetRouted = runtime.gossipSubsetRouted->load();
    inputs.consensusLag = consensusLag;
    return inputs;
}

} // namespace synapse::node
