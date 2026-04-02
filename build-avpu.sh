#!/bin/bash

# AVPU Build Script
# This script builds the AVPU kernel module with detailed logging

set -e  # Exit on any error

export CROSS_COMPILE=mipsel-linux-
export KDIR=/home/matteius/ingenic-linux/thingino-firmware/output/master/wyze_cam3_t31x_gc2053_rtl8189ftv-3.10.14-uclibc/build/linux-7354de1b0a8b9e9afc1699222f44101933244f04/
export PATH=/home/matteius/ingenic-linux/thingino-firmware/output/master/wyze_cam3_t31x_gc2053_rtl8189ftv-3.10.14-uclibc/host/bin/:$PATH

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
    export AVPU_NO_DMABUF=0
    
    # Clean previous build
    print_status "Cleaning previous build..."
    make -C "${KDIR}" M="${PWD}/avpu" clean
    
    # Build the module
    print_status "Compiling AVPU module..."
    if make -C "${KDIR}" M="${PWD}/avpu" modules; then
        print_status "Build completed successfully!"
        
        # Check if the .ko file was created
        if [[ -f "avpu/avpu.ko" ]]; then
            print_status "AVPU module built: avpu/avpu.ko"
            ls -lh avpu/avpu.ko
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
print_status "AVPU module ready: avpu/avpu.ko"

