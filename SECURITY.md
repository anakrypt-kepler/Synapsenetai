# Security Policy

## Supported Versions

`main` is the only supported branch. Security fixes are not backported to released tags.

## Reporting a Vulnerability

Please do **not** open a public GitHub issue for security findings.

Send a detailed report (including reproduction steps, affected commit hash, and suggested mitigation if any) to the maintainers via a GitHub private security advisory on this repository, or to the PGP key published alongside repository releases. We target acknowledgement within 72 hours.

## Post-Quantum Cryptography: Current State

This project ships a hybrid classical+post-quantum (PQ) crypto stack backed by [liboqs](https://github.com/open-quantum-safe/liboqs). **liboqs is mandatory for all build types** — CMake will fail at configure time if it cannot find or fetch liboqs. Simulated / hash-based PQC fallback implementations have been removed; every cryptographic operation requires a real liboqs backend.

### Primitives in use

- **Key exchange**: X25519 (classical) + ML-KEM-768 (PQ). Session key is derived by an HKDF-SHA256 combiner documented below.
- **Signatures**: Ed25519 (classical) + ML-DSA-65 (PQ) exposed as a `HybridSig` compound. The signature bytes on the wire are `classical_sig || pqc_sig` concatenated and verified independently on both halves.
- **Block producer signature**: same `HybridSig` suite, emitted inside an `ApplicationSignatureEnvelope` bound to the producer public key.
- **Wallet at rest**: AES-256-GCM + PBKDF2-HMAC-SHA256 at 600 000 iterations (format v3). v1 and v2 wallets lazy-migrate to v3 on first unlock.

### Session key combiner

The hybrid session key derivation (see `deriveHandshakeSessionKey` in `KeplerSynapseNet/src/network/handshake.cpp`) is:

```
salt  = min(localNonce, remoteNonce) || max(localNonce, remoteNonce)
info  = "synapse-handshake-session-key-v2"
     || suiteId
     || length-prefixed("domain",             ...)
     || length-prefixed("sig-suite",          "ed25519+ml-dsa-65")
     || length-prefixed("classic-public-key", firstPub || secondPub)
     || pqcLen (u32 LE)
     || pqcSharedSecret
     || 0x01
ikm   = x25519SharedSecret || kyberSharedSecret
prk   = HMAC-SHA256(salt, ikm)
key   = HMAC-SHA256(prk, info)
```

Notes:

- `x25519SharedSecret` is always 32 bytes. `kyberSharedSecret` is either 32 bytes (successful KEM) or empty (no PQ material exchanged).
- The combiner is domain-separated from any prior handshake version via the `synapse-handshake-session-key-v2` label.
- When `SYNAPSE_HANDSHAKE_PQ_STRICT=1`, a session refuses to derive a key unless `kyberSharedSecret` is 32 bytes, closing the "silent fall-back to classical-only" gap.

### Application signature envelope

Block producer sigs, transaction PQ sigs, vote PQ sigs, and on-chain `IDENTITY_BIND` events all use the same envelope format (`quantum::ApplicationSignatureEnvelope` in `include/quantum/application_signature.h`):

```
magic        (u32, big-endian) = 0x4B514153 ("KQAS")
suite_id     (u32)             = 0x53505131
domain_len   (u32) | domain (UTF-8)
classic_pk_len (u32) | classic_pk                        (32 bytes ed25519)
pqc_pk_len     (u32) | pqc_pk                            (ML-DSA-65 public key)
sig_len        (u32) | classic_sig || pqc_sig            (Ed25519 || ML-DSA-65 signature)
```

The signed transcript binds the domain, payload, binding bytes (caller-chosen context, e.g. producer pubkey or tx input pubkey), both public keys and a fixed protocol label, so a signature valid for `core.transfer.transaction` cannot be replayed as `core.consensus.vote` or `core.block.producer`.

### Identity binding

An `IdentityRegistry` (singleton, on-disk JSON in `<data>/identities.json`) maps wallet addresses to hybrid identity ids (`sha256(classic_pk):sha256(pqc_pk)`). Bindings are seeded Trust-On-First-Use either:

1. When `submitTransaction` sees the first hybrid-signed transaction from an address, or
2. When a block containing an `EventType::IDENTITY_BIND` is appended and the event's `data` field is a valid envelope signed with the event author's hybrid key.

Once a binding is recorded, any subsequent tx/vote/event from that address whose envelope does not resolve to the same identity id is rejected.

### PQ cutover

`core::BLOCK_PQ_MANDATORY_HEIGHT = 1_000_000`. At or above this height, `Ledger::verifyBlock` requires a v2 block carrying a non-zero producer pubkey, a verifying classical producer signature, and a valid `producerQuantumSignature` envelope. Mining emits v2 blocks with full signatures either past the cutover automatically, or when the operator sets `SYNAPSE_EMIT_PQ_BLOCKS=1`.

## Known PQ Gaps

The following items are tracked publicly:

- **Closed** — Deterministic derivation of the hybrid keypair. `HybridSig::generateKeyPairFromSeed` derives the ed25519 + ML-DSA-65 pair from a master seed via HKDF-SHA256 (classical half) plus a SHAKE256 stream fed into a thread-local deterministic `OQS_randombytes` hook (PQ half). `core::Wallet` seeds this from the PBKDF2 master seed derived off the BIP-39 mnemonic and `crypto::Keys` seeds it from the ed25519 private key bytes, so the full hybrid identity is reconstructable from the mnemonic alone; the previous `<wallet>.pq` sidecar file has been removed.
- **Closed** — `ValidationVoteV1` (PoE pipeline) now supports an optional `quantumSignature` envelope. The wire layout stays single-byte-versioned: a trailing tag byte `0x01` followed by a `u32 LE` length and the envelope bytes is appended when hybrid signing is used. Legacy votes without the trailer continue to deserialize and verify. `signValidationVoteV1(vote, validatorKey, quantumKeyPair)` attaches an envelope bound to the validator pubkey under the `core.poe.validation_vote` domain.
- External cryptographic audit of the HKDF combiner, envelope canonicalisation, and block signing domain separation.

## Threat Model Out of Scope

- Compromise of the host OS running the node.
- Side-channel attacks on the underlying liboqs/OpenSSL installations — we rely on upstream hardening.
- Denial-of-service through lawful but expensive protocol operations (mitigated best-effort, not guaranteed).

## Reproducible builds

CI build `PQC Real Backend (liboqs)` fetches liboqs 0.12.0 via `FetchContent`, pins the minimal algorithm set to `KEM_ml_kem_768;SIG_ml_dsa_65;SIG_slh_dsa_sha2_128s`, builds Release with PQ required, runs the `QuantumTests`, `QuantumRuntimeTests`, `PqcCiValidationTests` and `PqIntegrationTests` suites, and asserts the `synapsed` binary is produced.
