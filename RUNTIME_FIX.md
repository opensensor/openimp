# OpenIMP Runtime Fix - ISP Implementation

## Problem Identified

The runtime error was:
```
[KernelIF] Failed to open /dev/framechan0: Operation not permitted
Frame channel0 is slake now, Please activate it firstly!
```

**Root Cause**: The ISP (Image Signal Processor) module was just a stub. The frame channels require the ISP device (`/dev/tx-isp`) to be opened and the sensor to be enabled before they can be accessed.

## Solution Implemented

### 1. ISP Device Structure
Created proper ISP device structure (0xe0 bytes) based on decompilation at 0x8b6ec:
```c
typedef struct {
    char dev_name[32];      /* 0x00: Device name "/dev/tx-isp" */
    int fd;                 /* 0x20: File descriptor */
    int opened;             /* 0x24: Opened flag */
    uint8_t data[0xb8];     /* 0x28-0xdf: Rest of data */
} ISPDevice;
```

### 2. IMP_ISP_Open Implementation
**Decompilation**: 0x8b6ec

**What it does**:
1. Allocates ISP device structure (0xe0 bytes via calloc)
2. Sets device name to "/dev/tx-isp"
3. Opens `/dev/tx-isp` with O_RDWR | O_NONBLOCK
4. Sets opened flag to 1

**Code**:
```c
gISPdev = (ISPDevice*)calloc(0xe0, 1);
strcpy(gISPdev->dev_name, "/dev/tx-isp");
gISPdev->fd = open(gISPdev->dev_name, O_RDWR | O_NONBLOCK);
gISPdev->opened = 1;
```

### 3. IMP_ISP_AddSensor Implementation
**Decompilation**: 0x8bd6c

**What it does**:
1. Checks ISP is opened and sensor not already enabled
2. Calls ioctl 0x805056c1 to add sensor
3. Copies sensor info to device structure at offset 0x28

**Key ioctl**:
- `0x805056c1` - Add sensor to ISP

### 4. IMP_ISP_EnableSensor Implementation
**Decompilation**: 0x98450

**What it does**:
1. Gets sensor index via ioctl 0x40045626
2. Starts ISP streaming via ioctl 0x80045612 (VIDIOC_STREAM_ON)
3. Enables sensor via ioctl 0x800456d0
4. Final enable via ioctl 0x800456d2
5. Increments opened flag by 2 to mark sensor as enabled

**Key ioctls**:
- `0x40045626` - Get sensor index
- `0x80045612` - Start ISP streaming (VIDIOC_STREAM_ON)
- `0x800456d0` - Enable sensor
- `0x800456d2` - Final sensor enable

### 5. IMP_ISP_Close Implementation
**Decompilation**: 0x8b8d8

**What it does**:
1. Checks sensor is disabled (opened < 2)
2. Closes `/dev/tx-isp` file descriptor
3. Frees ISP device structure

## Initialization Sequence

The correct initialization sequence is now:

```c
// 1. Open ISP device
IMP_ISP_Open();                    // Opens /dev/tx-isp

// 2. Add sensor
IMP_ISP_AddSensor(&sensor_info);   // Registers sensor with ISP

// 3. Enable sensor
IMP_ISP_EnableSensor();            // Activates sensor and starts ISP streaming

// 4. Now frame channels can be opened
IMP_FrameSource_CreateChn(0, &attr);
IMP_FrameSource_EnableChn(0);      // This will now succeed
```

## Expected Behavior

With the ISP properly initialized:
1. `/dev/tx-isp` is opened successfully
2. Sensor is registered and enabled
3. ISP starts streaming
4. Frame channels (`/dev/framechan0`, etc.) become accessible
5. Frame capture can begin

## Testing

Deploy the new library and test:

```bash
# Copy to device
scp lib/libimp.so root@device:/usr/lib/
scp lib/libsysutils.so root@device:/usr/lib/

# Run prudynt
prudynt &

# Check for errors
dmesg | tail -20
```

## Expected Output

You should now see:
```
[IMP_ISP] Open: opened /dev/tx-isp (fd=X)
[IMP_ISP] AddSensor: gc2053
[IMP_ISP] EnableSensor: sensor enabled (idx=0)
[FrameSource] CreateChn: chn=0
[FrameSource] EnableChn: chn=0 enabled successfully
```

Instead of:
```
[KernelIF] Failed to open /dev/framechan0: Operation not permitted
```

## Files Modified

- `src/imp_isp.c`: Implemented real ISP device management
  - IMP_ISP_Open: Opens `/dev/tx-isp`
  - IMP_ISP_Close: Closes ISP device
  - IMP_ISP_AddSensor: Registers sensor via ioctl
  - IMP_ISP_EnableSensor: Enables sensor with 4 ioctls

## Build Status

✅ **Compiles successfully**
- Library size: 145KB (libimp.a), 136KB (libimp.so stripped)
- Total code: 6,100+ lines
- Ready for deployment

## Next Steps

1. **Deploy and test** on device
2. **Verify frame capture** works
3. **Check encoder** receives frames
4. **Test video streaming** through prudynt

## Potential Issues

If frame channels still don't work:
1. Check `/dev/tx-isp` permissions
2. Verify sensor info structure is correct
3. Check if additional ISP configuration is needed
4. Look for other ioctls in the decompilation

## References

- ISP Open: 0x8b6ec
- ISP Close: 0x8b8d8
- ISP AddSensor: 0x8bd6c
- ISP EnableSensor: 0x98450
- Binary: `/home/matteius/ingenic-lib-new/T31/lib/1.1.6/uclibc/5.4.0/libimp.so`

