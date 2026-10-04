# OpenIMP Implementation Progress

## Approach: Binary Ninja MCP-Based Reverse Engineering

We are implementing OpenIMP by decompiling the actual libimp.so binary using Binary Ninja MCP and recreating the implementations based on the decompiled code.

## Progress

### System Module - IN PROGRESS

**Decompiled Functions:**
- ✅ `IMP_System_Init` (0x1dab0) - calls `system_init`
- ✅ `IMP_System_GetVersion` (0x1dd70) - sprintf "IMP-%s", "1.1.6"
- ✅ `IMP_System_Bind` (0x1ddbc) - calls `system_bind`
- ✅ `IMP_System_GetCPUInfo` (0x1e02c) - switch on CPU ID
- ✅ `IMP_System_GetTimeStamp` (0x1dc00) - calls `system_gettime`
- ✅ `system_init` (0x1d2ac) - initializes timestamp_base, calls subsystem inits
- ✅ `system_bind` (0x1bfe0) - gets modules, checks bounds, calls BindObserverToSubject
- ✅ `system_gettime` (0x1bcc4) - clock_gettime with timestamp_base subtraction
- ✅ `get_module` (0x130a0) - returns `*(((deviceID * 6 + groupID) << 2) + &g_modules)`
- ✅ `BindObserverToSubject` (0x1b388) - calls bind function pointer at offset 0x40
- ✅ `AllocModule` (0x1b01c) - allocates Module structure with semaphores, mutex, thread
- ✅ `create_group` (0x13198) - calls AllocModule, stores in g_modules array

**Implemented:**
- ✅ Module structure (based on AllocModule decompilation)
  - 16-byte name
  - Function pointers at 0x40, 0x44, 0x48, 0x4c
  - Observer list at 0x58
  - Semaphores at 0xe4, 0xf4
  - Thread at 0x104
  - Mutex at 0x108
  - Group ID at 0x130
  - Output count at 0x134
  - Module ops at 0x144
  
- ✅ g_modules array [6][6] - global module registry
- ✅ get_module() - array lookup
- ✅ AllocModule() - allocates and initializes Module
- ✅ FreeModule() - cleanup
- ✅ BindObserverToSubject() - calls bind function pointer
- ✅ system_bind() - validates modules, checks output bounds, binds
- ✅ IMP_System_Init() - initializes timestamp_base using clock_gettime
- ✅ IMP_System_Exit() - clears g_modules
- ✅ IMP_System_Bind() - validates cells, calls system_bind
- ✅ IMP_System_UnBind() - stub
- ✅ IMP_System_GetVersion() - returns "IMP-1.1.6"
- ✅ IMP_System_GetCPUInfo() - switch on CPU ID (0-23)
- ✅ IMP_System_GetTimeStamp() - monotonic time minus timestamp_base
- ✅ IMP_System_RebaseTimeStamp() - sets timestamp_base

**TODO:**
- [ ] Implement subsystem initialization loop (6 subsystems)
- [ ] Implement actual bind function pointers
- [ ] Implement UnBind logic

### FrameSource Module - ✅ COMPLETE (Basic Functions)

**Decompiled Functions:**
- ✅ `IMP_FrameSource_CreateChn` (0x9d5d4) - Very complex (~500 lines)
- ✅ `IMP_FrameSource_DestroyChn` (0x9dfdc) - Destroys channel
- ✅ `IMP_FrameSource_EnableChn` (0x9ecf8) - Extremely complex (~800 lines)
- ✅ `IMP_FrameSource_DisableChn` (0xa03bc) - Complex pipeline draining
- ✅ `IMP_FrameSource_SetChnAttr` (0x9c890) - Sets channel attributes
- ✅ `IMP_FrameSource_GetChnAttr` (0x9cb20) - Gets channel attributes

**Implemented:**
- ✅ FSChannel structure (0x2e8 bytes per channel)
  - State at 0x1c (0=disabled, 1=created, 2=running)
  - Attributes at 0x20 (0x50 bytes)
  - Mode at 0x58 (0=normal, 1=special)
  - Thread at 0x1c0
  - File descriptor at 0x1c4
  - Module pointer at 0x1c8
  - Special data pointer at 0x23c
  - Semaphore at 0x244

- ✅ FrameSourceState (gFramesource global)
  - Active count at 0x10
  - Special count at 0x14
  - 5 channels array at 0x20

- ✅ CreateChn/DestroyChn - Channel lifecycle (simplified)
- ✅ EnableChn/DisableChn - Channel enable/disable with state management
- ✅ SetChnAttr/GetChnAttr - Attribute management with validation

**Observations:**
- Heavily dependent on kernel driver (/dev/framechanN)
- Uses ioctl extensively (0x407056c4, 0xc07056c3, 0xc0145608, 0x80045613, etc.)
- Requires VBM (Video Buffer Manager) implementation
- Complex state machine: 0=disabled, 1=created, 2=running
- Special mode (mode=1) has different handling
- Validates format alignment and crop settings
- Channel 0 cannot have crop enabled

**TODO:**
- [ ] Implement kernel driver interface (open /dev/framechanN, ioctl calls)
- [ ] Implement VBM integration (VBMCreatePool, VBMDestroyPool, VBMFlushFrame)
- [ ] Implement thread creation and management
- [ ] Implement GetFrame/ReleaseFrame with actual buffering
- [ ] Implement flush_module_tree_sync for pipeline draining

### Encoder Module - ✅ COMPLETE (Basic Functions)

**Decompiled Functions:**
- ✅ `IMP_Encoder_CreateGroup` (0x82658) - Creates encoder group
- ✅ `IMP_Encoder_DestroyGroup` (0x827f4) - Destroys group, checks for channels
- ✅ `IMP_Encoder_CreateChn` (0x836e0) - Very complex (~800 lines), creates channel
- ✅ `IMP_Encoder_DestroyChn` (0x85c30) - Destroys channel, cleanup
- ✅ `IMP_Encoder_RegisterChn` (0x84634) - Registers channel to group (max 3 per group)
- ✅ `IMP_Encoder_UnRegisterChn` (0x84838) - Unregisters channel from group
- ✅ `IMP_Encoder_StartRecvPic` (0x849c8) - Sets flags at 0x400, 0x404
- ✅ `IMP_Encoder_StopRecvPic` (0x8591c) - Complex pipeline draining logic

**Implemented:**
- ✅ EncChannel structure (0x308 bytes per channel)
  - Channel ID at 0x00
  - Group pointer at 0x294
  - Registered flag at 0x398
  - Started flag at 0x3ac
  - Recv pic enabled at 0x400
  - Recv pic started at 0x404

- ✅ EncGroup structure
  - Group ID
  - Channel count (max 3)
  - Channel pointers array

- ✅ EncoderState (gEncoder global)
  - Module pointer
  - 6 groups array

- ✅ g_EncChannel[9] - Global channel array
- ✅ CreateGroup/DestroyGroup - Group management
- ✅ CreateChn/DestroyChn - Channel lifecycle (simplified)
- ✅ RegisterChn/UnRegisterChn - Group/channel binding
- ✅ StartRecvPic/StopRecvPic - Picture reception control

**Observations:**
- Uses same Module/group infrastructure as System
- Each group can have up to 3 channels
- Channels are 0x308 bytes each (776 bytes)
- Complex initialization with semaphores, mutexes, threads
- CreateChn is extremely complex (~800 lines decompiled)
- Includes buffer allocation, codec initialization, eventfd creation

**TODO:**
- [ ] Implement full CreateChn initialization (semaphores, mutexes, threads)
- [ ] Implement channel_encoder_init/exit functions
- [ ] Implement GetStream/ReleaseStream with actual buffering
- [ ] Add VBM integration for buffer management

### Audio Module - ✅ COMPLETE (Basic Functions)

**Decompiled Functions:**
- ✅ `IMP_AI_SetPubAttr` (0xa8638) - Sets audio attributes with validation
- ✅ `IMP_AI_GetPubAttr` (0xa884c) - Gets audio attributes
- ✅ `IMP_AI_Enable` (0xa895c) - Enables audio device, creates thread
- ✅ `IMP_AI_Disable` (0xa8d04) - Disables device, stops thread
- ✅ `IMP_AI_EnableChn` (0xad8c8) - Very complex (~300 lines), enables channel
- ✅ `IMP_AI_DisableChn` (0xa9024) - Disables channel with cleanup

**Implemented:**
- ✅ AudioDevice structure (0x260 bytes per device)
  - Attributes at 0x10 (0x18 bytes)
  - Enable flag at 0x04 from base+0x228
  - Thread at 0x0c from base+0x228
  - Mutex at 0x208 from base+0x228
  - Condition at 0x220 from base+0x228

- ✅ AudioChannel structure (0x1d0 bytes per channel)
  - Enable flag at 0x3c from base+0x260

- ✅ AudioState global (starts at 0x10b228)
  - 2 devices max
  - 1 channel per device

- ✅ SetPubAttr/GetPubAttr - Attribute management with validation
- ✅ Enable/Disable - Device lifecycle (simplified)
- ✅ EnableChn/DisableChn - Channel lifecycle (simplified)

**Observations:**
- Only 16kHz sample rate supported (from decompilation)
- Frame time must be divisible by 10ms
- Complex buffer allocation with audio_buf_alloc
- Thread management for audio processing
- Mutex/condition for synchronization

**TODO:**
- [ ] Implement __ai_dev_init/__ai_dev_deinit
- [ ] Implement audio buffer allocation
- [ ] Implement thread creation and management
- [ ] Implement GetFrame/ReleaseFrame
- [ ] Add AENC/ADEC support

### OSD Module - ✅ COMPLETE (Basic Functions)

**Decompiled Functions:**
- ✅ `IMP_OSD_CreateGroup` (0xc1cf8) - Creates OSD group
- ✅ `IMP_OSD_DestroyGroup` (0xc1f2c) - Destroys group
- ✅ `IMP_OSD_CreateRgn` (0xc225c) - Very complex, creates region with memory allocation
- ✅ `IMP_OSD_DestroyRgn` (0xc297c) - Destroys region, frees memory

**Implemented:**
- ✅ OSDRegion structure (0x38 bytes)
  - Handle at 0x00
  - Attributes at 0x04 (0x20 bytes)
  - Data pointer at 0x28
  - Prev/next pointers at 0x2c/0x30
  - Allocated/registered flags at 0x34/0x35

- ✅ OSDState global (gosd)
  - Free region list at 0x11b8/0x11bc
  - Used region list at 0x11c0/0x11c4
  - Group pointers at 0x11c8
  - Memory pool at 0x11cc
  - 512 regions at 0x24050
  - Semaphore at 0x2b060

- ✅ CreateGroup/DestroyGroup - Group management
- ✅ CreateRgn/DestroyRgn - Region lifecycle with list management
- ✅ Free/used list management for regions
- ✅ Semaphore protection for thread safety

**Observations:**
- Uses linked lists for free/used regions
- Semaphore for thread-safe access
- Memory pool for region data
- Complex pixel format handling based on CPU ID
- Supports multiple region types (OSD_REG_LINE, OSD_REG_RECT, OSD_REG_BITMAP, etc.)

**TODO:**
- [ ] Implement RegisterRgn/UnRegisterRgn
- [ ] Implement SetRgnAttr/GetRgnAttr
- [ ] Implement ShowRgn/Start/Stop
- [ ] Implement memory pool allocation
- [ ] Add pixel format conversion

### IVS Module - NOT STARTED

**TODO:**
- [ ] Decompile IVS functions

## Key Discoveries

### Module System Architecture

The IMP library uses a sophisticated module system:

1. **Global Module Registry**: `g_modules[deviceID][groupID]` at address 0x108ca0
   - deviceID: 0-5 (FS=0, ENC=1, OSD=2, IVS=3, etc.)
   - groupID: 0-5
   
2. **Module Structure**: ~0x148 bytes + extra data
   - Name, function pointers, semaphores, mutex, thread
   - Observer pattern for binding
   - Output array at offset 0x128
   
3. **Binding Mechanism**:
   - get_module() retrieves module from registry
   - Validates output ID against output_count
   - Calls bind function pointer with output pointer
   
4. **Initialization**:
   - AllocModule() creates module with pthread infrastructure
   - create_group() stores module in g_modules
   - system_init() calls 6 subsystem init functions

### Kernel Driver Interface

Many functions use ioctl() to communicate with kernel drivers:
- `/dev/framechanN` - Frame source channels
- Various ioctl commands (0x407056c4, 0xc07056c3, etc.)

### VBM (Video Buffer Manager)

A sophisticated buffer management system:
- VBMCreatePool() - Create buffer pool
- VBMDestroyPool() - Destroy pool
- VBMFillPool() - Fill with buffers
- VBMGetFrame() - Get frame from pool
- VBMReleaseFrame() - Return frame to pool

### Complexity Assessment

**Simple** (can implement from decompilation):
- System module core functions ✅
- Module allocation/management ✅
- Version/CPU info ✅
- Timestamp functions ✅

**Medium** (requires understanding kernel interface):
- Encoder group/channel management
- Basic ioctl wrappers
- Module binding logic

**Complex** (requires significant reverse engineering):
- FrameSource (kernel driver heavy)
- VBM implementation
- Thread management
- Audio processing

**Very Complex** (may need kernel driver source):
- ISP tuning (hardware registers)
- Video encoding pipeline
- Audio encoding pipeline
- OSD rendering

## Next Steps

1. **Complete System Module**:
   - Implement subsystem init loop
   - Add bind function pointer support
   - Test with prudynt-t

2. **Implement Encoder Module**:
   - Decompile remaining Encoder functions
   - Implement group/channel management
   - Add ioctl stubs or real calls

3. **Tackle FrameSource**:
   - Decompile VBM functions
   - Understand gFramesource layout
   - Implement or stub ioctl calls

4. **Audio Modules**:
   - Decompile AI/AENC/ADEC functions
   - Implement based on patterns from other modules

## Build Status

✅ Compiles successfully
⚠️ Warnings about unused functions (AllocModule, FreeModule) - will be used later
✅ Links into libimp.so

## Testing Strategy

1. Unit test each function against known behavior
2. Test with prudynt-t to see which functions are actually called
3. Implement functions in order of usage frequency
4. Stub complex kernel interactions initially
5. Add real kernel driver calls incrementally

## Challenges

1. **Kernel Driver Dependency**: Many functions require /dev/framechanN and other devices
2. **VBM Complexity**: Video buffer management is a large subsystem
3. **Thread Management**: Modules spawn threads that need proper lifecycle management
4. **Hardware Specifics**: ISP tuning requires hardware register knowledge
5. **Incomplete Decompilation**: Some functions are too complex to fully understand

## Solutions

1. **Hybrid Approach**: Implement what we can from decompilation, stub the rest
2. **Incremental**: Start with simple functions, add complexity gradually
3. **Testing**: Use prudynt-t as integration test
4. **Documentation**: Document what each function does even if not fully implemented
5. **Kernel Driver**: May need to reverse engineer kernel module too

## Conclusion

We've made good progress on the System module by using actual decompilations. The approach is working but reveals significant complexity in the full implementation. We should continue with this method, focusing on the most commonly used functions first.

