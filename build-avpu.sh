#!/bin/bash

# AVPU Build Script
# This script builds the AVPU kernel module with detailed logging

set -e  # Exit on any error

# Kernel tree and toolchain come from the environment, e.g. a thingino
# output directory:
#   O=.../output/<board>-3.10.14-uclibc
#   KDIR=$O/build/linux-3.10.14 TOOLCHAIN_BIN=$O/host/bin ./build-avpu.sh
export CROSS_COMPILE="${CROSS_COMPILE:-mipsel-linux-}"
if [[ -z "${KDIR:-}" || ! -d "${KDIR}" ]]; then
    echo "KDIR must point to a configured and built kernel tree" >&2
    exit 1
fi
export KDIR
if [[ -n "${TOOLCHAIN_BIN:-}" ]]; then
    export PATH="${TOOLCHAIN_BIN}:$PATH"
fi
# Kernels without CONFIG_DMA_SHARED_BUFFER may need AVPU_NO_DMABUF=1.
AVPU_NO_DMABUF="${AVPU_NO_DMABUF:-0}"
# Module source directory (an out-of-tree copy keeps the tracked one clean).
AVPU_SRC="${AVPU_SRC:-$PWD/avpu}"

# Configuration
TARGET="${TARGET:-t31}"                 # Default target, can be overridden
KERNEL_VERSION="${KERNEL_VERSION:-3.10}"  # Default kernel version

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to build the AVPU module
build_avpu() {
    print_status "Building AVPU kernel module for ${TARGET}..."
    
    # Set environment variables for the build
    export SOC="${TARGET}"
    export AVPU_NO_DMABUF
    
    # Clean previous build
    print_status "Cleaning previous build..."
    make -C "${KDIR}" M="${AVPU_SRC}" clean
    
    # Build the module
    print_status "Compiling AVPU module..."
    if make -C "${KDIR}" M="${AVPU_SRC}" modules; then
        print_status "Build completed successfully!"
        
        # Check if the .ko file was created
        if [[ -f "${AVPU_SRC}/avpu.ko" ]]; then
            print_status "AVPU module built: ${AVPU_SRC}/avpu.ko"
            ls -lh "${AVPU_SRC}/avpu.ko"
        else
            print_error "Build succeeded but avpu.ko not found"
            exit 1
        fi
    else
        print_error "Build failed"
        exit 1
    fi
}

# Help function
show_help() {
    echo "AVPU Build Script"
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -t, --target TARGET   Set build target (default: t31)"
    echo "  -h, --help           Show this help message"
    echo ""
    echo "Environment variables:"
    echo "  TARGET               Override default target"
    echo "  KERNEL_VERSION       Override default kernel version (default: 3.10)"
    echo "  KDIR                 Kernel build tree (required)"
    echo "  CROSS_COMPILE        Toolchain prefix (default: mipsel-linux-)"
    echo "  TOOLCHAIN_BIN        Directory prepended to PATH for the toolchain"
    echo "  AVPU_NO_DMABUF       1 builds without the dma-buf export (default: 0)"
    echo ""
    echo "Examples:"
    echo "  $0                   # Use all defaults (t31)"
    echo "  $0 -t t40           # Build for t40"
    echo "  TARGET=t41 $0       # Using environment variable"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Main execution
print_status "Starting AVPU build..."
print_status "Target: ${TARGET}"
print_status "Kernel: ${KDIR}"

build_avpu

print_status "All operations completed successfully!"
print_status "AVPU module ready: ${AVPU_SRC}/avpu.ko"

