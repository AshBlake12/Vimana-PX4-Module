<p align="center">
  <img src="docs/vimana_logo.png" alt="Vimana Aerotech" width="280"/>
</p>

<h1 align="center">Vimana VTOL — PX4 Flight Module</h1>

<p align="center">
  <b>Tailsitter VTOL flight monitoring &amp; safety system for the CubeOrange+</b>
  <br/>
  <i>Vimana Aerotech · NS1400 Platform</i>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/board-CubeOrange+-orange?style=flat-square" alt="Board"/>
  <img src="https://img.shields.io/badge/arch-Cortex--M7-blue?style=flat-square" alt="Architecture"/>
  <img src="https://img.shields.io/badge/type-Tailsitter_VTOL-green?style=flat-square" alt="Vehicle Type"/>
  <img src="https://img.shields.io/badge/crypto-ECDSA_P--256-red?style=flat-square" alt="Signing"/>
  <img src="https://img.shields.io/badge/license-BSD--3-lightgrey?style=flat-square" alt="License"/>
</p>

---

## Overview

This repository is a **PX4-Autopilot fork** with the custom **Vimana tailsitter VTOL module** — a production-grade flight monitoring and safety system designed for the Vimana NS1400 airframe running on the CubePilot CubeOrange+ flight controller.

The Vimana module runs as a dedicated 10 Hz work-queue task that continuously monitors vehicle health across all flight phases — hover, transition, and cruise — and sends real-time alerts via MAVLink when safety thresholds are breached.

### Key Capabilities

| Feature | Description |
|---------|-------------|
| **VTOL Transition Monitoring** | Tracks MC↔FW transitions with airspeed, attitude, and duration guards |
| **Battery Watchdog** | Dual-threshold voltage and percentage alerts (warning + critical) |
| **EKF2 Health** | Monitors position accuracy and filter fault flags |
| **Attitude Guards** | Mode-aware pitch/roll envelope protection (MC vs FW limits) |
| **GPS & RC Link** | Fix-type and satellite monitoring; RC-loss alerts while armed |
| **Flight Statistics** | Per-session tracking: transitions, peak current, max altitude, flight time |
| **MAVLink Alerts** | Critical messages pushed to GCS in real time |
| **Firmware Signing** | ECDSA P-256 certificate chain with irreversible signed-only lockout |

---

## Repository Structure

```
Vimana-PX4-Module/
├── src/modules/vimana/                    # Vimana flight module
│   ├── vimana_main.cpp                    #   10 Hz monitoring & safety logic
│   ├── CMakeLists.txt                     #   Build definition
│   └── Kconfig                            #   Menuconfig entry
│
├── boards/cubepilot/cubeorangeplus/
│   ├── vimana_vtol.px4board               # Board config (Vimana profile)
│   ├── vmn_keys.h                         # Master public key (Root of Trust)
│   └── firmware.prototype                 # .px4 metadata template
│
├── Tools/
│   ├── px_mkfw.py                         # Firmware packager (+ signing)
│   ├── px_uploader.py                     # Firmware uploader (+ verification)
│   └── vimana_signing/                    # Cryptographic signing toolchain
│       ├── vmn_verify.py                  #   Core verification library
│       ├── vmn_generate_release_cert.py   #   Release certificate generator
│       ├── vmn_sign_firmware.py           #   Firmware signing tool
│       ├── vmn_master_pub.bin             #   Raw master public key (64 bytes)
│       └── test_signing_roundtrip.py      #   End-to-end test suite
│
├── platforms/nuttx/CMakeLists.txt         # Build system (signing integration)
└── setup_vimana.sh                        # Development environment setup
```

---

## Getting Started

### Prerequisites

- **ARM Toolchain**: `arm-none-eabi-gcc` (for Cortex-M7)
- **Python 3.8+** with `cryptography` package (for firmware signing)
- **PX4 build dependencies** — see [PX4 Dev Guide](https://docs.px4.io/main/en/dev_setup/dev_env.html)

### Setup

```bash
# Clone the repository
git clone <repo-url> Vimana-PX4-Module
cd Vimana-PX4-Module

# Initialize PX4 submodules
git submodule update --init --recursive

# Run the Vimana setup script
bash setup_vimana.sh

# Install signing dependencies
pip install cryptography
```

### Build

```bash
# Build the Vimana VTOL firmware
make cubepilot_cubeorangeplus_vimana_vtol
```

The output `.px4` firmware file will be in `build/cubepilot_cubeorangeplus_vimana_vtol/`.

### Upload

```bash
# Upload to a connected CubeOrange+ via USB
python3 Tools/px_uploader.py \
  --port /dev/ttyACM0 \
  build/cubepilot_cubeorangeplus_vimana_vtol/*.px4
```

---

## Firmware Signing

The Vimana build pipeline supports **cryptographic firmware signing** using a two-tier ECDSA P-256 certificate chain. Once a board accepts a signed firmware image, it is permanently locked to signed-only updates.

### Signing Architecture

```
  Master CA Key (Root of Trust)
       │
       ├── Signs → Release Certificate (90-day validity)
       │                │
       │                └── Release Key signs → Firmware Image
       │
  vmn_keys.h (on-device)         px_uploader.py (host-side verification)
```

### Signed Build Workflow

```bash
# 1. Generate a 90-day release certificate
python3 Tools/vimana_signing/vmn_generate_release_cert.py \
  --master-priv ~/om/secrets/vimana_master_priv.pem \
  --release-cert ~/om/secrets/release_cert.pem \
  --validity-days 90 \
  --output /tmp/release_cert.vmn

# 2. Build the firmware
make cubepilot_cubeorangeplus_vimana_vtol

# 3. Sign the binary
python3 Tools/vimana_signing/vmn_sign_firmware.py \
  --release-priv ~/om/secrets/release_priv.pem \
  --firmware build/cubepilot_cubeorangeplus_vimana_vtol/*.bin \
  --output /tmp/firmware.sig

# 4. Package with signing artifacts
python3 Tools/px_mkfw.py \
  --prototype boards/cubepilot/cubeorangeplus/firmware.prototype \
  --image build/cubepilot_cubeorangeplus_vimana_vtol/*.bin \
  --release_cert /tmp/release_cert.vmn \
  --firmware_signature /tmp/firmware.sig \
  > signed_firmware.px4

# 5. Upload (automatic verification)
python3 Tools/px_uploader.py --port /dev/ttyACM0 signed_firmware.px4
```

### Environment Variable Signing (CI/CD)

```bash
export VIMANA_RELEASE_CERT=/tmp/release_cert.vmn
export VIMANA_FW_SIGNATURE=/tmp/firmware.sig
make cubepilot_cubeorangeplus_vimana_vtol
# Signing artifacts are automatically embedded in the .px4 output
```

### Security Properties

| Property | Detail |
|----------|--------|
| **Algorithm** | ECDSA P-256 (secp256r1) with SHA-256 |
| **Root of Trust** | 64-byte master public key in `vmn_keys.h` |
| **Certificate Expiry** | Configurable (default 90 days) |
| **Lockout Policy** | Irreversible — once signed, board rejects unsigned firmware |
| **Tamper Detection** | Any byte modification in the firmware image is detected |

---

## Vimana Flight Module

### Monitored Parameters

The Vimana module subscribes to 13 uORB topics and runs continuous safety checks:

| Subsystem | Topics | Check |
|-----------|--------|-------|
| **Attitude** | `vehicle_attitude` | Pitch/roll envelope per flight mode |
| **Battery** | `battery_status` | Voltage (14.0V warn / 13.2V critical), percentage (25% / 10%) |
| **VTOL State** | `vtol_vehicle_status` | Transition duration, airspeed during TX→FW |
| **Airspeed** | `airspeed` | Min IAS 8 m/s for FW, abort below 5 m/s |
| **EKF2** | `estimator_status` | Fault flags, H/V accuracy (5m threshold) |
| **GPS** | `sensor_gps` | Fix type, satellite count, HDOP |
| **RC** | `input_rc` | Link loss detection while armed |
| **Navigation** | `vehicle_local_position` | Altitude, vertical speed |
| **Magnetometer** | `vehicle_magnetometer` | Field strength logging |
| **Landing** | `vehicle_land_detected` | Takeoff/landing event tracking |

### NSH Commands

```bash
# Start the module
vimana start

# View current status
vimana status

# Stop the module
vimana stop
```

### Example Status Output

```
 Vimana status
state    : FW  [CRUISE]
armed    : yes   landed: no
battery  : 15.42V  78%  12.3A  32.1°C
gps      : 14 sats  fix: 3  hdop: 0.8  alt: 142.3m
attitude : pitch -2.1  roll 1.4  yaw 182.3  (deg)
airspeed : 18.7 m/s
ekf2     : healthy  hacc: 0.4m  vacc: 0.3m  faults: 0x00000000
rc       : ok
mag      : [0.213  -0.041  0.382] Ga
--- lifetime stats ---
transitions  : 4
lowest batt  : 14.81V
peak current : 38.2A
max altitude : 156.7m
total flight : 847s
```

---

## Board Configuration

The Vimana VTOL board config (`vimana_vtol.px4board`) is based on the CubeOrange+ default with the following modifications:

**Enabled:**
- `CONFIG_MODULES_VIMANA=y` — Vimana flight safety module
- Full VTOL stack (MC + FW attitude/rate control, VTOL att control)
- EKF2, Navigator, Commander, Sensors, MAVLink
- DShot ESC protocol, UAVCAN, GPS, Barometer

**Disabled (for flash/RAM savings):**
- Gimbal, Camera, IR lock, Landing target estimator
- Autotune (MC + FW), Gyro calibration
- Rover, Airship, UUV, Simulator (SIH)
- Non-essential system commands

**Target Hardware:**
- **Board**: CubePilot CubeOrange+ (board_id: `1063`)
- **MCU**: STM32H757 (Cortex-M7, 480 MHz)
- **Flash**: 2 MB (max image: 1,966,080 bytes)
- **IO Co-processor**: CubePilot IO v2

---

## Safety Reminders

> ⚠️ **CRITICAL: Remove ALL propellers for initial bench testing.**

- Never test custom firmware on a flying vehicle without bench validation
- Keep stock CubeOrange+ firmware ready as a recovery image
- Read the full testing guide before attempting flight: `tools/vimana/TESTING_GUIDE.md`
- Always verify `vimana status` output on the bench before arming

---

## Development

### Running the Signing Test Suite

```bash
python3 Tools/vimana_signing/test_signing_roundtrip.py \
  --secrets-dir ~/om/secrets
```

All 20 tests should pass, covering: key loading, certificate creation, firmware signing, chain verification, expiry rejection, tamper detection, and `px_mkfw.py` integration.

### Project Layout

This is a fork of [PX4-Autopilot](https://github.com/PX4/PX4-Autopilot). Vimana-specific additions are confined to:

1. `src/modules/vimana/` — The flight module (C++)
2. `boards/cubepilot/cubeorangeplus/vimana_vtol.px4board` — Board config
3. `boards/cubepilot/cubeorangeplus/vmn_keys.h` — Cryptographic root of trust
4. `Tools/vimana_signing/` — Host-side signing toolchain (Python)
5. `Tools/px_mkfw.py` and `Tools/px_uploader.py` — Extended PX4 tools

---

## License

PX4 Autopilot base: **BSD 3-Clause** — see [LICENSE](LICENSE)

Vimana extensions: **Copyright © 2026 Vimana Aerotech. All rights reserved.**

---

<p align="center">
  <sub>Built by <b>Vimana Aerotech</b> · NS1400 Tailsitter VTOL Platform</sub>
</p>
