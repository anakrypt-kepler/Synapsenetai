#!/usr/bin/env python3
import hashlib
import time
import sys


def solve_pow(challenge, difficulty):
    for n in range(100000000):
        h = hashlib.sha256((challenge + str(n)).encode()).hexdigest()
        val = int(h[:8], 16)
        bits = 32 - val.bit_length() if val > 0 else 32
        if bits >= difficulty:
            return n, h
    return None, None


def verify_pow(challenge, nonce, difficulty):
    h = hashlib.sha256((challenge + str(nonce)).encode()).hexdigest()
    val = int(h[:8], 16)
    bits = 32 - val.bit_length() if val > 0 else 32
    return bits >= difficulty, h, bits


def test_pow():
    test_cases = [
        ("endgame_v3_test_challenge_abc123", 16),
        ("dread_pow_session_xyz789", 18),
        ("endgame_challenge_2026_04_25", 20),
        ("hashcash_1:20:260425:dreadytofatroptsdj6io7l:::", 20),
        ("pow_challenge_random_f4e8a1b2c3d5", 22),
        ("eg3_" + hashlib.sha256(b"test_session").hexdigest()[:32], 16),
        ("eg3_" + hashlib.sha256(b"hard_session").hexdigest()[:32], 24),
    ]

    print("EndGame V3 Hashcash PoW Solver Test")
    print("=" * 70)

    all_passed = True
    total_time = 0

    for i, (challenge, difficulty) in enumerate(test_cases):
        print(f"\nTest {i+1}/{len(test_cases)}: difficulty={difficulty} bits")
        print(f"  Challenge: {challenge[:50]}{'...' if len(challenge) > 50 else ''}")

        start = time.time()
        nonce, h = solve_pow(challenge, difficulty)
        elapsed = time.time() - start
        total_time += elapsed

        if nonce is None:
            print(f"  FAIL: no nonce found in 100M attempts")
            all_passed = False
            continue

        ok, verify_hash, actual_bits = verify_pow(challenge, nonce, difficulty)

        if ok:
            print(f"  PASS: nonce={nonce} hash={verify_hash[:16]}... "
                  f"bits={actual_bits} time={elapsed:.3f}s")
        else:
            print(f"  FAIL: nonce={nonce} hash={verify_hash[:16]}... "
                  f"bits={actual_bits} (need {difficulty})")
            all_passed = False

    print("\n" + "=" * 70)
    print(f"Total time: {total_time:.3f}s")

    if all_passed:
        print(f"ALL {len(test_cases)} TESTS PASSED")
    else:
        print("SOME TESTS FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_pow()
