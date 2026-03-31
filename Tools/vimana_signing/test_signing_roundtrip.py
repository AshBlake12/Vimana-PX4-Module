#!/usr/bin/env python3
############################################################################
#
#   Vimana Signing Pipeline - End-to-End Test
#
#   Usage:
#     python3 test_signing_roundtrip.py --secrets-dir ~/om/secrets
#
############################################################################

import argparse
import os
import sys
import time
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmn_verify import (
    _load_priv_key_from_pem,
    _load_pub_key_from_cert_pem,
    _load_pub_key_from_pem,
    _pub_key_raw_bytes,
    _ecdsa_sign_raw,
    create_release_cert,
    verify_vimana_update,
    parse_release_cert,
    compute_master_key_hash,
    VMN_CERT_SIZE,
    VMN_FW_SIG_LEN,
)

PASS = "\033[92m[PASS]\033[0m"
FAIL = "\033[91m[FAIL]\033[0m"
test_count = 0
pass_count = 0


def test(name, condition, detail=""):
    global test_count, pass_count
    test_count += 1
    if condition:
        pass_count += 1
        print("  %s %s" % (PASS, name))
    else:
        print("  %s %s" % (FAIL, name))
        if detail:
            print("         %s" % detail)


def main():
    global test_count, pass_count

    parser = argparse.ArgumentParser(description="Test Vimana signing round-trip")
    parser.add_argument("--secrets-dir", required=True, help="Path to ~/om/secrets")
    args = parser.parse_args()

    secrets = args.secrets_dir
    master_priv_path = os.path.join(secrets, "vimana_master_priv.pem")
    release_cert_path = os.path.join(secrets, "release_cert.pem")
    release_priv_path = os.path.join(secrets, "release_priv.pem")
    master_pub_path = os.path.join(secrets, "master_pub.pem")

    for path in [master_priv_path, release_cert_path, release_priv_path, master_pub_path]:
        if not os.path.exists(path):
            print("ERROR: Required file not found: %s" % path)
            sys.exit(1)

    print("=" * 60)
    print("  VIMANA SIGNING PIPELINE - END-TO-END TEST")
    print("=" * 60)

    # Load keys
    print("\n--- Loading Keys ---")
    master_priv = _load_priv_key_from_pem(master_priv_path)
    master_pub = _load_pub_key_from_pem(master_pub_path)
    master_pub_raw = _pub_key_raw_bytes(master_pub)
    release_pub = _load_pub_key_from_cert_pem(release_cert_path)
    release_pub_raw = _pub_key_raw_bytes(release_pub)
    release_priv = _load_priv_key_from_pem(release_priv_path)

    test("Master public key is 64 bytes", len(master_pub_raw) == 64,
         "got %d bytes" % len(master_pub_raw))
    test("Release public key is 64 bytes", len(release_pub_raw) == 64,
         "got %d bytes" % len(release_pub_raw))
    test("Master key hash matches vmn_keys.h",
         compute_master_key_hash(master_pub_raw) == "d173888c48dcbcd58b48d3a0932a4e437d2eacea21bad085ec1c33de50535c00",
         "got %s" % compute_master_key_hash(master_pub_raw))

    # Test 1: Create a release certificate
    print("\n--- Test 1: Certificate Creation ---")
    cert_blob = create_release_cert(master_priv, release_pub_raw, validity_days=90)
    test("Certificate is 136 bytes", len(cert_blob) == VMN_CERT_SIZE,
         "got %d bytes" % len(cert_blob))

    parsed = parse_release_cert(cert_blob)
    test("Release pub key extracted correctly",
         parsed['release_pub_key_raw'] == release_pub_raw)
    test("Not-before is approximately now",
         abs(parsed['not_before'] - int(time.time())) < 5)
    test("Not-after is ~90 days from now",
         abs(parsed['not_after'] - parsed['not_before'] - 90 * 86400) < 5)

    # Test 2: Sign dummy firmware
    print("\n--- Test 2: Firmware Signing ---")
    dummy_firmware = b"VIMANA_NS1400_DUMMY_FIRMWARE_" * 1000
    fw_signature = _ecdsa_sign_raw(release_priv, dummy_firmware)
    test("Firmware signature is 64 bytes", len(fw_signature) == VMN_FW_SIG_LEN,
         "got %d bytes" % len(fw_signature))

    # Test 3: Full chain verification (should pass)
    print("\n--- Test 3: Full Chain Verification (VALID) ---")
    success, reason = verify_vimana_update(
        master_pub_key=master_pub_raw,
        release_cert_blob=cert_blob,
        firmware_image=dummy_firmware,
        firmware_signature=fw_signature,
    )
    test("Full chain verification passes", success, reason)

    # Test 4: Expired certificate (should fail)
    print("\n--- Test 4: Expired Certificate ---")
    expired_cert = create_release_cert(
        master_priv, release_pub_raw,
        validity_days=1,
        not_before=int(time.time()) - 86400 * 10
    )
    success, reason = verify_vimana_update(
        master_pub_key=master_pub_raw,
        release_cert_blob=expired_cert,
        firmware_image=dummy_firmware,
        firmware_signature=fw_signature,
    )
    test("Expired certificate is rejected", not success, reason)
    test("Reason mentions expiry", "expired" in reason.lower() or "not_after" in reason.lower(), reason)

    # Test 5: Tampered firmware (should fail)
    print("\n--- Test 5: Tampered Firmware ---")
    tampered_fw = bytearray(dummy_firmware)
    tampered_fw[100] ^= 0xFF
    success, reason = verify_vimana_update(
        master_pub_key=master_pub_raw,
        release_cert_blob=cert_blob,
        firmware_image=bytes(tampered_fw),
        firmware_signature=fw_signature,
    )
    test("Tampered firmware is rejected", not success, reason)
    test("Reason mentions firmware signature", "firmware" in reason.lower(), reason)

    # Test 6: Wrong master key (should fail)
    print("\n--- Test 6: Wrong Master Key ---")
    from cryptography.hazmat.primitives.asymmetric import ec
    fake_master_priv = ec.generate_private_key(ec.SECP256R1())
    fake_master_pub_raw = _pub_key_raw_bytes(fake_master_priv.public_key())
    success, reason = verify_vimana_update(
        master_pub_key=fake_master_pub_raw,
        release_cert_blob=cert_blob,
        firmware_image=dummy_firmware,
        firmware_signature=fw_signature,
    )
    test("Wrong master key is rejected", not success, reason)
    test("Reason mentions certificate/master", "certificate" in reason.lower() or "master" in reason.lower(), reason)

    # Test 7: px_mkfw.py integration (dry-run)
    print("\n--- Test 7: px_mkfw.py Integration ---")
    with tempfile.NamedTemporaryFile(suffix='.vmn', delete=False) as tf:
        tf.write(cert_blob)
        cert_file = tf.name
    with tempfile.NamedTemporaryFile(suffix='.sig', delete=False) as tf:
        tf.write(fw_signature)
        sig_file = tf.name
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as tf:
        tf.write(dummy_firmware)
        fw_file = tf.name

    import subprocess, json
    mkfw_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'px_mkfw.py')
    cmd = [
        sys.executable, mkfw_path,
        '--image', fw_file,
        '--release_cert', cert_file,
        '--firmware_signature', sig_file,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    test("px_mkfw.py exits successfully", result.returncode == 0, result.stderr)

    if result.returncode == 0:
        px4_desc = json.loads(result.stdout)
        test("Output has vimana_signed=true", px4_desc.get('vimana_signed') == True)
        test("Output has vimana_release_cert", 'vimana_release_cert' in px4_desc)
        test("Output has vimana_firmware_signature", 'vimana_firmware_signature' in px4_desc)
        test("Output has vimana_master_key_hash", 'vimana_master_key_hash' in px4_desc)

    for f in [cert_file, sig_file, fw_file]:
        os.unlink(f)

    # Summary
    print("\n" + "=" * 60)
    print("  RESULTS: %d/%d tests passed" % (pass_count, test_count))
    if pass_count == test_count:
        print("  ALL TESTS PASSED")
    else:
        print("  %d TESTS FAILED" % (test_count - pass_count))
    print("=" * 60)

    sys.exit(0 if pass_count == test_count else 1)


if __name__ == "__main__":
    main()
