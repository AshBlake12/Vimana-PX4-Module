#!/usr/bin/env python3
############################################################################
#
#   Copyright (C) 2026 Vimana Aerotech. All rights reserved.
#
#   Release Certificate Generator for Vimana NS1400 Secure Boot
#
#   Issues a time-limited release certificate signed by the Master CA key.
#
#   Usage:
#     python3 vmn_generate_release_cert.py \
#       --master-priv /path/to/vimana_master_priv.pem \
#       --release-cert /path/to/release_cert.pem \
#       --validity-days 90 \
#       --output release_cert.vmn
#
############################################################################

import argparse
import base64
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vmn_verify import (
    _load_priv_key_from_pem,
    _load_pub_key_from_cert_pem,
    _pub_key_raw_bytes,
    create_release_cert,
    parse_release_cert,
    VMN_CERT_SIZE,
)


def main():
    parser = argparse.ArgumentParser(
        description="Generate a Vimana release certificate for firmware signing."
    )
    parser.add_argument(
        "--master-priv", required=True,
        help="Path to master CA private key PEM (vimana_master_priv.pem)"
    )
    parser.add_argument(
        "--release-cert", required=True,
        help="Path to release X.509 certificate PEM (release_cert.pem)"
    )
    parser.add_argument(
        "--validity-days", type=int, default=90,
        help="Certificate validity period in days (default: 90)"
    )
    parser.add_argument(
        "--output", required=True,
        help="Output path for the compact binary release certificate (.vmn)"
    )
    args = parser.parse_args()

    print("Loading master private key from: %s" % args.master_priv)
    master_priv = _load_priv_key_from_pem(args.master_priv)

    print("Extracting release public key from: %s" % args.release_cert)
    release_pub = _load_pub_key_from_cert_pem(args.release_cert)
    release_pub_raw = _pub_key_raw_bytes(release_pub)
    print("  Release key (hex): %s" % release_pub_raw.hex())

    print("Creating certificate (validity: %d days)..." % args.validity_days)
    cert_blob = create_release_cert(
        master_priv_key=master_priv,
        release_pub_key_raw=release_pub_raw,
        validity_days=args.validity_days,
    )

    parsed = parse_release_cert(cert_blob)
    assert len(cert_blob) == VMN_CERT_SIZE

    with open(args.output, "wb") as f:
        f.write(cert_blob)

    print("Certificate written to: %s" % args.output)
    print("  Size: %d bytes" % len(cert_blob))
    print("  Not-Before: %d" % parsed['not_before'])
    print("  Not-After:  %d" % parsed['not_after'])
    print("  Base64: %s" % base64.b64encode(cert_blob).decode())
    print("Done.")


if __name__ == "__main__":
    main()
