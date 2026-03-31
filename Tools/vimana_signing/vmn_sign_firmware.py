#!/usr/bin/env python3
############################################################################
#
#   Copyright (C) 2026 Vimana Aerotech. All rights reserved.
#
#   Firmware Signing Tool for Vimana NS1400 Secure Boot
#
#   Signs a firmware binary with the release private key (ECDSA P-256).
#
#   Usage:
#     python3 vmn_sign_firmware.py \
#       --release-priv /path/to/release_priv.pem \
#       --firmware /path/to/firmware.bin \
#       --output firmware.sig
#
############################################################################

import argparse
import base64
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vmn_verify import (
    _load_priv_key_from_pem,
    _ecdsa_sign_raw,
    VMN_FW_SIG_LEN,
)


def main():
    parser = argparse.ArgumentParser(
        description="Sign a firmware image with the Vimana release key."
    )
    parser.add_argument(
        "--release-priv", required=True,
        help="Path to release private key PEM (release_priv.pem)"
    )
    parser.add_argument(
        "--firmware", required=True,
        help="Path to the firmware binary (.bin) to sign"
    )
    parser.add_argument(
        "--output", required=True,
        help="Output path for the signature file (.sig)"
    )
    args = parser.parse_args()

    print("Loading release private key from: %s" % args.release_priv)
    release_priv = _load_priv_key_from_pem(args.release_priv)

    print("Reading firmware image: %s" % args.firmware)
    with open(args.firmware, "rb") as f:
        fw_data = f.read()
    print("  Firmware size: %d bytes" % len(fw_data))

    print("Signing firmware with ECDSA P-256 SHA-256...")
    signature = _ecdsa_sign_raw(release_priv, fw_data)
    assert len(signature) == VMN_FW_SIG_LEN

    with open(args.output, "wb") as f:
        f.write(signature)

    print("Signature written to: %s" % args.output)
    print("  Size: %d bytes" % len(signature))
    print("  Base64: %s" % base64.b64encode(signature).decode())
    print("Done.")


if __name__ == "__main__":
    main()
