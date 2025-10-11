# Binary Analysis of libimp.so (T31 v1.1.6)

This document contains findings from reverse engineering the actual libimp.so binary using Binary Ninja.

## Binary Information

- **File**: `/home/matteius/ingenic-lib-new/T31/lib/1.1.6/uclibc/5.4.0/libimp.so`
- **Version**: IMP-1.1.6
- **Architecture**: MIPS32 (uclibc)
- **Platform**: Ingenic T31

## Key Findings

### IMP_System_GetVersion

```c
int IMP_System_GetVersion(IMPVersion *pstVersion) {
    if (pstVersion == NULL)
        return -1;
    
    sprintf(pstVersion->aVersion, "IMP-%s", "1.1.6");
    return 0;
}
```

**Findings**:
- Version string format: "IMP-1.1.6"
- IMPVersion structure has a string field `aVersion`

### IMP_System_Init

```c
int IMP_System_Init(void) {
    imp_log_fun(...);  // Logging
    modify_phyclk_strength();
    return system_init();  // Tail call
}
```

**Findings**:
- Calls internal `system_init()` function
- Performs PHY clock strength modification
- Uses logging system

### IMP_System_Bind

```c
int IMP_System_Bind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL) {
        // Log error
        return -1;
    }
    if (dstCell == NULL) {
        // Log error
        return -1;
    }
    
    return system_bind(srcCell, dstCell);
}
```

**Findings**:
- IMPCell structure accessed as: `cell[0]`, `cell[1]`, `cell[2]`
- These correspond to deviceID, groupID, outputID
- Calls internal `system_bind()` which uses `get_module()` and `BindObserverToSubject()`

### system_bind Implementation

```c
int system_bind(IMPCell *src, IMPCell *dst) {
    void *src_module = get_module(src[0], src[1]);  // deviceID, groupID
    void *dst_module = get_module(dst[0], dst[1]);
    
    if (src_module == NULL) {
        // Log error about source
        return -1;
    }
    if (dst_module == NULL) {
        // Log error about destination
        return -1;
    }
    
    int output_id = src[2];  // outputID
    if (output_id < *(src_module + 0x134)) {  // Check bounds
        BindObserverToSubject(src_module, dst_module, 
                             src_module + 0x128 + ((output_id + 4) << 2));
        return 0;
    }
    
    // Log error about invalid output ID
    return -1;
}
```

**Findings**:
- IMPCell is a 3-element array: `[deviceID, groupID, outputID]`
- Modules are looked up by deviceID and groupID
- Uses observer pattern for binding
- Module structure has output array at offset 0x128
- Module structure has output count at offset 0x134

### IMP_System_GetTimeStamp

```c
uint64_t IMP_System_GetTimeStamp(void) {
    return system_gettime(0);
}
```

### system_gettime Implementation

```c
uint64_t system_gettime(int mode) {
    struct timespec ts;
    int clock_id;
    
    if (mode == 1) {
        clock_id = CLOCK_REALTIME;  // 2
    } else if (mode == 2) {
        clock_id = CLOCK_PROCESS_CPUTIME_ID;  // 3
    } else if (mode == 0) {
        clock_id = CLOCK_MONOTONIC;  // 4
    } else {
        return -1;
    }
    
    if (clock_gettime(clock_id, &ts) >= 0) {
        uint64_t usec = (ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
        return usec - timestamp_base;
    }
    return 0;
}
```

**Findings**:
- Uses POSIX `clock_gettime()` with CLOCK_MONOTONIC
- Returns microseconds
- Subtracts a global `timestamp_base` variable
- timestamp_base is set by `IMP_System_RebaseTimeStamp()`

### IMP_System_GetCPUInfo

Returns a string pointer based on CPU ID:
- 0: "T10"
- 1-2: "T20"
- 3: "T21"
- 4: "T23"
- 5: "T30"
- 6: "T31"
- 7: "T40"
- 8: "T41"
- 9: "C100"
- 10: "T15"
- 11: "T20L"
- 12: "T20X"
- 13: "T21L"
- 14: "T21N"
- 15: "T21Z"
- 16: "T30A"
- 17: "T30L"
- 18: "T30N"
- 19: "T30X"
- 20: "T31A"
- 21: "T31L"
- 22: "T31N"
- 23: "T31X"
- default: "Unknown"

**Findings**:
- CPU ID is read from hardware register
- Returns static string pointer (not allocated)

## Data Structures

### IMPCell

```c
typedef struct {
    int deviceID;       // offset 0x00
    int groupID;        // offset 0x04
    int outputID;       // offset 0x08
} IMPCell;
```

Size: 12 bytes (3 x 4-byte integers)

### IMPVersion

```c
typedef struct {
    char aVersion[64];  // Version string buffer
} IMPVersion;
```

Size: 64 bytes

### Module Structure (Internal)

```c
struct Module {
    // ... unknown fields ...
    void *outputs[N];   // offset 0x128 - array of output pointers
    int output_count;   // offset 0x134 - number of outputs
    // ... more fields ...
};
```

## Memory Management

The library uses several memory allocation strategies:
- **buddy allocator**: For general allocations
- **continuous allocator**: For physically contiguous memory
- **mempool**: For pool-based allocation
- **kmem**: Kernel memory interface
- **pmem**: Physical memory interface

Functions observed:
- `IMP_Alloc()` - General allocation
- `IMP_Sp_Alloc()` - Special allocation
- `IMP_Free()` - Free memory
- `IMP_Virt_to_Phys()` - Virtual to physical address
- `IMP_Phys_to_Virt()` - Physical to virtual address
- `IMP_FlushCache()` - Cache flush
- `IMP_MemPool_*()` - Memory pool operations
- `IMP_PoolAlloc()` - Pool allocation
- `IMP_PoolFree()` - Pool free

## Observer Pattern

The library uses an observer/subject pattern for module binding:
- `BindObserverToSubject()` - Bind modules together
- `UnBindObserverFromSubject()` - Unbind modules
- `notify_observers()` - Notify all observers
- `add_observer()` - Add observer to list
- `remove_observer()` - Remove observer from list

## Module Management

Internal functions for module management:
- `get_module(deviceID, groupID)` - Get module by ID
- `get_module_location()` - Get module location
- `AllocModule()` - Allocate module
- `FreeModule()` - Free module
- `create_group()` - Create group
- `destroy_group()` - Destroy group
- `alloc_device()` - Allocate device
- `free_device()` - Free device

## Video Buffer Management (VBM)

The library has a sophisticated video buffer manager:
- `VBMCreatePool()` - Create buffer pool
- `VBMDestroyPool()` - Destroy buffer pool
- `VBMGetFrame()` - Get frame from pool
- `VBMReleaseFrame()` - Release frame back to pool
- `VBMLockFrame()` - Lock frame
- `VBMUnLockFrame()` - Unlock frame
- `VBMFlushFrame()` - Flush frame cache
- `VBMDumpPool()` - Dump pool info

## FIFO Implementation

Generic FIFO queue implementation:
- `fifo_alloc()` - Allocate FIFO
- `fifo_free()` - Free FIFO
- `fifo_put()` - Put item in FIFO
- `fifo_get()` - Get item from FIFO
- `fifo_clear()` - Clear FIFO
- `fifo_num()` - Get number of items

## Image Processing

The library includes SIMD-optimized image processing:
- `c_resize_simd_nv12_up()` - Upscale NV12 with SIMD
- `c_resize_simd_nv12_down()` - Downscale NV12 with SIMD
- `nv12_left_rotate_90()` - Rotate NV12 left 90°
- `nv12_right_rotate_90()` - Rotate NV12 right 90°
- `c_copy_frame_*()` - Various format conversion functions
- `is_video_has_simd128_proc()` - Check for SIMD128 support

## Logging System

The library uses a logging system:
- `imp_log_fun()` - Main logging function
- `IMP_Log_Get_Option()` - Get logging options
- Log levels: 3 (info), 6 (error)

## Next Steps for Implementation

Based on this analysis, our stub implementation should:

1. **IMPCell**: Use a simple 3-int structure
2. **Version**: Return "IMP-1.1.6" format
3. **Timestamp**: Use clock_gettime(CLOCK_MONOTONIC) with a base offset
4. **CPU Info**: Return appropriate string based on platform define
5. **Binding**: Implement a simple module registry and observer pattern
6. **Memory**: Provide basic allocation wrappers (can use malloc/mmap initially)
7. **Logging**: Implement basic logging (can use printf initially)

The actual hardware interaction (ISP, encoder, etc.) will need to interface with the kernel driver or be stubbed out for testing.

