# Building OpenIMP

This document describes how to build OpenIMP for different targets.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Building for Host (Development)](#building-for-host-development)
- [Cross-Compiling for Ingenic Devices](#cross-compiling-for-ingenic-devices)
- [Build Outputs](#build-outputs)
- [Installation](#installation)
- [Troubleshooting](#troubleshooting)

## Prerequisites

### For Host Build

- GCC compiler
- Make
- pthread library
- Standard C library

### For Cross-Compilation

- MIPS cross-compiler toolchain (mipsel-linux-gcc)
- Make
- Access to the Buildroot toolchain

## Building for Host (Development)

To build for your local machine (useful for development and testing):

```bash
make clean
make
```

This will create:
- `lib/libimp.so` - Shared library
- `lib/libimp.a` - Static library
- `lib/libsysutils.so` - SysUtils shared library
- `lib/libsysutils.a` - SysUtils static library

## Cross-Compiling for Ingenic Devices

### Using the Build Script (Recommended)

The easiest way to cross-compile for Ingenic T31 devices:

```bash
./build-for-device.sh
```

This script will:
1. Set up the cross-compilation environment
2. Clean previous builds
3. Build all libraries with the MIPS toolchain
4. Strip debug symbols for smaller binaries
5. Display build information

### Manual Cross-Compilation

If you need more control or want to build for a different platform:

```bash
# Set up environment
export CROSS_COMPILE=mipsel-linux-
export PATH=/path/to/toolchain/bin:$PATH

# Clean and build
make clean
make CROSS_COMPILE=mipsel-linux- PLATFORM=T31

# Optional: Strip debug symbols
make CROSS_COMPILE=mipsel-linux- strip
```

### Supported Platforms

You can specify the target platform with the `PLATFORM` variable:

- `T21` - Ingenic T21
- `T23` - Ingenic T23
- `T31` - Ingenic T31 (default)
- `C100` - Ingenic C100
- `T40` - Ingenic T40
- `T41` - Ingenic T41

Example:
```bash
make CROSS_COMPILE=mipsel-linux- PLATFORM=T40
```

## Build Outputs

After a successful build, you'll find the following in the `lib/` directory:

### Shared Libraries

- **libimp.so** (136KB stripped) - Main IMP library
  - System module
  - Encoder module
  - FrameSource module
  - Audio module
  - OSD module
  - IVS module
  - Hardware encoder integration
  - VBM (Video Buffer Manager)
  - DMA allocator

- **libsysutils.so** (66KB stripped) - System utilities library
  - Basic system utilities

### Static Libraries

- **libimp.a** (139KB) - Static version of libimp
- **libsysutils.a** (1.7KB) - Static version of libsysutils

## Installation

### Local Installation

To install to a custom location:

```bash
make install PREFIX=/path/to/install
```

Default installation paths:
- Headers: `$(PREFIX)/include/imp/` and `$(PREFIX)/include/sysutils/`
- Libraries: `$(PREFIX)/lib/`

### Device Installation

To deploy to an Ingenic device:

```bash
# Copy libraries to device
scp lib/libimp.so root@device:/usr/lib/
scp lib/libsysutils.so root@device:/usr/lib/

# Copy headers (if needed for development)
scp -r include/imp root@device:/usr/include/
scp -r include/sysutils root@device:/usr/include/
```

## Verifying the Build

### Check Binary Type

```bash
file lib/libimp.so
```

Expected output for MIPS:
```
lib/libimp.so: ELF 32-bit LSB shared object, MIPS, MIPS32 rel2 version 1 (SYSV), dynamically linked, stripped
```

### Check Library Size

```bash
ls -lh lib/
```

Expected sizes (stripped):
- libimp.so: ~136KB
- libsysutils.so: ~66KB

### Check Dependencies

```bash
mipsel-linux-readelf -d lib/libimp.so | grep NEEDED
```

Expected dependencies:
- libpthread.so.0
- librt.so.1
- libc.so.0

## Troubleshooting

### Toolchain Not Found

If you get "command not found" errors:

```bash
# Verify toolchain path
export PATH=/home/matteius/output/wyze_cam3_t31x_gc2053_rtl8189ftv/per-package/toolchain-external-custom/host/bin/:$PATH

# Verify compiler exists
which mipsel-linux-gcc
```

### Build Warnings

The build may show some warnings about:
- Unused parameters (normal for stub functions)
- Array bounds (expected due to structure size differences)
- Unused functions (normal for helper functions)

These warnings are expected and don't affect functionality.

### Clean Build

If you encounter issues, try a clean build:

```bash
make clean
./build-for-device.sh
```

## Build Configuration

### Compiler Flags

Default flags (can be overridden):
- `-Wall -Wextra` - Enable warnings
- `-O2` - Optimization level 2
- `-fPIC` - Position independent code (for shared libraries)
- `-DPLATFORM_T31` - Platform-specific defines

### Custom Flags

To add custom compiler flags:

```bash
make CFLAGS="-Wall -O3 -DCUSTOM_DEFINE"
```

## Development Workflow

For active development:

1. **Edit source files** in `src/`
2. **Rebuild**: `make`
3. **Test locally** (if possible)
4. **Cross-compile**: `./build-for-device.sh`
5. **Deploy to device**
6. **Test on device**

## Performance Optimization

For production builds, consider:

1. **Strip symbols**: `make strip` (already done by build script)
2. **Optimize for size**: `make CFLAGS="-Os -fPIC -Iinclude -DPLATFORM_T31"`
3. **Link-time optimization**: Add `-flto` to CFLAGS

## Additional Resources

- [README.md](README.md) - Project overview
- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - Implementation details
- [include/imp/](include/imp/) - API headers

