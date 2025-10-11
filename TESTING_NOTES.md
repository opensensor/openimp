# OpenIMP Testing Notes

## Build Status

✅ **Successfully cross-compiles for MIPS T31**
- Library size: 139KB (libimp.a), 154KB (libimp.so unstripped), 136KB (stripped)
- Total code: 6,038+ lines
- All compilation warnings are expected (unused parameters in stubs, array bounds in codec)

## Current Runtime Issue

### Error Message
```
[   55.572304] Frame channel0 is slake now, Please activate it firstly!
```

### Analysis

The kernel driver (`/dev/framechan0`) is reporting that the channel is "slake" (inactive/slack) and needs to be activated before use.

**What we're doing:**
1. Opening `/dev/framechan0` with `O_RDWR | O_NONBLOCK`
2. Setting format via `ioctl(fd, VIDIOC_SET_FMT, &fmt)` (0xc07056c3)
3. Setting buffer count via `ioctl(fd, VIDIOC_SET_BUFCNT, &count)` (0xc0145608)
4. Starting stream via `ioctl(fd, VIDIOC_STREAM_ON, &enable)` (0x80045612)

**What might be missing:**
- The kernel driver may require an additional "activation" ioctl before STREAM_ON
- There may be a dependency on ISP (Image Signal Processor) being initialized first
- The channel may need to be bound to an ISP device before activation

### Decompilation Evidence

From `IMP_FrameSource_EnableChn` at 0x9ecf8, the sequence is:
1. Open device: `open("/dev/framechan%d", O_RDWR | O_NONBLOCK)`
2. Get format (if channel 0 and NV12): `ioctl(fd, 0x407056c4, &fmt)` - GET_FMT
3. Set format: `ioctl(fd, 0xc07056c3, &fmt)` - SET_FMT
4. Create VBM pool
5. Set buffer count: `ioctl(fd, 0xc0145608, &count)` - SET_BUFCNT
6. Set depth (if fifo): `ioctl(fd, 0x800456c5, &depth)` - SET_DEPTH
7. Fill VBM pool
8. Create capture thread
9. **Stream on**: `ioctl(fd, 0x80045612, &enable)` - STREAM_ON

### Possible Solutions

1. **ISP Initialization**: The ISP module may need to be initialized and a sensor added before frame channels can be activated:
   ```c
   IMP_ISP_Open();
   IMP_ISP_AddSensor(&sensor_info);
   IMP_ISP_EnableSensor();
   ```

2. **Missing ioctl**: There may be an activation ioctl we haven't discovered yet. Need to:
   - Check if there's a VIDIOC_ACTIVATE or similar command
   - Look for ioctls called between SET_BUFCNT and STREAM_ON

3. **Binding**: The frame channel may need to be bound to an ISP device first

4. **Kernel Driver Version**: The stock kernel driver may expect a specific initialization sequence that differs from what we've implemented

## Testing Recommendations

### Test 1: Add ISP Initialization
Before calling `IMP_FrameSource_EnableChn`, ensure:
```c
IMP_ISP_Open();
// Add sensor configuration
IMP_ISP_EnableSensor();
```

### Test 2: Check for Missing ioctls
Use `strace` on the stock libimp.so to see the exact ioctl sequence:
```bash
strace -e ioctl prudynt 2>&1 | grep framechan
```

### Test 3: Kernel Driver Analysis
Check the kernel driver source (if available) for the "slake" error message and what conditions trigger it.

### Test 4: Minimal Test Program
Create a minimal test that just:
1. Opens `/dev/framechan0`
2. Calls each ioctl in sequence
3. Logs the return values

## Implementation Status

### Fully Implemented Modules ✅
- **System**: Module registry, binding/unbinding, observer pattern
- **Encoder**: Channel management, codec integration, rate control, threading
- **FrameSource**: Device management, VBM pools, frame capture threads
- **OSD**: Group/region management, registration, start/stop
- **Audio**: Device initialization, threading, channel management
- **DMA**: Buffer allocation, physical memory mapping
- **Hardware Encoder**: Integration with `/dev/jz-venc`
- **VBM**: Frame queue management, buffer lifecycle

### Stub Modules (Logging Only) ⚠️
- **ISP**: All functions are stubs that just log and return success
- **IVS**: All functions are stubs that just log and return success
- **Audio Encoder/Decoder**: Stub implementations

### Known Limitations

1. **ISP Module**: Currently just stubs. May need real implementation for frame channels to work
2. **Audio Buffers**: Channel enable/disable don't allocate actual audio buffers
3. **OSD Rendering**: Region data isn't actually rendered to frames
4. **IVS Processing**: No actual intelligent video analysis

## Next Steps

1. **Immediate**: Investigate ISP initialization requirements
2. **Short-term**: Implement ISP device opening and sensor management
3. **Medium-term**: Add proper error handling for kernel driver failures
4. **Long-term**: Implement full ISP tuning and control

## Hardware Integration Status

### Working ✅
- DMA allocation via `/dev/jz-dma`
- Memory mapping via `/dev/mem`
- Device file opening (`/dev/framechan*`)

### Untested ⚠️
- Hardware encoder (`/dev/jz-venc`)
- Audio device (`/dev/dsp`)
- ISP device (if exists)

### Not Working ❌
- Frame channel activation (current issue)
- Frame capture from `/dev/framechan0`

## Build Commands

### Cross-compile for T31:
```bash
./build-for-device.sh
```

### Manual build:
```bash
export CROSS_COMPILE=mipsel-linux-
export PATH=/home/matteius/output/wyze_cam3_t31x_gc2053_rtl8189ftv/per-package/toolchain-external-custom/host/bin/:$PATH
make clean
make CROSS_COMPILE=mipsel-linux- PLATFORM=T31
make CROSS_COMPILE=mipsel-linux- strip
```

### Deploy to device:
```bash
scp lib/libimp.so root@device:/usr/lib/
scp lib/libsysutils.so root@device:/usr/lib/
```

## Debugging Tips

1. **Enable verbose logging**: The library uses stderr for logging. Redirect to see all messages:
   ```bash
   prudynt 2>&1 | tee openimp.log
   ```

2. **Check kernel messages**: Use `dmesg` to see kernel driver errors:
   ```bash
   dmesg | tail -50
   ```

3. **Compare with stock**: Run with stock libimp.so and compare behavior:
   ```bash
   LD_LIBRARY_PATH=/path/to/stock prudynt
   ```

4. **Trace system calls**: Use `strace` to see all system calls:
   ```bash
   strace -f prudynt 2>&1 | grep -E "open|ioctl|mmap"
   ```

## References

- Binary Ninja MCP server: `/home/matteius/ingenic-lib-new/T31/lib/1.1.6/uclibc/5.4.0/libimp.so`
- Decompilation addresses documented in source code comments
- Kernel driver: Part of stock firmware (proprietary)

