# T23 Sensor Structure Size Fix

## Problem

The T23 platform was failing during `IMP_ISP_AddSensor()` with:
```
[IMP_ISP] AddSensor: REGISTER_SENSOR ioctl(0x805456c1) failed for sc2336: Invalid argument
```

## Root Cause

The `IMPSensorInfo` structure size didn't match what the T23 kernel driver expected:

- **Our structure**: 88 bytes (0x58) - included `void *private_data` field
- **T23 kernel expects**: 84 bytes (0x54) - as encoded in ioctl `0x805456c1`
- **T31 kernel expects**: 80 bytes (0x50) - as encoded in ioctl `0x805056c1`

The ioctl number encodes the structure size:
- `0x805456c1` = `_IOW('V', 0xc1, 0x54)` → expects 84 bytes (0x54)
- `0x805056c1` = `_IOW('V', 0xc1, 0x50)` → expects 80 bytes (0x50)

## Structure Layout Analysis

### T31 Structure (80 bytes)
```c
typedef struct {
    char name[32];                      // 0-31   (32 bytes)
    TXSensorControlBusType cbus_type;   // 32-35  (4 bytes)
    TXSNSI2CConfig i2c;                 // 36-63  (28 bytes: 20+4+4)
    int rst_gpio;                       // 64-67  (4 bytes)
    int pwdn_gpio;                      // 68-71  (4 bytes)
    int power_gpio;                     // 72-75  (4 bytes)
    int sensor_id;                      // 76-79  (4 bytes)
    // Total: 80 bytes (no additional fields)
} IMPSensorInfo_T31;
```

### T23 Structure (84 bytes)
```c
typedef struct {
    char name[32];                      // 0-31   (32 bytes)
    TXSensorControlBusType cbus_type;   // 32-35  (4 bytes)
    TXSNSI2CConfig i2c;                 // 36-63  (28 bytes: 20+4+4)
    int rst_gpio;                       // 64-67  (4 bytes)
    int pwdn_gpio;                      // 68-71  (4 bytes)
    int power_gpio;                     // 72-75  (4 bytes)
    int sensor_id;                      // 76-79  (4 bytes)
    int video_interface;                // 80-83  (4 bytes) ← T23-specific field
    // Total: 84 bytes
} IMPSensorInfo_T23;
```

## Solution

Modified `include/imp/imp_common.h` to use platform-specific structure definitions:

```c
typedef struct {
    char name[32];
    TXSensorControlBusType cbus_type;
    TXSNSI2CConfig i2c;
    int rst_gpio;
    int pwdn_gpio;
    int power_gpio;
    int sensor_id;
#if defined(PLATFORM_T23)
    int video_interface;                    // T23: makes struct 84 bytes
#elif defined(PLATFORM_T40) || defined(PLATFORM_T41)
    void *private_data;                     // T40/T41: extended structure
#endif
    // T31/T21/C100: no field here, keeping struct at 80 bytes
} IMPSensorInfo;
```

## Additional T23 Fixes

### 1. Correct REGISTER_SENSOR ioctl
**File**: `src/isp_ioctl_compat.h`

Changed from:
```c
#define TX_ISP_REGISTER_SENSOR    0x805056c1u  // Wrong: T31 ioctl
```

To:
```c
#define TX_ISP_REGISTER_SENSOR    0x805456c1u  // Correct: T23 ioctl
```

### 2. Sensor Binary Loading Order
**File**: `src/imp_isp.c`

The sensor binary must be loaded **BEFORE** enumeration on T23:

```c
// 1. REGISTER_SENSOR ioctl
ioctl(fd, TX_ISP_REGISTER_SENSOR, pinfo);

// 2. Set bin path (CRITICAL: must happen before enumeration)
ioctl(fd, 0xC00456C7, bin_path);

// 3. Enumerate sensors (sensor only appears after bin is loaded)
ioctl(fd, 0xC050561A, &enum_input);

// 4. Set input (stock T23 uses hardcoded value 1)
int input_index = 1;
ioctl(fd, 0xC0045627, &input_index);
```

### 3. Sensor Enumeration Required
T23 **requires** the sensor to be found in enumeration (no fallback):

```c
#ifdef PLATFORM_T23
    if (sensor_idx == -1) {
        LOG_ISP("AddSensor: sensor %s not found in enumeration after loading bin", pinfo->name);
        return -1;  // Fatal error on T23
    }
#endif
```

### 4. Hardcoded SET_INPUT Value
Stock T23 libimp uses hardcoded value `1` for SET_INPUT (not the enumerated index):

```c
#ifdef PLATFORM_T23
    int input_index = 1;  // Stock T23 libimp always uses 1
#else
    int input_index = sensor_idx;
#endif
```

## Platform Summary

| Platform | Structure Size | REGISTER_SENSOR ioctl | Notes |
|----------|---------------|----------------------|-------|
| T31      | 80 bytes (0x50) | 0x805056c1 | No extra fields |
| T23      | 84 bytes (0x54) | 0x805456c1 | Includes `video_interface` |
| T40/T41  | 88 bytes (0x58) | TBD | Includes `private_data` |

## Verification

To verify structure sizes:
```bash
gcc -DPLATFORM_T23 -I./include -c test.c
# Should produce 84-byte structure for T23

gcc -DPLATFORM_T31 -I./include -c test.c
# Should produce 80-byte structure for T31
```

## References

- Stock T23 libimp decompilation (Binary Ninja port_9012)
- T23 kernel driver tx-isp-t23.ko (Binary Ninja port_9013)
- Ioctl encoding: `_IOW('V', cmd, size)` where size is structure size

