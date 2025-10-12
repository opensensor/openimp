# OpenIMP - Open Implementation of Ingenic Media Platform

This project provides an open-source stub implementation of the Ingenic Media Platform (IMP) libraries, reverse-engineered from the T31 kernel module and designed to be compatible with the [prudynt-t](https://github.com/gtxaspec/prudynt-t) project.

## Project Goals

1. **Scope-Limited Implementation**: Only implement the IMP API functions actually used by prudynt-t
2. **Binary Analysis**: Use Binary Ninja to reverse engineer data structures from the actual kernel module
3. **API Compatibility**: Provide header-compatible interfaces for compilation
4. **Stub Implementation**: Initial implementation will be stubs that can be filled in with actual functionality

## Project Structure

```
openimp/
├── include/
│   ├── imp/
│   │   ├── imp_common.h       # Common definitions
│   │   ├── imp_system.h       # System module
│   │   ├── imp_isp.h          # ISP module
│   │   ├── imp_framesource.h  # Frame source module
│   │   ├── imp_encoder.h      # Encoder module
│   │   ├── imp_audio.h        # Audio module
│   │   ├── imp_osd.h          # OSD module
│   │   ├── imp_ivs.h          # IVS module
│   │   └── imp_ivs_move.h     # IVS motion detection
│   └── sysutils/
│       └── su_base.h          # Sysutils base
├── src/
│   ├── imp_system.c           # System implementation
│   ├── imp_isp.c              # ISP implementation
│   ├── imp_framesource.c      # Frame source implementation
│   ├── imp_encoder.c          # Encoder implementation
│   ├── imp_audio.c            # Audio implementation
│   ├── imp_osd.c              # OSD implementation
│   ├── imp_ivs.c              # IVS implementation
│   └── su_base.c              # Sysutils implementation
├── tests/
│   └── api_test.c             # API surface test
├── Makefile                   # Build system
├── IMP_API_ANALYSIS.md        # API analysis document
└── README.md                  # This file
```

## API Modules

### Implemented Modules

- **IMP_System**: System initialization, cell binding, versioning
- **IMP_ISP**: Image Signal Processor control and tuning
- **IMP_FrameSource**: Video frame source management
- **IMP_Encoder**: Video encoding (H264/H265/JPEG)
- **IMP_AI**: Audio input
- **IMP_AENC**: Audio encoding
- **IMP_ADEC**: Audio decoding
- **IMP_OSD**: On-Screen Display
- **IMP_IVS**: Intelligent Video System (motion detection)
- **SU_Base**: Sysutils base functions

## Platform Support

The implementation targets compatibility with multiple Ingenic SoC platforms:
- T21
- T23
- T31 (primary target)
- C100
- T40
- T41

Platform-specific differences are handled through conditional compilation.

## Building

### Prerequisites

- GCC or compatible C compiler
- Make
- pthread library
- For cross-compilation: appropriate toolchain (e.g., mipsel-linux-gnu-gcc)

### Build Commands

```bash
# Build for T31 (default)
make

# Build for other platforms
make PLATFORM=T23
make PLATFORM=T40

# Cross-compile for MIPS
make CC=mipsel-linux-gnu-gcc

# Install to system
sudo make install

# Install to custom location
make install PREFIX=$HOME/.local

# Clean build artifacts
make clean
```

### Build Output

This will build the following libraries in the `lib/` directory:
- `libimp.so` - IMP shared library
- `libimp.a` - IMP static library
- `libsysutils.so` - Sysutils shared library
- `libsysutils.a` - Sysutils static library

## Testing

```bash
make test
```

Runs the API surface test to verify all expected functions are present and callable.

## Usage with prudynt-t

To use this library with prudynt-t:

1. Build the libraries:
   ```bash
   make
   ```

2. Install headers and libraries to your toolchain:
   ```bash
   make install PREFIX=/path/to/toolchain
   ```

3. Build prudynt-t against these libraries

## Development Status

- [x] API analysis complete (130+ functions documented)
- [x] Project structure created
- [x] Header files defined (all modules)
- [x] Stub implementations created (all modules)
- [x] Data structures reverse engineered from binary
- [x] Build system working (Makefile with platform support)
- [x] API surface test passing
- [ ] Hardware integration (ISP, encoder, audio)
- [ ] Functional video pipeline
- [ ] Functional audio pipeline
- [ ] Memory management (VBM, physical memory)

**Current Status**: Fully functional stub implementation. All API functions are present and callable, but hardware interaction is not implemented. Suitable for development and testing of applications that use the IMP API.


## 24-hour milestone: End-to-end RTSP streaming on T31

In under 24 hours we achieved a full bring-up of prudynt-t streaming on Ingenic T31 using this OpenIMP shim:
- Cross-compiled prudynt-t against the Buildroot toolchain and staging IMP/Live555 libs
- Fixed a startup crash by default-initializing config when no prudynt.json is present
- Brought up RTSP server (Live555) and confirmed video streaming from ch0/ch1
- Resolved a Live555 fast-profile/truncation issue that caused the stream to freeze after a few seconds
  - Defensive measure: advertise a larger `maxFrameSize` from the source to prevent NAL truncation

How to reproduce the milestone quickly:
1. Build OpenIMP (this repo): `make`
2. Build prudynt-t (see prudynt-t/README.md for exact env/toolchain notes); produce `prudynt-t/bin/prudynt`
3. Deploy `prudynt` to the device (e.g., `/opt`) and run it; connect to `rtsp://<device-ip>/ch0`

Notes:
- If you see any freeze on large IDR frames, ensure your Live555 is patched (fast profile) and/or increase the source max frame size.
- Optional: set a shared RTP timestamp base to keep sinks aligned.

## Contributing

This is a reverse engineering project. Contributions should:
1. Reference the Binary Ninja analysis where applicable
2. Maintain API compatibility with prudynt-t usage
3. Document any assumptions or unknowns

## License

This is a clean-room reverse engineering effort. The API signatures are derived from public headers and usage in open-source projects like prudynt-t.

## Documentation

- **IMP_API_ANALYSIS.md** - Comprehensive analysis of all IMP functions used by prudynt-t
- **BINARY_ANALYSIS.md** - Reverse engineering findings from libimp.so binary analysis
- **STATUS.md** - Current project status and implementation details

## References

- [prudynt-t](https://github.com/gtxaspec/prudynt-t) - The primary consumer of this API
- Ingenic IMP SDK Documentation
- Binary analysis via Binary Ninja MCP (libimp.so T31 v1.1.6)

