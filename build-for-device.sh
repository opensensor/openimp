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

# Choose platform from first argument (T31 or T23). Default: T31.
PLATFORM_INPUT=${1:-T31}
PLATFORM=$(echo "$PLATFORM_INPUT" | tr '[:lower:]' '[:upper:]')

# Build mode: "ported" uses the reverse-engineered port under
# src/{alcodec,audio,core,framesource,osd,ivs,isp,codec_c,video,...};
# "legacy" uses the original flat-file stubs under src/imp_*.c.
# Override with the second arg: ./build-for-device.sh T31 legacy
BUILD=${2:-ported}

# Select toolchain per platform (adjust these paths for your environment)
case "$PLATFORM" in
  T31)
    TOOLCHAIN_PATH=/home/matteius/thingino-firmware/output/master/wyze_cam3_t31x_gc2053_rtl8189ftv-3.10.14-uclibc-192.168.50.215/host/bin/
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
echo -e "${YELLOW}Build mode:${NC} $BUILD"
echo ""

# Clean previous build
echo -e "${YELLOW}Cleaning previous build...${NC}"
make BUILD=$BUILD clean

# Build libraries
echo -e "${YELLOW}Building libraries...${NC}"
make CROSS_COMPILE=$CROSS_COMPILE PLATFORM=$PLATFORM BUILD=$BUILD -j$(nproc)

# Strip debug symbols
echo -e "${YELLOW}Stripping debug symbols...${NC}"
make CROSS_COMPILE=$CROSS_COMPILE BUILD=$BUILD strip

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

