#!/usr/bin/env python3
"""AND-mode HybridSig combiner spec.

IETF draft-ietf-lamps-pq-composite-sigs-19 defines Composite ML-DSA
(id-MLDSA65-Ed25519-SHA512) for X.509/PKIX: both halves sign a labeled
SHA-512 representative. This stack is not X.509. Wire bytes stay
classic_sig || pqc_sig (AND-mode). Application envelopes already domain-
separate via buildApplicationSignatureTranscript.

Place Dilithium / ML-DSA-65:
  YES — KQAS identity / producer / vote trailers (HybridSig AND-mode)
  NO  — MLSAG / RingCT / stealth spends (a 5KB envelope links the signer)
  NO  — handshake.cpp / NODE_MSG (Kyber+X25519 only; no Dilithium trailer)

MSG peer advertise stays unsigned: NODE_PROFILE already carries Kyber+X25519
kem_pk over Tor. Do not glue Dilithium onto private_transfer.
"""
from __future__ import annotations

from dataclasses import dataclass

# Must match KeplerSynapseNet/src/quantum/application_signature.cpp
DOMAIN_LABEL = b"synapsenet-application-signature-v1"
SUITE_LABEL = b"ed25519+ml-dsa-65"
APPLICATION_DOMAIN_LABEL = DOMAIN_LABEL.decode("ascii")
SIG_SUITE = SUITE_LABEL.decode("ascii")

# FIPS 204 ML-DSA-65 only. Dilithium2 / ML-DSA-44 = 2420; Dilithium5 / ML-DSA-87 = 4595.
CLASSIC_SIGNATURE_SIZE = 64
ML_DSA_65_PUBLIC_KEY_SIZE = 1952
ML_DSA_65_SECRET_KEY_SIZE = 4032
ML_DSA_65_SIGNATURE_SIZE = 3309
ML_DSA_PARAMETER_SET = "ML-DSA-65"
REJECTED_PQC_SETS = ("ML-DSA-44", "ML-DSA-87", "Dilithium2", "Dilithium5")
COMBINER_MODE = "AND"

# KQAS trailer domains. Handshake/NODE_MSG have none.
KQAS_DOMAIN_VOTE = "core.consensus.vote"
KQAS_DOMAIN_POE_VOTE = "core.poe.validation_vote"
KQAS_DOMAIN_PRODUCER = "core.block.producer"
KQAS_DOMAIN_TRANSFER = "core.transfer.transaction"


class CombinerError(ValueError):
    pass


class MissingHalfError(CombinerError):
    pass


class TruncatedClassicError(CombinerError):
    pass


class SwappedHalvesError(CombinerError):
    pass


class RejectedParameterSetError(CombinerError):
    pass


@dataclass(frozen=True)
class HybridPolicy:
    domain_label: str = APPLICATION_DOMAIN_LABEL
    suite: str = SIG_SUITE
    classic_sig_bytes: int = CLASSIC_SIGNATURE_SIZE
    ml_dsa_65_sig_bytes: int = ML_DSA_65_SIGNATURE_SIZE
    ml_dsa_65_pk_bytes: int = ML_DSA_65_PUBLIC_KEY_SIZE
    parameter_set: str = ML_DSA_PARAMETER_SET
    and_mode: bool = True
    combiner_mode: str = COMBINER_MODE


POLICY = HybridPolicy()


def pin_ml_dsa_65(parameter_set: str) -> str:
    """Reject mixed lattice parameter sets. HybridSig is ML-DSA-65 only.

    Dilithium3 is the round-3 name for the same parameter set as ML-DSA-65.
    """
    if parameter_set in ("ML-DSA-65", "Dilithium3"):
        return ML_DSA_PARAMETER_SET
    if parameter_set in REJECTED_PQC_SETS:
        raise RejectedParameterSetError(
            "HybridSig is %s only, not %s" % (ML_DSA_PARAMETER_SET, parameter_set)
        )
    raise RejectedParameterSetError(
        "unknown PQC parameter set %s (want %s)" % (parameter_set, ML_DSA_PARAMETER_SET)
    )


def is_ml_dsa_65_signature_size(n: int) -> bool:
    return n == ML_DSA_65_SIGNATURE_SIZE


def combine_and(classic_sig: bytes, pqc_sig: bytes) -> bytes:
    """Concatenate halves in canonical order. Both halves are required."""
    if not classic_sig:
        raise MissingHalfError("AND-mode requires the Ed25519 half")
    if not pqc_sig:
        raise MissingHalfError("AND-mode requires the ML-DSA-65 half")
    if len(classic_sig) != CLASSIC_SIGNATURE_SIZE:
        raise TruncatedClassicError(
            "classic half must be %d-byte Ed25519, got %d"
            % (CLASSIC_SIGNATURE_SIZE, len(classic_sig))
        )
    return classic_sig + pqc_sig


def split_and(combined: bytes) -> tuple:
    """Split canonical wire bytes. Does not try pqc||classic (no OR-mode)."""
    if len(combined) < CLASSIC_SIGNATURE_SIZE:
        raise TruncatedClassicError("need a 64-byte Ed25519 prefix")
    classic = combined[:CLASSIC_SIGNATURE_SIZE]
    pqc = combined[CLASSIC_SIGNATURE_SIZE:]
    if not pqc:
        raise MissingHalfError("AND-mode requires the ML-DSA-65 half")
    return classic, pqc


def classic_half(signature: bytes) -> bytes:
    return signature[:CLASSIC_SIGNATURE_SIZE]


def pqc_half(signature: bytes) -> bytes:
    return signature[CLASSIC_SIGNATURE_SIZE:]


def both_halves_present(signature: bytes) -> bool:
    return len(signature) > CLASSIC_SIGNATURE_SIZE and len(pqc_half(signature)) > 0


def layout_of(combined: bytes, classic_sig: bytes, pqc_sig: bytes) -> str:
    """Classify concatenation order. AND-mode only accepts canonical."""
    if combined == classic_sig + pqc_sig:
        return "canonical"
    if combined == pqc_sig + classic_sig:
        return "swapped"
    return "unknown"


def and_mode_accepts(classic_ok: bool, pqc_ok: bool, layout: str = "canonical") -> bool:
    """Both halves must verify, in canonical order. Classic-only is a failure."""
    if layout != "canonical":
        return False
    return bool(classic_ok) and bool(pqc_ok)


def and_verify(classic_ok: bool, pqc_ok: bool, signature: bytes) -> bool:
    if not both_halves_present(signature):
        return False
    return bool(classic_ok) and bool(pqc_ok)


def require_canonical_layout(combined: bytes, classic_sig: bytes, pqc_sig: bytes) -> None:
    layout = layout_of(combined, classic_sig, pqc_sig)
    if layout == "swapped":
        raise SwappedHalvesError("AND-mode does not accept pqc||classic")
    if layout != "canonical":
        raise CombinerError("signature is not classic||pqc")


def print_policy() -> None:
    print("HybridSig combiner policy")
    print("  mode:        " + COMBINER_MODE + " (classic AND pqc)")
    print("  domain:      " + APPLICATION_DOMAIN_LABEL)
    print("  suite:       " + SIG_SUITE)
    print("  parameter:   " + ML_DSA_PARAMETER_SET + " (FIPS 204)")
    print("  classic:     Ed25519, %d bytes" % CLASSIC_SIGNATURE_SIZE)
    print("  pqc:         ML-DSA-65 (FIPS 204), %d bytes typical" % ML_DSA_65_SIGNATURE_SIZE)
    print("  wire:        classic_sig || pqc_sig")
    print("  kqas:        vote/poe/producer/transfer trailers")
    print("  mlsag/ringct: classical only (no Dilithium on spends)")
    print("  msg advertise: skipped (kem_pk stays Kyber+X25519 over Tor)")
    print("  handshake:   Kyber+X25519 (no Dilithium trailer)")


if __name__ == "__main__":
    print_policy()
