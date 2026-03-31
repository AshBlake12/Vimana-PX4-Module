#!/usr/bin/env python3
############################################################################
#
#   Copyright (C) 2026 Vimana Aerotech. All rights reserved.
#
#   Vimana NS1400 Firmware Signature Verification Library
#
#   Implements the two-tier certificate chain verification:
#     Master Root CA  ->  Release Certificate  ->  Firmware Image
#
#   Uses ECDSA P-256 (secp256r1/prime256v1) with SHA-256 throughout,
#   matching the on-device uECC_secp256r1() verification.
#
############################################################################

import hashlib
import struct
import time
import os

# Certificate blob layout (compact binary, 136 bytes total):
#   [0:64]    Release public key (raw X||Y, 64 bytes)
#   [64:68]   Not-Before timestamp (uint32 LE, UNIX epoch)
#   [68:72]   Not-After  timestamp (uint32 LE, UNIX epoch)
#   [72:136]  Certificate signature by Master key (64 bytes, r||s)
VMN_CERT_SIZE = 136
VMN_CERT_PUBKEY_OFF = 0
VMN_CERT_PUBKEY_LEN = 64
VMN_CERT_NOT_BEFORE_OFF = 64
VMN_CERT_NOT_AFTER_OFF = 68
VMN_CERT_SIG_OFF = 72
VMN_CERT_SIG_LEN = 64

# Firmware signature: raw r||s, 64 bytes
VMN_FW_SIG_LEN = 64


def _load_pub_key_from_pem(pem_path):
    """Load an EC public key from a PEM file. Returns a cryptography EllipticCurvePublicKey."""
    from cryptography.hazmat.primitives.serialization import load_pem_public_key
    with open(pem_path, "rb") as f:
        return load_pem_public_key(f.read())


def _load_pub_key_from_cert_pem(cert_pem_path):
    """Load the public key from an X.509 PEM certificate."""
    from cryptography import x509
    with open(cert_pem_path, "rb") as f:
        cert = x509.load_pem_x509_certificate(f.read())
    return cert.public_key()


def _load_priv_key_from_pem(pem_path):
    """Load an EC private key from a PEM file."""
    from cryptography.hazmat.primitives.serialization import load_pem_private_key
    with open(pem_path, "rb") as f:
        return load_pem_private_key(f.read(), password=None)


def _pub_key_raw_bytes(pub_key):
    """Extract raw X||Y (64 bytes) from an EllipticCurvePublicKey."""
    from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
    raw = pub_key.public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    assert raw[0] == 0x04 and len(raw) == 65, "Expected uncompressed P-256 point"
    return raw[1:]  # strip 0x04 prefix


def _ecdsa_sign_raw(private_key, data_bytes):
    """
    Sign data with ECDSA P-256 SHA-256 and return a fixed 64-byte r||s signature.
    The cryptography library produces DER-encoded signatures; we convert to raw.
    """
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

    der_sig = private_key.sign(data_bytes, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_sig)
    return r.to_bytes(32, 'big') + s.to_bytes(32, 'big')


def _ecdsa_verify_raw(pub_key, data_bytes, raw_sig):
    """
    Verify a 64-byte r||s ECDSA P-256 signature.
    Returns True on success, False on failure.
    """
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    from cryptography.exceptions import InvalidSignature

    r = int.from_bytes(raw_sig[:32], 'big')
    s = int.from_bytes(raw_sig[32:], 'big')
    der_sig = encode_dss_signature(r, s)

    try:
        pub_key.verify(der_sig, data_bytes, ec.ECDSA(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False


def _raw_bytes_to_pub_key(raw_bytes):
    """Reconstruct an EllipticCurvePublicKey from 64-byte raw X||Y."""
    from cryptography.hazmat.primitives.asymmetric.ec import (
        EllipticCurvePublicKey, SECP256R1
    )
    uncompressed = b'\x04' + raw_bytes
    return EllipticCurvePublicKey.from_encoded_point(SECP256R1(), uncompressed)


# ---------------------------------------------------------------------------
# Certificate creation
# ---------------------------------------------------------------------------

def create_release_cert(master_priv_key, release_pub_key_raw,
                        validity_days=90, not_before=None):
    """
    Create a compact binary release certificate signed by the master key.

    Args:
        master_priv_key:       EllipticCurvePrivateKey (master)
        release_pub_key_raw:   bytes, 64-byte raw X||Y of release key
        validity_days:         int, certificate validity in days
        not_before:            int, UNIX timestamp (defaults to now)

    Returns:
        bytes, 136-byte certificate blob
    """
    if not_before is None:
        not_before = int(time.time())
    not_after = not_before + (validity_days * 86400)

    # The signed payload is: release_pub_key || not_before || not_after
    payload = (release_pub_key_raw
               + struct.pack('<I', not_before)
               + struct.pack('<I', not_after))
    assert len(payload) == 72

    sig = _ecdsa_sign_raw(master_priv_key, payload)
    assert len(sig) == 64

    cert_blob = payload + sig
    assert len(cert_blob) == VMN_CERT_SIZE
    return cert_blob


# ---------------------------------------------------------------------------
# Certificate parsing
# ---------------------------------------------------------------------------

def parse_release_cert(cert_blob):
    """
    Parse a 136-byte binary release certificate.

    Returns dict with: release_pub_key_raw, not_before, not_after, signature
    """
    assert len(cert_blob) == VMN_CERT_SIZE, \
        "Invalid cert size: %d (expected %d)" % (len(cert_blob), VMN_CERT_SIZE)

    release_pub = cert_blob[VMN_CERT_PUBKEY_OFF:VMN_CERT_PUBKEY_OFF + VMN_CERT_PUBKEY_LEN]
    not_before = struct.unpack('<I', cert_blob[VMN_CERT_NOT_BEFORE_OFF:VMN_CERT_NOT_BEFORE_OFF + 4])[0]
    not_after = struct.unpack('<I', cert_blob[VMN_CERT_NOT_AFTER_OFF:VMN_CERT_NOT_AFTER_OFF + 4])[0]
    signature = cert_blob[VMN_CERT_SIG_OFF:VMN_CERT_SIG_OFF + VMN_CERT_SIG_LEN]

    return {
        'release_pub_key_raw': release_pub,
        'not_before': not_before,
        'not_after': not_after,
        'signature': signature,
    }


# ---------------------------------------------------------------------------
# Full chain verification (mirrors the on-device pseudocode)
# ---------------------------------------------------------------------------

def verify_vimana_update(master_pub_key, release_cert_blob,
                         firmware_image, firmware_signature,
                         reference_time=None):
    """
    Full certificate chain verification.

    1. Verify the release certificate was signed by the master key
    2. Check certificate expiry
    3. Verify the firmware image was signed by the release key

    Args:
        master_pub_key:      EllipticCurvePublicKey or 64-byte raw bytes
        release_cert_blob:   bytes, 136-byte compact certificate
        firmware_image:      bytes, the raw firmware binary
        firmware_signature:  bytes, 64-byte r||s ECDSA signature
        reference_time:      int, UNIX timestamp to check expiry against (defaults to now)

    Returns:
        (success: bool, reason: str)
    """
    if reference_time is None:
        reference_time = int(time.time())

    # Convert raw bytes to key object if needed
    if isinstance(master_pub_key, (bytes, bytearray)) and len(master_pub_key) == 64:
        master_pub_key = _raw_bytes_to_pub_key(master_pub_key)

    # Parse the certificate
    cert = parse_release_cert(release_cert_blob)

    # Step 1: Verify the certificate signature using the Master Key (Root of Trust)
    cert_payload = release_cert_blob[:VMN_CERT_SIG_OFF]  # First 72 bytes
    if not _ecdsa_verify_raw(master_pub_key, cert_payload, cert['signature']):
        return False, "Certificate signature invalid - release key not authorized by Vimana Master"

    # Step 2: Check expiry (the 3-month rule)
    if reference_time < cert['not_before']:
        return False, "Certificate not yet valid (not_before=%d, now=%d)" % (cert['not_before'], reference_time)
    if reference_time > cert['not_after']:
        return False, "Release certificate has expired (not_after=%d, now=%d)" % (cert['not_after'], reference_time)

    # Step 3: Verify firmware using the now-trusted release key
    release_pub_key = _raw_bytes_to_pub_key(cert['release_pub_key_raw'])
    if not _ecdsa_verify_raw(release_pub_key, firmware_image, firmware_signature):
        return False, "Firmware signature invalid - not signed by this release key"

    return True, "All checks passed"


def compute_master_key_hash(master_pub_key_raw):
    """Compute SHA-256 hash of the raw 64-byte master public key."""
    return hashlib.sha256(master_pub_key_raw).hexdigest()


# ---------------------------------------------------------------------------
# Lockout marker management
# ---------------------------------------------------------------------------

def _lockout_path(board_sn):
    """Get the path for the signed-firmware lockout marker."""
    home = os.path.expanduser("~")
    lockout_dir = os.path.join(home, ".vimana")
    os.makedirs(lockout_dir, exist_ok=True)
    return os.path.join(lockout_dir, "signed_lockout_%s" % board_sn)


def is_board_locked(board_sn):
    """Check if a board has been locked to signed-only firmware."""
    return os.path.exists(_lockout_path(board_sn))


def lock_board(board_sn):
    """Create the irreversible signed-only lockout marker for a board."""
    path = _lockout_path(board_sn)
    with open(path, "w") as f:
        f.write("locked_at=%d\n" % int(time.time()))
        f.write("# This board will only accept Vimana-signed firmware.\n")
        f.write("# Deleting this file DOES NOT remove the hardware lockout.\n")
    return path
