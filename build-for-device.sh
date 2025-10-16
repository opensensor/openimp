#!/bin/bash
# Build script for cross-compiling OpenIMP for Ingenic T23/T31 devices
# This script sets up the cross-compilation environment and builds the libraries

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}OpenIMP Cross-Compilation Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Configuration
CROSS_COMPILE=mipsel-linux-

# Choose platform from first argument (T31 or T23). Default: T23.
PLATFORM_INPUT=${1:-T23}
PLATFORM=$(echo "$PLATFORM_INPUT" | tr '[:lower:]' '[:upper:]')

# Select toolchain per platform (adjust these paths for your environment)
case "$PLATFORM" in
  T31)
    TOOLCHAIN_PATH=/home/matteius/output-stable/wyze_cam3_t31x_gc2053_rtl8189ftv/host/bin/
    ;;
  T23)
    TOOLCHAIN_PATH=/home/matteius/output-openimp-override/cinnado_d1_t23n_sc2336_atbm6012bx/per-package/toolchain-buildroot/host/bin/
    ;;
  *)
    echo -e "${RED}Error: Unsupported platform '$PLATFORM_INPUT'. Use: T31 or T23${NC}"
    exit 1
    ;;
esac

# Check if toolchain exists
if [ ! -d "$TOOLCHAIN_PATH" ]; then
    echo -e "${RED}Error: Toolchain not found at $TOOLCHAIN_PATH${NC}"
    exit 1
fi

# Export toolchain path
export PATH=$TOOLCHAIN_PATH:$PATH

# Verify cross-compiler is available
if ! command -v ${CROSS_COMPILE}gcc &> /dev/null; then
    echo -e "${RED}Error: ${CROSS_COMPILE}gcc not found in PATH${NC}"
    exit 1
fi

echo -e "${YELLOW}Toolchain:${NC} ${CROSS_COMPILE}gcc"
echo -e "${YELLOW}Platform:${NC}  $PLATFORM"
echo -e "${YELLOW}Path:${NC}      $TOOLCHAIN_PATH"
echo ""

# Clean previous build
echo -e "${YELLOW}Cleaning previous build...${NC}"
make clean

# Build libraries
echo -e "${YELLOW}Building libraries...${NC}"
make CROSS_COMPILE=$CROSS_COMPILE PLATFORM=$PLATFORM -j$(nproc)

# Strip debug symbols
echo -e "${YELLOW}Stripping debug symbols...${NC}"
make CROSS_COMPILE=$CROSS_COMPILE strip

echo ""
echo -e "${GREEN}Build complete!${NC}"
echo ""

# Show library information
echo -e "${YELLOW}Library information:${NC}"
ls -lh lib/
echo ""

echo -e "${YELLOW}File types:${NC}"
file lib/*.so
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Libraries ready for deployment:${NC}"
echo -e "  ${YELLOW}lib/libimp.so${NC}      - Main IMP library"
echo -e "  ${YELLOW}lib/libimp.a${NC}       - Static IMP library"
echo -e "  ${YELLOW}lib/libsysutils.so${NC} - SysUtils library"
echo -e "  ${YELLOW}lib/libsysutils.a${NC}  - Static SysUtils library"
echo -e "${GREEN}========================================${NC}"

