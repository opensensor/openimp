# OpenIMP Project Status

## Overview

OpenIMP is a reverse-engineered, open-source implementation of the Ingenic Media Platform (IMP) libraries. The goal is to provide API-compatible stub libraries that can be used for development and testing of applications that depend on IMP, particularly the prudynt-t camera streaming application.

## Current Status: ✅ FUNCTIONAL STUB IMPLEMENTATION

### Completed

1. **Binary Analysis** ✅
   - Analyzed libimp.so (T31 v1.1.6) using Binary Ninja
   - Extracted version information, function signatures, and data structures
   - Documented internal implementation details (observer pattern, module registry, etc.)
   - See: `BINARY_ANALYSIS.md`

2. **API Surface Analysis** ✅
   - Identified all 130+ IMP functions used by prudynt-t
   - Organized functions by module (System, ISP, FrameSource, Encoder, Audio, OSD, IVS)
   - Documented platform-specific differences (T20/T21/T23 vs T31/C100 vs T40/T41)
   - See: `IMP_API_ANALYSIS.md`

3. **Header Files** ✅
   - `include/imp/imp_common.h` - Common types and structures
   - `include/imp/imp_system.h` - System module
   - `include/imp/imp_isp.h` - ISP module with tuning functions
   - `include/imp/imp_framesource.h` - Frame source module
   - `include/imp/imp_encoder.h` - Encoder module
   - `include/imp/imp_audio.h` - Audio input/output/encoding/decoding
   - `include/imp/imp_osd.h` - On-screen display
   - `include/imp/imp_ivs.h` - Intelligent video system
   - `include/imp/imp_ivs_move.h` - Motion detection interface
   - `include/sysutils/su_base.h` - Sysutils base module

4. **Stub Implementations** ✅
   - `src/imp_system.c` - System module with timestamp, version, binding
   - `src/imp_isp.c` - ISP module with tuning stubs
   - `src/imp_framesource.c` - Frame source stubs
   - `src/imp_encoder.c` - Encoder stubs
   - `src/imp_audio.c` - Audio input/output/encoding/decoding stubs
   - `src/imp_osd.c` - OSD stubs
   - `src/imp_ivs.c` - IVS stubs
   - `src/su_base.c` - Sysutils base implementation

5. **Build System** ✅
   - Makefile with support for multiple platforms
   - Builds both shared (.so) and static (.a) libraries
   - Platform selection via `PLATFORM=` variable (T21, T23, T31, C100, T40, T41)
   - Cross-compilation support via `CC=` variable
   - Installation target with configurable `PREFIX=`

6. **Testing** ✅
   - `tests/api_test.c` - API surface test
   - Verifies all major functions are present and callable
   - Tests basic functionality of each module
   - All tests passing ✅

### Build Instructions

```bash
# Build for T31 (default)
make

# Build for other platforms
make PLATFORM=T23
make PLATFORM=T40

# Cross-compile
make CC=mipsel-linux-gnu-gcc

# Install
make install PREFIX=/usr/local

# Run tests
make test

# Clean
make clean
```

### Library Output

- `lib/libimp.so` - Shared library
- `lib/libimp.a` - Static library
- `lib/libsysutils.so` - Sysutils shared library
- `lib/libsysutils.a` - Sysutils static library

### Test Results

```
OpenIMP API Surface Test
=========================

Testing IMP_System...
  IMP_System_Init: OK
  IMP_System_GetVersion: OK (version: IMP-1.1.6)
  IMP_System_GetCPUInfo: T31
  IMP_System_GetTimeStamp: 11 us
  IMP_System_RebaseTimeStamp: OK
  IMP_System_Bind: OK
  IMP_System_UnBind: OK

Testing SU_Base...
  SU_Base_GetVersion: OK (version: SU-1.0.0)

Testing IMP_ISP...
  IMP_ISP_Open: OK
  IMP_ISP_AddSensor: OK
  IMP_ISP_EnableSensor: OK
  IMP_ISP_EnableTuning: OK
  IMP_ISP_Tuning_SetSensorFPS: OK

Testing IMP_FrameSource...
  IMP_FrameSource_CreateChn: OK
  IMP_FrameSource_EnableChn: OK

Testing IMP_Encoder...
  IMP_Encoder_CreateGroup: OK
  IMP_Encoder_SetDefaultParam: OK
  IMP_Encoder_CreateChn: OK
  IMP_Encoder_RegisterChn: OK

Testing IMP_Audio...
  IMP_AI_Enable: OK
  IMP_AI_SetPubAttr: OK
  IMP_AI_EnableChn: OK

Testing IMP_OSD...
  IMP_OSD_SetPoolSize: OK
  IMP_OSD_CreateGroup: OK
  IMP_OSD_CreateRgn: OK

Testing IMP_IVS...
  IMP_IVS_CreateGroup: OK

All API tests completed!
```

## Implementation Details

### What Works

1. **System Module**
   - Initialization and cleanup
   - Version reporting (IMP-1.1.6)
   - CPU info (platform-specific)
   - Timestamp functions using CLOCK_MONOTONIC
   - Module binding/unbinding (stub)

2. **ISP Module**
   - Sensor management (add/remove/enable/disable)
   - Tuning functions (all stubbed but callable)
   - Platform-specific variants (T40/T41 with IMPVI parameter)

3. **FrameSource Module**
   - Channel creation/destruction
   - Channel enable/disable
   - Attribute get/set
   - FIFO configuration
   - Rotation support (T31-specific)

4. **Encoder Module**
   - Group and channel management
   - H264/H265/JPEG support
   - Rate control modes (FIXQP, CBR, VBR, etc.)
   - Default parameter setup
   - Stream operations (stubbed)

5. **Audio Module**
   - Audio input (AI) operations
   - Audio output (AO) operations
   - Audio encoding (AENC)
   - Audio decoding (ADEC)
   - Noise suppression, HPF, AGC

6. **OSD Module**
   - Group and region management
   - Multiple region types (LINE, RECT, BITMAP, COVER, PIC)
   - Attribute management

7. **IVS Module**
   - Group and channel management
   - Motion detection interface

### What Doesn't Work (Yet)

1. **Hardware Interaction**
   - No actual ISP control
   - No actual video encoding/decoding
   - No actual audio capture/playback
   - No actual OSD rendering

2. **Data Flow**
   - Module binding is stubbed (no actual data flow)
   - GetStream operations return "no data available"
   - GetFrame operations return "no data available"

3. **Memory Management**
   - No VBM (Video Buffer Manager) implementation
   - No physical memory allocation
   - No cache management

### Next Steps

To make this a fully functional implementation, you would need to:

1. **Kernel Driver Integration**
   - Interface with tx-isp kernel module for ISP control
   - Interface with video encoder/decoder hardware
   - Interface with audio hardware

2. **Video Pipeline**
   - Implement actual frame capture from ISP
   - Implement actual video encoding (could use software encoder initially)
   - Implement buffer management and data flow

3. **Audio Pipeline**
   - Implement actual audio capture (ALSA)
   - Implement actual audio encoding (software codecs)
   - Implement audio playback

4. **OSD Rendering**
   - Implement bitmap/text rendering
   - Implement overlay composition

5. **Memory Management**
   - Implement VBM for buffer pooling
   - Implement physical memory allocation (CMA/ION)
   - Implement cache management

## Usage with prudynt-t

To use this stub library with prudynt-t:

1. Build the library:
   ```bash
   make PLATFORM=T31
   ```

2. Install the library:
   ```bash
   make install PREFIX=/path/to/install
   ```

3. Build prudynt-t against the stub library:
   ```bash
   export PKG_CONFIG_PATH=/path/to/install/lib/pkgconfig
   cd prudynt-t
   make
   ```

4. Note: The application will link and run, but won't produce actual video/audio output since the hardware interaction is stubbed.

## License

This is a reverse-engineered implementation for educational and development purposes. The API is compatible with Ingenic's IMP library, but the implementation is original.

## Contributing

Contributions are welcome! Areas that need work:

- Hardware integration (ISP, encoder, audio)
- Video buffer management
- Memory management
- Platform-specific implementations
- Testing on actual hardware
- Documentation improvements

## References

- Ingenic IMP SDK Documentation
- prudynt-t source code: https://github.com/gtxaspec/prudynt-t
- Binary analysis: `BINARY_ANALYSIS.md`
- API analysis: `IMP_API_ANALYSIS.md`

