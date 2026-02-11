#!/bin/bash
# Pre-Build Validation for Vimana Module
# Run this BEFORE building to catch issues early

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ERRORS=0
WARNINGS=0

echo -e "${GREEN}=== Vimana Module Validation ===${NC}"
echo ""

# Find PX4 root directory (go up 2 levels from tools/vimana)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PX4_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

echo "Script location: $SCRIPT_DIR"
echo "PX4 root: $PX4_ROOT"
echo ""

# Change to PX4 root for all checks
cd "$PX4_ROOT"

# Verify we're in the right place
if [ ! -f "CMakeLists.txt" ] || [ ! -d "src/modules" ]; then
    echo -e "${RED}ERROR: Could not find PX4 root directory${NC}"
    echo "Expected to find CMakeLists.txt and src/modules/"
    exit 1
fi

# Check 1: File structure
echo -e "${YELLOW}Checking file structure...${NC}"
FILES=(
    "src/modules/vimana/vimana_main.cpp"
    "src/modules/vimana/CMakeLists.txt"
    "boards/cubepilot/cubeorangeplus/vimana_vtol.px4board"
)

for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo -e "  ${GREEN}✓${NC} $file"
    else
        echo -e "  ${RED}✗${NC} $file (MISSING)"
        ((ERRORS++))
    fi
done
echo ""

# Check 2: Code analysis
echo -e "${YELLOW}Analyzing vimana_main.cpp...${NC}"

VIMANA_CPP="src/modules/vimana/vimana_main.cpp"

# Check for required includes
REQUIRED_INCLUDES=(
    "px4_platform_common/module.h"
    "uORB/topics/vtol_vehicle_status.h"
    "uORB/topics/vehicle_attitude.h"
)

for include in "${REQUIRED_INCLUDES[@]}"; do
    if grep -q "#include.*$include" "$VIMANA_CPP"; then
        echo -e "  ${GREEN}✓${NC} Include: $include"
    else
        echo -e "  ${RED}✗${NC} Missing include: $include"
        ((ERRORS++))
    fi
done

# Check for dangerous patterns
echo ""
echo -e "${YELLOW}Checking for dangerous patterns...${NC}"

if grep -q "delete this" "$VIMANA_CPP"; then
    echo -e "  ${RED}✗${NC} Found 'delete this' - DANGEROUS"
    ((ERRORS++))
else
    echo -e "  ${GREEN}✓${NC} No 'delete this' found"
fi

if grep -q "malloc\|calloc\|free" "$VIMANA_CPP"; then
    echo -e "  ${YELLOW}⚠${NC} Found C-style memory allocation (use new/delete)"
    ((WARNINGS++))
else
    echo -e "  ${GREEN}✓${NC} No C-style malloc/free"
fi

# Check for proper cleanup
if grep -q "orb_unsubscribe" "$VIMANA_CPP"; then
    echo -e "  ${GREEN}✓${NC} Proper uORB cleanup found"
else
    echo -e "  ${YELLOW}⚠${NC} No orb_unsubscribe found (consider adding in destructor)"
    ((WARNINGS++))
fi

echo ""

# Check 3: CMakeLists.txt validation
echo -e "${YELLOW}Validating CMakeLists.txt...${NC}"

CMAKE_FILE="src/modules/vimana/CMakeLists.txt"

if grep -q "px4_add_module" "$CMAKE_FILE"; then
    echo -e "  ${GREEN}✓${NC} px4_add_module found"
else
    echo -e "  ${RED}✗${NC} Missing px4_add_module"
    ((ERRORS++))
fi

if grep -q "MODULE modules__vimana" "$CMAKE_FILE"; then
    echo -e "  ${GREEN}✓${NC} Correct module name"
else
    echo -e "  ${RED}✗${NC} Incorrect module name (should be modules__vimana)"
    ((ERRORS++))
fi

if grep -q "MAIN vimana" "$CMAKE_FILE"; then
    echo -e "  ${GREEN}✓${NC} Main function defined"
else
    echo -e "  ${RED}✗${NC} Missing MAIN definition"
    ((ERRORS++))
fi

# Check stack size
if grep -q "STACK_MAIN" "$CMAKE_FILE"; then
    STACK_SIZE=$(grep "STACK_MAIN" "$CMAKE_FILE" | grep -o '[0-9]*')
    if [ "$STACK_SIZE" -ge 2048 ]; then
        echo -e "  ${GREEN}✓${NC} Stack size adequate ($STACK_SIZE bytes)"
    else
        echo -e "  ${YELLOW}⚠${NC} Stack size may be too small ($STACK_SIZE bytes)"
        ((WARNINGS++))
    fi
else
    echo -e "  ${YELLOW}⚠${NC} No stack size specified (will use default)"
    ((WARNINGS++))
fi

echo ""

# Check 4: Board configuration
echo -e "${YELLOW}Validating board configuration...${NC}"

BOARD_CONFIG="boards/cubepilot/cubeorangeplus/vimana_vtol.px4board"

if grep -q "CONFIG_MODULES_VIMANA=y" "$BOARD_CONFIG"; then
    echo -e "  ${GREEN}✓${NC} Vimana module enabled"
else
    echo -e "  ${RED}✗${NC} Vimana module not enabled in config"
    ((ERRORS++))
fi

# Check for essential VTOL modules
REQUIRED_MODULES=(
    "CONFIG_MODULES_VTOL_ATT_CONTROL=y"
    "CONFIG_MODULES_MC_ATT_CONTROL=y"
    "CONFIG_MODULES_FW_ATT_CONTROL=y"
)

for module in "${REQUIRED_MODULES[@]}"; do
    if grep -q "$module" "$BOARD_CONFIG"; then
        echo -e "  ${GREEN}✓${NC} $module"
    else
        echo -e "  ${RED}✗${NC} Missing: $module"
        ((ERRORS++))
    fi
done

echo ""

# Check 5: Memory considerations
echo -e "${YELLOW}Checking memory usage...${NC}"

# Count number of subscriptions (rough memory estimate)
SUB_COUNT=$(grep -c "orb_subscribe" "$VIMANA_CPP" || echo "0")
echo "  → $SUB_COUNT uORB subscriptions found"

if [ "$SUB_COUNT" -gt 10 ]; then
    echo -e "  ${YELLOW}⚠${NC} High number of subscriptions may increase RAM usage"
    ((WARNINGS++))
else
    echo -e "  ${GREEN}✓${NC} Reasonable subscription count"
fi

echo ""

# Summary
echo -e "${GREEN}=== Validation Summary ===${NC}"
echo ""

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✓ All checks passed! Ready to build.${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Make backup of current firmware"
    echo "  2. Run: ./safe_build.sh"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠ $WARNINGS warning(s) found${NC}"
    echo -e "${GREEN}✓ No critical errors${NC}"
    echo ""
    echo "You can proceed with build, but review warnings above."
    exit 0
else
    echo -e "${RED}✗ $ERRORS error(s) found${NC}"
    echo -e "${YELLOW}⚠ $WARNINGS warning(s) found${NC}"
    echo ""
    echo "Please fix errors before building."
    exit 1
fi
