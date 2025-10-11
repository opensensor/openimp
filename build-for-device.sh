#!/bin/bash
# Build script for cross-compiling OpenIMP for Ingenic T31 device
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
TOOLCHAIN_PATH=/home/matteius/output/wyze_cam3_t31x_gc2053_rtl8189ftv/per-package/toolchain-external-custom/host/bin/
PLATFORM=T31

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

