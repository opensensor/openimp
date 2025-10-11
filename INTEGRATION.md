# Integrating OpenIMP with prudynt-t

This guide explains how to use the OpenIMP stub library with the prudynt-t project.

## Overview

OpenIMP provides a stub implementation of the Ingenic Media Platform (IMP) libraries. While it doesn't provide actual hardware functionality, it allows you to:

1. **Compile prudynt-t** without the proprietary IMP libraries
2. **Test API usage** and verify function calls
3. **Debug application logic** without hardware
4. **Develop on x86/ARM** systems without Ingenic hardware

## Build OpenIMP

### For Native Development (x86_64)

```bash
cd /home/matteius/openimp
make clean
make
```

### For Cross-Compilation (MIPS)

```bash
cd /home/matteius/openimp
make clean
make CC=mipsel-linux-gnu-gcc PLATFORM=T31
```

This produces:
- `lib/libimp.so`
- `lib/libimp.a`
- `lib/libsysutils.so`
- `lib/libsysutils.a`

## Install OpenIMP

### Option 1: System-wide Installation

```bash
sudo make install
```

This installs to `/usr/local/lib` and `/usr/local/include`.

### Option 2: Custom Location

```bash
make install PREFIX=$HOME/.local
```

### Option 3: Use Directly from Build Directory

You can link directly against the libraries in the `lib/` directory without installing.

## Build prudynt-t Against OpenIMP

### Method 1: Using Installed Libraries

If you installed OpenIMP to a standard location:

```bash
cd /home/matteius/openimp/prudynt-t
make clean
make
```

The build system should automatically find the libraries in `/usr/local`.

### Method 2: Using Custom Install Location

```bash
cd /home/matteius/openimp/prudynt-t
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig
export LD_LIBRARY_PATH=$HOME/.local/lib
make clean
make
```

### Method 3: Direct Linking

Modify prudynt-t's Makefile to point to OpenIMP:

```makefile
IMP_INC = /home/matteius/openimp/include
IMP_LIB = /home/matteius/openimp/lib

CFLAGS += -I$(IMP_INC)
LDFLAGS += -L$(IMP_LIB) -limp -lsysutils
```

## Running prudynt-t with OpenIMP

### Set Library Path

```bash
export LD_LIBRARY_PATH=/home/matteius/openimp/lib:$LD_LIBRARY_PATH
```

### Run the Application

```bash
cd /home/matteius/openimp/prudynt-t
./prudynt
```

### Expected Behavior

Since OpenIMP is a stub implementation, you'll see:

1. **Successful startup** - All IMP initialization calls succeed
2. **Debug logging** - OpenIMP logs all function calls to stderr
3. **No video/audio output** - Hardware interaction is stubbed
4. **No crashes** - All API calls return success or safe error codes

Example output:
```
[IMP_System] Initialized (stub implementation)
[IMP_ISP] Open
[IMP_ISP] AddSensor: jxf23
[IMP_ISP] EnableSensor
[IMP_FrameSource] CreateChn: chn=0, 1920x1080, fmt=10
[IMP_Encoder] CreateGroup: grp=0
[IMP_Encoder] CreateChn: chn=0
...
```

## Debugging with OpenIMP

### Enable Verbose Logging

OpenIMP logs all function calls to stderr. Redirect to a file:

```bash
./prudynt 2> imp_calls.log
```

### Analyze Function Call Sequence

The log shows the exact sequence of IMP calls:

```
[IMP_System] Initialized (stub implementation)
[IMP_System] GetVersion: IMP-1.1.6
[IMP_ISP] Open
[IMP_ISP] AddSensor: jxf23
[IMP_ISP] EnableSensor
[IMP_ISP] EnableTuning
[IMP_ISP] SetSensorFPS: 25/1
[IMP_FrameSource] CreateChn: chn=0, 1920x1080, fmt=10
[IMP_FrameSource] EnableChn: chn=0
[IMP_Encoder] CreateGroup: grp=0
[IMP_Encoder] SetDefaultParam: 1920x1080, 25/1 fps, profile=1, rc=1
[IMP_Encoder] CreateChn: chn=0
[IMP_Encoder] RegisterChn: grp=0, chn=0
[IMP_System] Bind: [0,0,0] -> [1,0,0]
[IMP_Encoder] StartRecvPic: chn=0
```

This helps you understand:
- Initialization order
- Parameter values
- Module binding relationships
- Error conditions

### Verify API Usage

Check that prudynt-t is using the API correctly:

1. **Initialization order**: System → ISP → FrameSource → Encoder
2. **Binding**: Proper cell connections
3. **Parameter validation**: Correct values passed to functions
4. **Cleanup**: Proper shutdown sequence

## Limitations

### What Works

- ✅ All API functions are callable
- ✅ Proper return values (success/failure)
- ✅ Parameter validation
- ✅ Thread-safe operations
- ✅ Timestamp functions
- ✅ Version reporting

### What Doesn't Work

- ❌ No actual video capture
- ❌ No actual video encoding
- ❌ No actual audio capture/playback
- ❌ No actual OSD rendering
- ❌ GetStream() returns "no data"
- ❌ GetFrame() returns "no data"
- ❌ PollingStream() returns timeout

### Workarounds

For testing video pipeline without hardware:

1. **Mock video source**: Modify OpenIMP to generate test patterns
2. **File-based input**: Read from video files instead of ISP
3. **Software encoding**: Use libx264/libx265 instead of hardware encoder

## Transitioning to Real Hardware

When you're ready to use real hardware:

1. **Replace libraries**: Use Ingenic's official libimp.so
2. **No code changes needed**: prudynt-t code remains the same
3. **Update LD_LIBRARY_PATH**: Point to official libraries

```bash
# Development (OpenIMP)
export LD_LIBRARY_PATH=/home/matteius/openimp/lib

# Production (Ingenic IMP)
export LD_LIBRARY_PATH=/usr/lib
```

## Troubleshooting

### Compilation Errors

**Problem**: Undefined references to IMP functions

**Solution**: Make sure you're linking against both libraries:
```bash
-limp -lsysutils
```

**Problem**: Header files not found

**Solution**: Add include path:
```bash
-I/home/matteius/openimp/include
```

### Runtime Errors

**Problem**: Library not found

**Solution**: Set LD_LIBRARY_PATH:
```bash
export LD_LIBRARY_PATH=/home/matteius/openimp/lib:$LD_LIBRARY_PATH
```

**Problem**: Wrong library version loaded

**Solution**: Check which library is being used:
```bash
ldd ./prudynt | grep libimp
```

Should show:
```
libimp.so => /home/matteius/openimp/lib/libimp.so
```

### Application Hangs

**Problem**: Application waits forever for video data

**Solution**: OpenIMP's GetStream/GetFrame functions return "no data available" immediately. If prudynt-t hangs, it may be in a blocking wait loop. Check the polling timeout values.

## Advanced: Extending OpenIMP

If you want to add functionality to OpenIMP:

### 1. Add Test Pattern Generation

Edit `src/imp_encoder.c`:

```c
int IMP_Encoder_GetStream(int encChn, IMPEncoderStream *stream, int block) {
    // Generate a dummy H264 frame
    static uint8_t dummy_frame[1024] = {0x00, 0x00, 0x00, 0x01, ...};
    
    stream->pack[0].virAddr = dummy_frame;
    stream->pack[0].length = sizeof(dummy_frame);
    stream->packCount = 1;
    
    return 0;
}
```

### 2. Add File-based Video Source

Edit `src/imp_framesource.c` to read from video files.

### 3. Add Software Encoding

Integrate libx264 or libx265 for actual encoding.

## Example: Complete Build and Run

```bash
# 1. Build OpenIMP
cd /home/matteius/openimp
make clean
make PLATFORM=T31

# 2. Install OpenIMP
make install PREFIX=$HOME/.local

# 3. Build prudynt-t
cd /home/matteius/openimp/prudynt-t
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig
export LD_LIBRARY_PATH=$HOME/.local/lib
make clean
make

# 4. Run prudynt-t
./prudynt 2>&1 | tee imp_debug.log

# 5. Analyze the log
grep "IMP_" imp_debug.log | head -20
```

## Support

For issues with:
- **OpenIMP**: Check BINARY_ANALYSIS.md and STATUS.md
- **prudynt-t**: See https://github.com/gtxaspec/prudynt-t
- **Integration**: Review this document and the test output

## Next Steps

1. Build OpenIMP for your target platform
2. Compile prudynt-t against OpenIMP
3. Analyze the function call sequence
4. Verify API usage is correct
5. When ready, switch to real IMP libraries for hardware testing

