#!/bin/bash
# Setup script for Vimana VTOL Module Development
# Run this from your PX4-Autopilot root directory

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}"
echo "╔══════════════════════════════════════════════════════════╗"
echo "║         Vimana Tailsitter VTOL Module Setup             ║"
echo "║              Safe Build Environment                      ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo -e "${NC}"
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ] || [ ! -d "src/modules" ]; then
    echo -e "${RED}ERROR: Not in PX4-Autopilot root directory${NC}"
    echo "Current directory: $(pwd)"
    echo ""
    echo "Please cd to your PX4 root directory first, then run:"
    echo "  bash $SCRIPT_DIR/setup_vimana.sh"
    exit 1
fi

echo -e "${GREEN}✓ Running in PX4 root directory: $(pwd)${NC}"
echo ""

# Create tools directory
TOOLS_DIR="tools/vimana"
echo -e "${YELLOW}Creating tools directory: $TOOLS_DIR${NC}"
mkdir -p "$TOOLS_DIR"

# Check which files exist in script directory
echo "Looking for helper files in: $SCRIPT_DIR"
echo ""

FILES_TO_COPY=(
    "validate_module.sh"
    "safe_build.sh"
    "TESTING_GUIDE.md"
    "RECOVERY_GUIDE.md"
    "QUICKSTART.md"
)

COPIED=0
for file in "${FILES_TO_COPY[@]}"; do
    if [ -f "$SCRIPT_DIR/$file" ]; then
        cp "$SCRIPT_DIR/$file" "$TOOLS_DIR/"
        if [[ $file == *.sh ]]; then
            chmod +x "$TOOLS_DIR/$file"
        fi
        echo -e "${GREEN}✓${NC} Copied: $file"
        ((COPIED++))
    else
        echo -e "${YELLOW}⚠${NC} Not found: $file (skipping)"
    fi
done

echo ""
echo -e "${GREEN}✓ Copied $COPIED files to $TOOLS_DIR/${NC}"
echo ""

# Create backup directory
BACKUP_DIR="backup/vimana"
echo -e "${YELLOW}Creating backup directory: $BACKUP_DIR${NC}"
mkdir -p "$BACKUP_DIR"
echo -e "${GREEN}✓ Backup directory created${NC}"
echo ""

# Verify Vimana module exists
echo -e "${YELLOW}Verifying Vimana module...${NC}"
if [ -f "src/modules/vimana/vimana_main.cpp" ]; then
    echo -e "${GREEN}✓ vimana_main.cpp found${NC}"
else
    echo -e "${RED}✗ vimana_main.cpp NOT found${NC}"
    echo "  Expected location: src/modules/vimana/vimana_main.cpp"
    echo ""
    echo "  Please create your Vimana module first!"
fi

if [ -f "src/modules/vimana/CMakeLists.txt" ]; then
    echo -e "${GREEN}✓ CMakeLists.txt found${NC}"
else
    echo -e "${RED}✗ CMakeLists.txt NOT found${NC}"
    echo "  Expected location: src/modules/vimana/CMakeLists.txt"
fi
echo ""

# Verify board config
echo -e "${YELLOW}Verifying board configuration...${NC}"
BOARD_CONFIG="boards/cubepilot/cubeorangeplus/vimana_vtol.px4board"
if [ -f "$BOARD_CONFIG" ]; then
    echo -e "${GREEN}✓ Board config found${NC}"

    if grep -q "CONFIG_MODULES_VIMANA=y" "$BOARD_CONFIG"; then
        echo -e "${GREEN}✓ Vimana module enabled in config${NC}"
    else
        echo -e "${YELLOW}⚠ Vimana module NOT enabled in config${NC}"
        echo "  Please add: CONFIG_MODULES_VIMANA=y"
    fi
else
    echo -e "${YELLOW}⚠ Board config NOT found${NC}"
    echo "  Expected: $BOARD_CONFIG"
    echo "  (This is OK if you're using a different board config)"
fi
echo ""

# Git status check
echo -e "${YELLOW}Git status check...${NC}"
if git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Git repository detected${NC}"

    # Show modified files
    MODIFIED=$(git status --porcelain 2>/dev/null | grep -c "^.M" || echo "0")
    UNTRACKED=$(git status --porcelain 2>/dev/null | grep -c "^??" || echo "0")

    echo "  Modified files: $MODIFIED"
    echo "  Untracked files: $UNTRACKED"

    if [ "$MODIFIED" -gt 0 ] || [ "$UNTRACKED" -gt 0 ]; then
        echo ""
        echo -e "${YELLOW}  Consider creating a git backup:${NC}"
        echo "  git stash push -m 'vimana-backup-$(date +%Y%m%d)'"
    fi
else
    echo -e "${YELLOW}⚠ Not a git repository${NC}"
fi
echo ""

# Download stock firmware
echo -e "${YELLOW}Checking for stock firmware backup...${NC}"
STOCK_FW="$BACKUP_DIR/stock_px4_fmu-v5_default.px4"
if [ -f "$STOCK_FW" ]; then
    echo -e "${GREEN}✓ Stock firmware backup found${NC}"
    SIZE=$(ls -lh "$STOCK_FW" | awk '{print $5}')
    echo "  Size: $SIZE"
else
    echo -e "${YELLOW}⚠ No stock firmware backup${NC}"
    echo ""
    read -p "Download stock firmware now? [y/N]: " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Downloading latest stable PX4..."
        LATEST_URL="https://github.com/PX4/PX4-Autopilot/releases/latest/download/px4_fmu-v5_default.px4"
        if command -v wget &> /dev/null; then
            wget -O "$STOCK_FW" "$LATEST_URL"
        elif command -v curl &> /dev/null; then
            curl -L -o "$STOCK_FW" "$LATEST_URL"
        else
            echo -e "${RED}Neither wget nor curl found. Please download manually:${NC}"
            echo "$LATEST_URL"
        fi

        if [ -f "$STOCK_FW" ]; then
            echo -e "${GREEN}✓ Stock firmware downloaded${NC}"
        fi
    fi
fi
echo ""

# Summary
echo -e "${BLUE}"
echo "╔══════════════════════════════════════════════════════════╗"
echo "║                    Setup Complete!                       ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo -e "${NC}"
echo ""
echo "📁 Files installed in: $TOOLS_DIR"
echo ""
echo "📝 Next Steps:"
echo ""
echo "1️⃣  Validate module:"
echo "   cd $TOOLS_DIR"
echo "   ./validate_module.sh"
echo ""
echo "2️⃣  Build firmware (if validation passes):"
echo "   ./safe_build.sh"
echo ""
echo "3️⃣  Read testing guide:"
echo "   cat TESTING_GUIDE.md"
echo ""
echo "📦 Stock firmware backup: $BACKUP_DIR"
echo ""
echo -e "${YELLOW}⚠️  CRITICAL SAFETY REMINDERS:${NC}"
echo "   • Remove ALL propellers for initial testing"
echo "   • NEVER test on flying vehicle first"
echo "   • Have stock firmware ready to flash back"
echo "   • Read TESTING_GUIDE.md completely before flight"
echo ""
echo -e "${GREEN}Good luck with your Vimana tailsitter! 🚁${NC}"
echo ""
