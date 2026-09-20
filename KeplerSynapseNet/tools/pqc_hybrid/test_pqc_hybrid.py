#!/usr/bin/env python3
# Combiner policy tests. No liboqs. Run:
#   python3 -m unittest KeplerSynapseNet/tools/pqc_hybrid/test_pqc_hybrid.py -v
from __future__ import annotations

import io
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
KEPLER = HERE.parents[1]
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from combiner import (
    APPLICATION_DOMAIN_LABEL,
    CLASSIC_SIGNATURE_SIZE,
    COMBINER_MODE,
    DOMAIN_LABEL,
    KQAS_DOMAIN_POE_VOTE,
    KQAS_DOMAIN_PRODUCER,
    KQAS_DOMAIN_TRANSFER,
    KQAS_DOMAIN_VOTE,
    ML_DSA_65_PUBLIC_KEY_SIZE,
    ML_DSA_65_SECRET_KEY_SIZE,
    ML_DSA_65_SIGNATURE_SIZE,
    ML_DSA_PARAMETER_SET,
    POLICY,
    REJECTED_PQC_SETS,
    SIG_SUITE,
    SUITE_LABEL,
    MissingHalfError,
    RejectedParameterSetError,
    SwappedHalvesError,
    TruncatedClassicError,
    and_mode_accepts,
    and_verify,
    both_halves_present,
    combine_and,
    is_ml_dsa_65_signature_size,
    layout_of,
    pin_ml_dsa_65,
    print_policy,
    require_canonical_layout,
    split_and,
)


class PqcHybridCombinerTests(unittest.TestCase):
    def setUp(self):
        self.classic = bytes([0x11]) * CLASSIC_SIGNATURE_SIZE
        self.pqc = bytes([0x22]) * ML_DSA_65_SIGNATURE_SIZE

    def test_domain_labels_match_cpp_transcript(self):
        self.assertEqual(DOMAIN_LABEL, b"synapsenet-application-signature-v1")
        self.assertEqual(SUITE_LABEL, b"ed25519+ml-dsa-65")
        self.assertEqual(APPLICATION_DOMAIN_LABEL, "synapsenet-application-signature-v1")
        self.assertEqual(SIG_SUITE, "ed25519+ml-dsa-65")
        self.assertEqual(COMBINER_MODE, "AND")
        self.assertTrue(POLICY.and_mode)
        self.assertEqual(CLASSIC_SIGNATURE_SIZE, 64)
        self.assertEqual(ML_DSA_65_SIGNATURE_SIZE, 3309)
        self.assertEqual(ML_DSA_65_PUBLIC_KEY_SIZE, 1952)
        self.assertEqual(ML_DSA_65_SECRET_KEY_SIZE, 4032)
        self.assertEqual(ML_DSA_PARAMETER_SET, "ML-DSA-65")

        app = (KEPLER / "src" / "quantum" / "application_signature.cpp").read_text()
        self.assertIn(APPLICATION_DOMAIN_LABEL, app)
        self.assertIn(SIG_SUITE, app)

    def test_pin_ml_dsa_65_only(self):
        self.assertEqual(pin_ml_dsa_65("ML-DSA-65"), "ML-DSA-65")
        self.assertEqual(pin_ml_dsa_65("Dilithium3"), "ML-DSA-65")
        self.assertTrue(is_ml_dsa_65_signature_size(3309))
        self.assertFalse(is_ml_dsa_65_signature_size(2420))
        self.assertFalse(is_ml_dsa_65_signature_size(3293))
        self.assertFalse(is_ml_dsa_65_signature_size(4595))
        for rejected in REJECTED_PQC_SETS:
            with self.assertRaises(RejectedParameterSetError):
                pin_ml_dsa_65(rejected)
        hybrid = (KEPLER / "src" / "quantum" / "hybrid_sig.cpp").read_text()
        self.assertIn("ML-DSA-65", hybrid)
        self.assertIn("not ML-DSA-44/87", hybrid)
        sizes = (KEPLER / "include" / "quantum" / "quantum_security.h").read_text()
        self.assertIn("DILITHIUM_PUBLIC_KEY_SIZE = 1952", sizes)
        self.assertIn("DILITHIUM_SECRET_KEY_SIZE = 4032", sizes)
        self.assertIn("DILITHIUM_SIGNATURE_SIZE = 3309", sizes)

    def test_and_mode_requires_both_halves(self):
        combined = combine_and(self.classic, self.pqc)
        classic, pqc = split_and(combined)
        self.assertEqual(classic, self.classic)
        self.assertEqual(pqc, self.pqc)
        self.assertTrue(both_halves_present(combined))
        self.assertTrue(and_mode_accepts(True, True))
        self.assertTrue(and_verify(True, True, combined))
        self.assertFalse(and_mode_accepts(True, False))
        self.assertFalse(and_mode_accepts(False, True))
        self.assertFalse(and_mode_accepts(False, False))
        self.assertFalse(and_verify(True, False, combined))
        self.assertFalse(and_verify(False, True, combined))

    def test_reject_missing_half(self):
        with self.assertRaises(MissingHalfError):
            combine_and(self.classic, b"")
        with self.assertRaises(MissingHalfError):
            combine_and(b"", self.pqc)
        with self.assertRaises(MissingHalfError):
            split_and(self.classic)
        self.assertFalse(both_halves_present(self.classic))
        self.assertFalse(and_verify(True, True, self.classic))

    def test_reject_truncated_classic_prefix(self):
        with self.assertRaises(TruncatedClassicError):
            split_and(b"\x00" * 63)
        with self.assertRaises(TruncatedClassicError):
            combine_and(self.classic[:32], self.pqc)

    def test_reject_swapped_halves(self):
        canonical = combine_and(self.classic, self.pqc)
        swapped = self.pqc + self.classic
        self.assertEqual(layout_of(canonical, self.classic, self.pqc), "canonical")
        self.assertEqual(layout_of(swapped, self.classic, self.pqc), "swapped")
        self.assertTrue(and_mode_accepts(True, True, "canonical"))
        self.assertFalse(and_mode_accepts(True, True, "swapped"))
        require_canonical_layout(canonical, self.classic, self.pqc)
        with self.assertRaises(SwappedHalvesError):
            require_canonical_layout(swapped, self.classic, self.pqc)
        prefix, remainder = split_and(swapped)
        self.assertNotEqual(prefix, self.classic)
        self.assertNotEqual(remainder, self.pqc)

    def test_split_uses_remaining_pqc_bytes(self):
        short_pqc = bytes([0x33]) * 100
        combined = combine_and(self.classic, short_pqc)
        classic, pqc = split_and(combined)
        self.assertEqual(len(classic), CLASSIC_SIGNATURE_SIZE)
        self.assertEqual(pqc, short_pqc)

    def test_print_policy_mentions_and_mode(self):
        buf = io.StringIO()
        old = sys.stdout
        sys.stdout = buf
        try:
            print_policy()
        finally:
            sys.stdout = old
        text = buf.getvalue()
        self.assertIn("AND", text)
        self.assertIn("synapsenet-application-signature-v1", text)
        self.assertIn("ed25519+ml-dsa-65", text)
        self.assertIn("ML-DSA-65", text)
        self.assertIn("Kyber+X25519", text)

    def test_stealth_spend_must_not_carry_dilithium(self):
        priv = (KEPLER / "src" / "privacy" / "private_transfer.cpp").read_text()
        self.assertIn("result.tx.pqcSig.clear()", priv)
        self.assertNotIn("signHybrid", priv)
        transfer = (KEPLER / "src" / "core" / "transfer.cpp").read_text()
        self.assertIn("tx.isRingCtSpend()", transfer)
        self.assertIn("Dilithium on this path fingerprints the signer", transfer)
        hybrid = (KEPLER / "src" / "quantum" / "hybrid_sig.cpp").read_text()
        self.assertIn("MLSAG/RingCT/stealth spends stay classical", hybrid)

    def test_vote_and_poe_verify_hybridsig_not_size_assert(self):
        consensus = (KEPLER / "src" / "core" / "consensus.cpp").read_text()
        self.assertIn("verifyApplicationPayload", consensus)
        self.assertIn(KQAS_DOMAIN_VOTE, consensus)
        self.assertIn("AND-verify HybridSig", consensus)
        poe = (KEPLER / "src" / "core" / "poe_v1_objects.cpp").read_text()
        self.assertIn("verifyApplicationPayload", poe)
        self.assertIn(KQAS_DOMAIN_POE_VOTE, poe)
        registry = (KEPLER / "src" / "quantum" / "identity_registry.cpp").read_text()
        self.assertIn("verifyApplicationPayload", registry)
        transfer = (KEPLER / "src" / "core" / "transfer.cpp").read_text()
        self.assertIn(KQAS_DOMAIN_TRANSFER, transfer)
        self.assertIn("verifyApplicationPayload", transfer)
        net = (KEPLER / "src" / "node" / "synapse_net.cpp").read_text()
        self.assertIn(KQAS_DOMAIN_PRODUCER, net)
        self.assertIn("signApplicationPayload", net)
        ledger = (KEPLER / "src" / "core" / "ledger.cpp").read_text()
        self.assertIn("verifyApplicationPayload", ledger)
        self.assertIn(KQAS_DOMAIN_PRODUCER, ledger)

    def test_handshake_and_node_msg_stay_kyber_x25519(self):
        handshake = (KEPLER / "src" / "network" / "handshake.cpp").read_text(encoding="utf-8")
        self.assertIn("X25519", handshake)
        self.assertIn("ML-KEM-768", handshake)
        self.assertIn("Kyber", handshake)
        self.assertIn("not Dilithium", handshake)
        self.assertIn("Do not put HybridSig here", handshake)
        code = "\n".join(
            line for line in handshake.splitlines() if not line.lstrip().startswith("//")
        )
        self.assertNotIn("Dilithium", code)
        self.assertNotIn("HybridSig", code)
        self.assertNotIn("signApplicationPayload", code)
        self.assertNotIn("ML-DSA", code)
        hybrid = (KEPLER / "src" / "quantum" / "hybrid_sig.cpp").read_text()
        self.assertIn("handshake.cpp has no Dilithium trailer", hybrid)
        engine = (KEPLER / "src" / "ide" / "synapsed_engine.cpp").read_text()
        self.assertIn("NODE_MSG", engine)
        self.assertIn("hybrid Kyber+X25519 NODE_MSG", engine)
        self.assertIn("quantum_signed", engine)


if __name__ == "__main__":
    unittest.main()
