#!/bin/bash
# Safe PX4 Build Script for Vimana Tailsitter Module
# Cube Orange Plus (px4_fmu-v5)

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Vimana PX4 Safe Build Script ===${NC}"
echo ""

# Find PX4 root directory (go up 2 levels from tools/vimana)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PX4_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

echo "Script location: $SCRIPT_DIR"
echo "PX4 root: $PX4_ROOT"
echo ""

# Change to PX4 root directory
cd "$PX4_ROOT"

# Step 1: Validate we're in PX4 directory
echo -e "${YELLOW}[1/8] Validating PX4 directory...${NC}"
if [ ! -f "CMakeLists.txt" ] || [ ! -d "src" ]; then
    echo -e "${RED}ERROR: Not in PX4-Autopilot root directory${NC}"
    echo "Current directory: $(pwd)"
    exit 1
fi
echo -e "${GREEN}✓ PX4 directory validated${NC}"
echo ""

# Step 2: Check for Vimana module
echo -e "${YELLOW}[2/8] Checking Vimana module files...${NC}"
if [ ! -f "src/modules/vimana/vimana_main.cpp" ]; then
    echo -e "${RED}ERROR: vimana_main.cpp not found${NC}"
    exit 1
fi
if [ ! -f "src/modules/vimana/CMakeLists.txt" ]; then
    echo -e "${RED}ERROR: vimana CMakeLists.txt not found${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Vimana module files present${NC}"
echo ""

# Step 3: Check board configuration
echo -e "${YELLOW}[3/8] Checking board configuration...${NC}"
BOARD_CONFIG="boards/cubepilot/cubeorangeplus/vimana_vtol.px4board"
if [ ! -f "$BOARD_CONFIG" ]; then
    echo -e "${RED}ERROR: Board config not found: $BOARD_CONFIG${NC}"
    exit 1
fi

# Validate CONFIG_MODULES_VIMANA is enabled
if ! grep -q "CONFIG_MODULES_VIMANA=y" "$BOARD_CONFIG"; then
    echo -e "${RED}ERROR: CONFIG_MODULES_VIMANA=y not found in board config${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Board configuration validated${NC}"
echo ""

# Step 4: Clean previous builds (safety measure)
echo -e "${YELLOW}[4/8] Cleaning previous builds...${NC}"
read -p "Clean build directory? (recommended) [y/N]: " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    make distclean
    echo -e "${GREEN}✓ Build cleaned${NC}"
else
    echo -e "${YELLOW}⚠ Skipping clean (using cached build)${NC}"
fi
echo ""

# Step 5: Pre-build validation
echo -e "${YELLOW}[5/8] Running pre-build checks...${NC}"

# Check for common syntax errors in vimana_main.cpp
echo "  → Checking for common C++ errors..."
if grep -q "delete this" "src/modules/vimana/vimana_main.cpp"; then
    echo -e "${RED}ERROR: Dangerous 'delete this' found${NC}"
    exit 1
fi

# Validate CMakeLists.txt syntax
echo "  → Validating CMakeLists.txt..."
if ! grep -q "px4_add_module" "src/modules/vimana/CMakeLists.txt"; then
    echo -e "${RED}ERROR: Invalid CMakeLists.txt${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Pre-build checks passed${NC}"
echo ""

# Step 6: Build firmware
echo -e "${YELLOW}[6/8] Building firmware...${NC}"
echo "Target: cubepilot_cubeorangeplus_vimana_vtol"
echo ""

BUILD_LOG="build_$(date +%Y%m%d_%H%M%S).log"

if make cubepilot_cubeorangeplus_vimana_vtol 2>&1 | tee "$BUILD_LOG"; then
    echo ""
    echo -e "${GREEN}✓ Build successful!${NC}"
else
    echo ""
    echo -e "${RED}✗ Build failed. Check log: $BUILD_LOG${NC}"
    echo ""
    echo "Common issues:"
    echo "  1. Missing dependencies - run: git submodule update --init --recursive"
    echo "  2. Syntax errors in vimana_main.cpp"
    echo "  3. Missing uORB topic includes"
    exit 1
fi
echo ""

# Step 7: Validate output
echo -e "${YELLOW}[7/8] Validating build output...${NC}"
FIRMWARE_FILE="build/cubepilot_cubeorangeplus_vimana_vtol/cubepilot_cubeorangeplus_vimana_vtol.px4"

if [ ! -f "$FIRMWARE_FILE" ]; then
    echo -e "${RED}ERROR: Firmware file not generated${NC}"
    exit 1
fi

# Check file size (should be reasonable, not empty or corrupted)
FILE_SIZE=$(stat -f%z "$FIRMWARE_FILE" 2>/dev/null || stat -c%s "$FIRMWARE_FILE" 2>/dev/null)
if [ "$FILE_SIZE" -lt 100000 ]; then
    echo -e "${RED}ERROR: Firmware file too small ($FILE_SIZE bytes) - likely corrupted${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Firmware file validated${NC}"
echo "  Size: $(($FILE_SIZE / 1024)) KB"
echo "  Path: $FIRMWARE_FILE"
echo ""

# Step 8: Safety reminders
echo -e "${YELLOW}[8/8] Build complete - Safety checklist:${NC}"
echo ""
echo "📋 PRE-FLASH CHECKLIST:"
echo "  [ ] Remove propellers from vehicle"
echo "  [ ] Disconnect battery/power"
echo "  [ ] Connect to QGroundControl via USB only"
echo "  [ ] Verify this is a TEST vehicle (not production)"
echo "  [ ] Have stock firmware ready to flash back"
echo "  [ ] Have debug probe available (if possible)"
echo ""
echo "🔧 TO FLASH:"
echo "  1. Open QGroundControl"
echo "  2. Go to: Vehicle Setup > Firmware"
echo "  3. Select 'Advanced Settings'"
echo "  4. Choose 'Custom firmware file'"
echo "  5. Select: $FIRMWARE_FILE"
echo ""
echo "⚠️  FIRST BOOT TESTING:"
echo "  1. Connect via USB (no battery)"
echo "  2. Open console: Tools > MAVLink Console"
echo "  3. Check boot messages for errors"
echo "  4. Run: vimana status"
echo "  5. Run: vimana start"
echo "  6. Check: listener vtol_vehicle_status"
echo ""
echo "🚨 IF SOMETHING GOES WRONG:"
echo "  1. Power cycle the flight controller"
echo "  2. Flash stock firmware via QGroundControl"
echo "  3. Check build logs in: $BUILD_LOG"
echo ""
echo -e "${GREEN}Build log saved to: $BUILD_LOG${NC}"
echo -e "${GREEN}Firmware ready at: $FIRMWARE_FILE${NC}"
echo ""
