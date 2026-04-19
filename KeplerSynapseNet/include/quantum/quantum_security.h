#ifndef SYNAPSE_QUANTUM_SECURITY_H
#define SYNAPSE_QUANTUM_SECURITY_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <array>
#include <cstdint>

namespace synapse {
namespace quantum {

constexpr size_t KYBER_PUBLIC_KEY_SIZE = 1184;
constexpr size_t KYBER_SECRET_KEY_SIZE = 2400;
constexpr size_t KYBER_CIPHERTEXT_SIZE = 1088;
constexpr size_t KYBER_SHARED_SECRET_SIZE = 32;

constexpr size_t DILITHIUM_PUBLIC_KEY_SIZE = 1952;
constexpr size_t DILITHIUM_SECRET_KEY_SIZE = 4032;
constexpr size_t DILITHIUM_SIGNATURE_SIZE = 3309;

constexpr size_t SPHINCS_PUBLIC_KEY_SIZE = 32;
constexpr size_t SPHINCS_SECRET_KEY_SIZE = 64;
constexpr size_t SPHINCS_SIGNATURE_SIZE = 7856;

enum class SecurityLevel {
    STANDARD = 0,
    HIGH = 1,
    PARANOID = 2,
    QUANTUM_READY = 3
};

enum class CryptoAlgorithm {
    CLASSIC_ED25519,
    CLASSIC_X25519,
    CLASSIC_AES256GCM,
    LATTICE_KYBER768,
    LATTICE_DILITHIUM65,
    HASH_SPHINCS128S,
    OTP_VERNAM,
    QKD_BB84,
    HYBRID_KEM,
    HYBRID_SIG
};

using KyberPublicKey = std::array<uint8_t, KYBER_PUBLIC_KEY_SIZE>;
using KyberSecretKey = std::array<uint8_t, KYBER_SECRET_KEY_SIZE>;
using KyberCiphertext = std::array<uint8_t, KYBER_CIPHERTEXT_SIZE>;
using KyberSharedSecret = std::array<uint8_t, KYBER_SHARED_SECRET_SIZE>;

using DilithiumPublicKey = std::array<uint8_t, DILITHIUM_PUBLIC_KEY_SIZE>;
using DilithiumSecretKey = std::array<uint8_t, DILITHIUM_SECRET_KEY_SIZE>;
using DilithiumSignature = std::array<uint8_t, DILITHIUM_SIGNATURE_SIZE>;

using SphincsPublicKey = std::array<uint8_t, SPHINCS_PUBLIC_KEY_SIZE>;
using SphincsSecretKey = std::array<uint8_t, SPHINCS_SECRET_KEY_SIZE>;
using SphincsSignature = std::array<uint8_t, SPHINCS_SIGNATURE_SIZE>;

struct KyberKeyPair {
    KyberPublicKey publicKey;
    KyberSecretKey secretKey;
};

struct DilithiumKeyPair {
    DilithiumPublicKey publicKey;
    DilithiumSecretKey secretKey;
};

struct SphincsKeyPair {
    SphincsPublicKey publicKey;
    SphincsSecretKey secretKey;
};

struct HybridKeyPair {
    std::vector<uint8_t> classicPublicKey;
    std::vector<uint8_t> classicSecretKey;
    std::vector<uint8_t> pqcPublicKey;
    std::vector<uint8_t> pqcSecretKey;
    CryptoAlgorithm classicAlgo;
    CryptoAlgorithm pqcAlgo;
};

struct EncapsulationResult {
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> sharedSecret;
    bool success;
};

struct SignatureResult {
    std::vector<uint8_t> signature;
    bool success;
};

struct OTPKey {
    std::vector<uint8_t> keyMaterial;
    uint64_t keyId;
    uint64_t createdAt;
    uint64_t expiresAt;
    size_t usedBytes;
    bool exhausted;
};

struct QKDSession {
    std::string sessionId;
    std::string remoteNodeId;
    std::vector<uint8_t> sharedKey;
    uint64_t establishedAt;
    uint64_t keyBitsGenerated;
    double errorRate;
    bool active;
};

struct QuantumStats {
    uint64_t kyberEncapsulations;
    uint64_t kyberDecapsulations;
    uint64_t dilithiumSignatures;
    uint64_t dilithiumVerifications;
    uint64_t sphincsSignatures;
    uint64_t sphincsVerifications;
    uint64_t otpBytesUsed;
    uint64_t qkdSessionsEstablished;
    uint64_t hybridOperations;
};

struct PQCBackendStatus {
    bool kyberReal = false;
    bool dilithiumReal = false;
    bool sphincsReal = false;
};

struct QuantumRuntimeStatus {
    bool initialized = false;
    SecurityLevel level = SecurityLevel::STANDARD;
    bool qkdConnected = false;
    bool qkdSessionActive = false;
    CryptoAlgorithm selectedKEM = CryptoAlgorithm::CLASSIC_X25519;
    CryptoAlgorithm selectedSignature = CryptoAlgorithm::CLASSIC_ED25519;
    CryptoAlgorithm selectedEncryption = CryptoAlgorithm::CLASSIC_AES256GCM;
    uint64_t qkdEncryptOperations = 0;
    uint64_t hybridEncryptOperations = 0;
    uint64_t qkdDecryptOperations = 0;
    uint64_t hybridDecryptOperations = 0;
    uint64_t qkdFallbackDecryptOperations = 0;
    PQCBackendStatus pqc;
};

PQCBackendStatus getPQCBackendStatus();

class Kyber {
public:
    Kyber();
    ~Kyber();
    
    KyberKeyPair generateKeyPair();
    EncapsulationResult encapsulate(const KyberPublicKey& publicKey);
    std::vector<uint8_t> decapsulate(const KyberCiphertext& ciphertext, 
                                      const KyberSecretKey& secretKey);
    
    bool validatePublicKey(const KyberPublicKey& publicKey);
    bool validateSecretKey(const KyberSecretKey& secretKey);
    
    std::vector<uint8_t> serializePublicKey(const KyberPublicKey& key);
    std::vector<uint8_t> serializeSecretKey(const KyberSecretKey& key);
    KyberPublicKey deserializePublicKey(const std::vector<uint8_t>& data);
    KyberSecretKey deserializeSecretKey(const std::vector<uint8_t>& data);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Dilithium {
public:
    Dilithium();
    ~Dilithium();
    
    DilithiumKeyPair generateKeyPair();
    SignatureResult sign(const std::vector<uint8_t>& message, 
                         const DilithiumSecretKey& secretKey);
    bool verify(const std::vector<uint8_t>& message,
                const DilithiumSignature& signature,
                const DilithiumPublicKey& publicKey);
    
    bool validatePublicKey(const DilithiumPublicKey& publicKey);
    bool validateSecretKey(const DilithiumSecretKey& secretKey);
    
    std::vector<uint8_t> serializePublicKey(const DilithiumPublicKey& key);
    std::vector<uint8_t> serializeSecretKey(const DilithiumSecretKey& key);
    DilithiumPublicKey deserializePublicKey(const std::vector<uint8_t>& data);
    DilithiumSecretKey deserializeSecretKey(const std::vector<uint8_t>& data);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Sphincs {
public:
    Sphincs();
    ~Sphincs();
    
    SphincsKeyPair generateKeyPair();
    SignatureResult sign(const std::vector<uint8_t>& message,
                         const SphincsSecretKey& secretKey);
    bool verify(const std::vector<uint8_t>& message,
                const SphincsSignature& signature,
                const SphincsPublicKey& publicKey);
    
    bool validatePublicKey(const SphincsPublicKey& publicKey);
    bool validateSecretKey(const SphincsSecretKey& secretKey);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class HybridKEM {
public:
    HybridKEM();
    ~HybridKEM();
    
    HybridKeyPair generateKeyPair();
    EncapsulationResult encapsulate(const HybridKeyPair& recipientPublicKey);
    std::vector<uint8_t> decapsulate(const std::vector<uint8_t>& ciphertext,
                                      const HybridKeyPair& secretKey);
    
    void setClassicAlgorithm(CryptoAlgorithm algo);
    void setPQCAlgorithm(CryptoAlgorithm algo);
    
    std::vector<uint8_t> combineSharedSecrets(const std::vector<uint8_t>& classicSecret,
                                               const std::vector<uint8_t>& pqcSecret);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class HybridSig {
public:
    HybridSig();
    ~HybridSig();
    
    HybridKeyPair generateKeyPair();
    SignatureResult sign(const std::vector<uint8_t>& message,
                         const HybridKeyPair& secretKey);
    bool verify(const std::vector<uint8_t>& message,
                const std::vector<uint8_t>& signature,
                const HybridKeyPair& publicKey);
    
    void setClassicAlgorithm(CryptoAlgorithm algo);
    void setPQCAlgorithm(CryptoAlgorithm algo);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OTP {
public:
    OTP();
    ~OTP();
    
    OTPKey generateKey(size_t length);
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, OTPKey& key);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, OTPKey& key);
    
    bool isKeyExhausted(const OTPKey& key) const;
    size_t remainingBytes(const OTPKey& key) const;
    
    void setKeyExpiry(uint64_t seconds);
    bool isKeyExpired(const OTPKey& key) const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OTPManager {
public:
    OTPManager();
    ~OTPManager();
    
    bool init(const std::string& storagePath);
    void shutdown();
    
    uint64_t createKey(size_t length);
    OTPKey getKey(uint64_t keyId);
    bool deleteKey(uint64_t keyId);
    
    std::vector<uint8_t> encryptWithKey(uint64_t keyId, const std::vector<uint8_t>& data);
    std::vector<uint8_t> decryptWithKey(uint64_t keyId, const std::vector<uint8_t>& data);
    
    void pruneExpiredKeys();
    void pruneExhaustedKeys();
    
    size_t keyCount() const;
    size_t totalKeyMaterial() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class HWRNG {
public:
    HWRNG();
    ~HWRNG();
    
    bool init();
    bool isAvailable() const;
    
    std::vector<uint8_t> generate(size_t length);
    void fill(uint8_t* buffer, size_t length);
    
    bool hasRDRAND() const;
    bool hasRDSEED() const;
    bool hasTPM() const;
    
    void setFallbackToDevRandom(bool enable);
    void setEntropyMixing(bool enable);
    
    struct RNGStats {
        uint64_t bytesGenerated;
        uint64_t rdrandCalls;
        uint64_t rdseedCalls;
        uint64_t devRandomCalls;
        bool hardwareAvailable;
    };
    RNGStats getStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class DeterministicOTP {
public:
    DeterministicOTP();
    ~DeterministicOTP();
    
    void setSeed(const std::vector<uint8_t>& seed);
    void setCounter(uint64_t counter);
    
    std::vector<uint8_t> deriveKey(size_t length);
    std::vector<uint8_t> deriveKeyAt(uint64_t index, size_t length);
    
    void advance(uint64_t steps = 1);
    uint64_t currentCounter() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class QKDIntegration {
public:
    QKDIntegration();
    ~QKDIntegration();
    
    bool init(const std::string& devicePath);
    void shutdown();
    bool isConnected() const;
    
    QKDSession establishSession(const std::string& remoteNodeId);
    bool closeSession(const std::string& sessionId);
    
    std::vector<uint8_t> getKeyMaterial(const std::string& sessionId, size_t length);
    double getErrorRate(const std::string& sessionId) const;
    
    void setMinKeyRate(uint64_t bitsPerSecond);
    void setMaxErrorRate(double rate);
    
    void onSessionEstablished(std::function<void(const QKDSession&)> callback);
    void onSessionClosed(std::function<void(const std::string&)> callback);
    void onKeyGenerated(std::function<void(const std::string&, size_t)> callback);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class QKDBB84 {
public:
    QKDBB84();
    ~QKDBB84();
    
    bool initAlice();
    bool initBob();
    
    std::vector<uint8_t> generateQubits(size_t count);
    std::vector<uint8_t> measureQubits(const std::vector<uint8_t>& qubits,
                                        const std::vector<uint8_t>& bases);
    
    std::vector<uint8_t> siftKey(const std::vector<uint8_t>& rawKey,
                                  const std::vector<uint8_t>& aliceBases,
                                  const std::vector<uint8_t>& bobBases);
    
    double estimateErrorRate(const std::vector<uint8_t>& aliceKey,
                             const std::vector<uint8_t>& bobKey,
                             size_t sampleSize);
    
    std::vector<uint8_t> privacyAmplification(const std::vector<uint8_t>& siftedKey,
                                               double errorRate);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class QKDRelay {
public:
    QKDRelay();
    ~QKDRelay();
    
    bool init(const std::string& nodeId);
    void shutdown();
    
    bool addTrustedRelay(const std::string& relayId, const std::vector<uint8_t>& publicKey);
    bool removeTrustedRelay(const std::string& relayId);
    
    std::vector<uint8_t> relayKey(const std::string& sourceId,
                                   const std::string& destId,
                                   const std::vector<uint8_t>& encryptedKey);
    
    std::vector<std::string> findPath(const std::string& sourceId,
                                       const std::string& destId);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class CryptoSelector {
public:
    CryptoSelector();
    ~CryptoSelector();
    
    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;
    
    CryptoAlgorithm selectKEM() const;
    CryptoAlgorithm selectSignature() const;
    CryptoAlgorithm selectEncryption() const;
    
    bool isAlgorithmAvailable(CryptoAlgorithm algo) const;
    void disableAlgorithm(CryptoAlgorithm algo);
    void enableAlgorithm(CryptoAlgorithm algo);
    
    std::vector<CryptoAlgorithm> getAvailableKEMs() const;
    std::vector<CryptoAlgorithm> getAvailableSignatures() const;
    std::vector<CryptoAlgorithm> getAvailableEncryptions() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class KeyDerivation {
public:
    KeyDerivation();
    ~KeyDerivation();
    
    std::vector<uint8_t> hkdfExtract(const std::vector<uint8_t>& salt,
                                      const std::vector<uint8_t>& ikm);
    std::vector<uint8_t> hkdfExpand(const std::vector<uint8_t>& prk,
                                     const std::vector<uint8_t>& info,
                                     size_t length);
    std::vector<uint8_t> hkdf(const std::vector<uint8_t>& salt,
                               const std::vector<uint8_t>& ikm,
                               const std::vector<uint8_t>& info,
                               size_t length);
    
    std::vector<uint8_t> pbkdf2(const std::string& password,
                                 const std::vector<uint8_t>& salt,
                                 uint32_t iterations,
                                 size_t length);
    
    std::vector<uint8_t> argon2id(const std::string& password,
                                   const std::vector<uint8_t>& salt,
                                   uint32_t timeCost,
                                   uint32_t memoryCost,
                                   uint32_t parallelism,
                                   size_t length);
    
    std::vector<uint8_t> mixEntropy(const std::vector<std::vector<uint8_t>>& sources);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class TimingDefense {
public:
    TimingDefense();
    ~TimingDefense();
    
    void constantTimeCompare(const uint8_t* a, const uint8_t* b, size_t length, bool& equal);
    void constantTimeCopy(uint8_t* dest, const uint8_t* src, size_t length);
    void constantTimeSelect(uint8_t* dest, const uint8_t* a, const uint8_t* b,
                            size_t length, bool selectA);
    
    void addRandomDelay(uint32_t minMicros, uint32_t maxMicros);
    void normalizeResponseTime(uint64_t targetMicros);
    
    void enableTimingProtection(bool enable);
    bool isTimingProtectionEnabled() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class QuantumSecurityManager {
public:
    QuantumSecurityManager();
    ~QuantumSecurityManager();
    
    bool init(SecurityLevel level = SecurityLevel::HIGH);
    void shutdown();
    
    Kyber& kyber();
    Dilithium& dilithium();
    Sphincs& sphincs();
    HybridKEM& hybridKEM();
    HybridSig& hybridSig();
    OTPManager& otpManager();
    HWRNG& hwrng();
    QKDIntegration& qkd();
    CryptoSelector& selector();
    KeyDerivation& kdf();
    TimingDefense& timing();
    
    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;
    
    QuantumStats getStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class QuantumManager {
public:
    QuantumManager();
    ~QuantumManager();
    
    bool init(SecurityLevel level = SecurityLevel::STANDARD);
    void shutdown();
    
    void performMaintenance();
    void rotateKeys();
    void pruneExpiredKeys();
    
    std::vector<uint8_t> generateQuantumSafeKey(size_t length);
    std::vector<uint8_t> encryptQuantumSafe(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decryptQuantumSafe(const std::vector<uint8_t>& data);
    
    std::vector<uint8_t> signQuantumSafe(const std::vector<uint8_t>& message);
    bool verifyQuantumSafe(const std::vector<uint8_t>& message, 
                           const std::vector<uint8_t>& signature);
    
    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;
    
    bool isQuantumSafe() const;
    QuantumStats getStats() const;
    QuantumRuntimeStatus getRuntimeStatus() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}

#endif
