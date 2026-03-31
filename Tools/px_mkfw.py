#!/usr/bin/env python3
############################################################################
#
#   Copyright (C) 2012, 2013 PX4 Development Team. All rights reserved.
#   Copyright (C) 2026 Vimana Aerotech. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name PX4 nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
############################################################################

#
# PX4 firmware image generator
#
# The PX4 firmware file is a JSON-encoded Python object, containing
# metadata fields and a zlib-compressed base64-encoded firmware image.
#
# Vimana extension: optionally embeds cryptographic signing artifacts
# (release certificate + firmware signature) for secure boot verification.
#

import argparse
import json
import base64
import zlib
import time
import subprocess
import hashlib
import sys

#
# Construct a basic firmware description
#
def mkdesc():
	proto = {}
	proto['magic']		= "PX4FWv1"
	proto['board_id']	= 0
	proto['board_revision']	= 0
	proto['version']	= ""
	proto['summary']	= ""
	proto['description']	= ""
	proto['git_identity']	= "" # git tag
	proto['git_hash']	= "" # git commit hash
	proto['build_time']	= 0
	proto['image']		= bytes()
	proto['image_size']	= 0
	proto['vimana_signed']	= False
	return proto

# Parse commandline
parser = argparse.ArgumentParser(description="Firmware generator for the PX autopilot system.")
parser.add_argument("--prototype",	action="store", help="read a prototype description from a file")
parser.add_argument("--board_id",	action="store", help="set the board ID required")
parser.add_argument("--board_revision",	action="store", help="set the board revision required")
parser.add_argument("--version",	action="store", help="set a version string")
parser.add_argument("--summary",	action="store", help="set a brief description")
parser.add_argument("--description",	action="store", help="set a longer description")
parser.add_argument("--git_identity",	action="store", help="the working directory to check for git identity")
parser.add_argument("--parameter_xml",	action="store", help="the parameters.xml file")
parser.add_argument("--airframe_xml",	action="store", help="the airframes.xml file")
parser.add_argument("--image",		action="store", help="the firmware image")

# Vimana signing arguments
parser.add_argument("--release_cert",	action="store",
	help="Vimana release certificate binary (.vmn) - compact 136-byte cert signed by Master CA")
parser.add_argument("--firmware_signature",	action="store",
	help="Vimana firmware signature binary (.sig) - 64-byte ECDSA P-256 r||s signature")
parser.add_argument("--master_pub_key",	action="store",
	help="Vimana master public key binary (64-byte raw X||Y) - for key hash embedding")

args = parser.parse_args()

# Fetch the firmware descriptor prototype if specified
if args.prototype != None:
	f = open(args.prototype,"r")
	desc = json.load(f)
	f.close()
else:
	desc = mkdesc()

desc['build_time'] 		= int(time.time())

if args.board_id != None:
	desc['board_id']	= int(args.board_id)
if args.board_revision != None:
	desc['board_revision']	= int(args.board_revision)
if args.version != None:
	desc['version']		= str(args.version)
if args.summary != None:
	desc['summary']		= str(args.summary)
if args.description != None:
	desc['description']	= str(args.description)
if args.git_identity != None:
	cmd = "git --git-dir '{:}/.git' describe --exclude ext/* --always --tags".format(args.git_identity)
	p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE).stdout
	desc['git_identity']	= p.read().strip().decode('utf-8')
	p.close()
	cmd = "git --git-dir '{:}/.git' rev-parse --verify HEAD".format(args.git_identity)
	p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE).stdout
	desc['git_hash']	= p.read().strip().decode('utf-8')
	p.close()
if args.parameter_xml != None:
	f = open(args.parameter_xml, "rb")
	bytes = f.read()
	desc['parameter_xml_size'] = len(bytes)
	desc['parameter_xml'] = base64.b64encode(zlib.compress(bytes,9)).decode('utf-8')
	desc['mav_autopilot'] = 12 # 12 = MAV_AUTOPILOT_PX4
if args.airframe_xml != None:
	f = open(args.airframe_xml, "rb")
	bytes = f.read()
	desc['airframe_xml_size'] = len(bytes)
	desc['airframe_xml'] = base64.b64encode(zlib.compress(bytes,9)).decode('utf-8')
if args.image != None:
	f = open(args.image, "rb")
	bytes = f.read()
	desc['image_size'] = len(bytes)
	desc['image'] = base64.b64encode(zlib.compress(bytes,9)).decode('utf-8')

# -----------------------------------------------------------------------
# Vimana Signing: embed certificate chain artifacts
# -----------------------------------------------------------------------
if args.release_cert is not None and args.firmware_signature is not None:
	# Read the release certificate (136-byte binary blob)
	with open(args.release_cert, "rb") as f:
		release_cert_data = f.read()
	if len(release_cert_data) != 136:
		print("ERROR: Release certificate must be exactly 136 bytes, got %d" % len(release_cert_data),
		      file=sys.stderr)
		sys.exit(1)

	# Read the firmware signature (64-byte binary blob)
	with open(args.firmware_signature, "rb") as f:
		fw_sig_data = f.read()
	if len(fw_sig_data) != 64:
		print("ERROR: Firmware signature must be exactly 64 bytes, got %d" % len(fw_sig_data),
		      file=sys.stderr)
		sys.exit(1)

	desc['vimana_signed'] = True
	desc['vimana_release_cert'] = base64.b64encode(release_cert_data).decode('utf-8')
	desc['vimana_firmware_signature'] = base64.b64encode(fw_sig_data).decode('utf-8')

	# Embed master key hash for key-rotation tracking
	if args.master_pub_key is not None:
		with open(args.master_pub_key, "rb") as f:
			master_pub_data = f.read()
		desc['vimana_master_key_hash'] = hashlib.sha256(master_pub_data).hexdigest()
	else:
		# Fallback: compute hash from the key embedded in vmn_keys.h
		master_key_bytes = bytearray([
			0x08, 0x25, 0x61, 0x7F, 0x3B, 0x12, 0xFE, 0x98,
			0x65, 0x73, 0xF7, 0x35, 0xC8, 0x44, 0xB2, 0x8B,
			0xF2, 0x47, 0xD5, 0xC4, 0x18, 0xB6, 0xCC, 0xE2,
			0x7B, 0x31, 0x19, 0x9E, 0x88, 0x0B, 0xCB, 0x84,
			0x8A, 0x58, 0xEE, 0x70, 0x8C, 0x2A, 0xFE, 0xA4,
			0xE0, 0xF4, 0x51, 0x6F, 0xCA, 0xE4, 0x62, 0x26,
			0x5B, 0x75, 0x49, 0x05, 0xAD, 0xB6, 0x36, 0xAA,
			0x1A, 0xB9, 0x48, 0xFC, 0x8F, 0xB1, 0x1C, 0xE5
		])
		desc['vimana_master_key_hash'] = hashlib.sha256(master_key_bytes).hexdigest()

	print("Vimana signing: ENABLED - firmware is cryptographically signed", file=sys.stderr)
	print("  Release cert: %s (%d bytes)" % (args.release_cert, len(release_cert_data)), file=sys.stderr)
	print("  FW signature: %s (%d bytes)" % (args.firmware_signature, len(fw_sig_data)), file=sys.stderr)
	print("  Master key hash: %s" % desc['vimana_master_key_hash'], file=sys.stderr)

elif args.release_cert is not None or args.firmware_signature is not None:
	print("ERROR: Both --release_cert and --firmware_signature must be provided for signing",
	      file=sys.stderr)
	sys.exit(1)
else:
	desc['vimana_signed'] = False

print(json.dumps(desc, indent=4))
