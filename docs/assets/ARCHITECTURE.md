# OpenIMP Architecture

This document describes the complete architecture of the OpenIMP library, including data flow, threading model, and module interactions.

## Complete Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Hardware Layer                                     │
│                    Ingenic ISP → Kernel Driver                               │
│                        /dev/framechan%d                                      │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ ioctl(VIDIOC_POLL_FRAME, 0x400456bf)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│              Frame Capture Thread (FrameSource Module)                       │
│                     frame_capture_thread()                                   │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ 1. Poll kernel driver for frame availability                          │  │
│  │ 2. Get frame from VBM pool                                            │  │
│  │ 3. Notify observers (bound modules)                                   │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ VBMGetFrame(chn, &frame)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                         VBM Frame Pool                                       │
│              Physical/Virtual Memory Buffers (DMA)                           │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ - Allocated via IMP_Alloc (kernel driver)                             │  │
│  │ - Mapped via mmap() (physical → virtual)                              │  │
│  │ - VBMPool: 0x180 + (bufcount * 0x428) bytes                           │  │
│  │ - VBMFrame: 0x428 bytes per frame                                     │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ notify_observers(module, frame)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                   Observer Pattern (System Module)                           │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ - Traverse observer list                                              │  │
│  │ - Call update callback (func_4c) for each observer                    │  │
│  │ - Observer structure: next, module, frame, output_index               │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ encoder_update(module)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                 Encoder Thread (Encoder Module)                              │
│                       encoder_thread()                                       │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ 1. Receive frame notification via observer callback                   │  │
│  │ 2. Process frame through codec                                        │  │
│  │ 3. Signal stream thread via eventfd                                   │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ AL_Codec_Encode_Process(codec, frame, NULL)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                       Codec Frame FIFO                                       │
│                  Thread-safe Queue (Fifo)                                    │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ - Fifo structure: 64 bytes                                            │  │
│  │ - Mutex, condition variable, semaphore                                │  │
│  │ - Timeout support (infinite, no-wait, timed)                          │  │
│  │ - Abort flag for graceful shutdown                                    │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ eventfd signal (write)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                 Stream Thread (Encoder Module)                               │
│                       stream_thread()                                        │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ 1. Wait for eventfd signal (select with timeout)                      │  │
│  │ 2. Get encoded stream from codec                                      │  │
│  │ 3. Queue stream for user retrieval                                    │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ AL_Codec_Encode_GetStream(codec, &stream)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                      Codec Stream FIFO                                       │
│                  Thread-safe Queue (Fifo)                                    │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ - Queues encoded H.264/H.265 streams                                  │  │
│  │ - Same Fifo implementation as frame FIFO                              │  │
│  │ - Thread-safe dequeue for IMP_Encoder_GetStream                       │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ IMP_Encoder_GetStream(chn, stream, block)
                                   ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                        User Application                                      │
│                   (prudynt-t, custom apps)                                   │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ 1. Call IMP_Encoder_GetStream() to retrieve encoded stream            │  │
│  │ 2. Process H.264/H.265 stream (RTSP, file, etc.)                      │  │
│  │ 3. Call IMP_Encoder_ReleaseStream() to release buffer                 │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Module Architecture

### System Module (src/imp_system.c)
- **Purpose**: Module registry, binding, and observer pattern
- **Key Structures**:
  - `Module`: 0x148+ bytes with bind/unbind function pointers
  - `Observer`: Linked list for module binding
  - `g_modules[6][6]`: Global module registry
- **Key Functions**:
  - `IMP_System_Init/Exit`: Initialize/cleanup system
  - `IMP_System_Bind/UnBind`: Bind modules together
  - `notify_observers()`: Notify all observers with frame
  - `IMP_System_GetTimeStamp()`: Monotonic timestamp

### FrameSource Module (src/imp_framesource.c)
- **Purpose**: Capture frames from kernel driver
- **Key Structures**:
  - `FSChannel`: 0x2e8 bytes per channel
  - `FrameSourceState`: Global state with 5 channels
- **Key Functions**:
  - `IMP_FrameSource_CreateChn/DestroyChn`: Channel lifecycle
  - `IMP_FrameSource_EnableChn/DisableChn`: Start/stop capture
  - `frame_capture_thread()`: Polls kernel driver for frames
  - `framesource_bind/unbind()`: Module binding
- **Kernel Integration**:
  - Opens `/dev/framechan%d` (0-5)
  - ioctl: VIDIOC_POLL_FRAME (0x400456bf)
  - ioctl: VIDIOC_SET_FMT, VIDIOC_STREAM_ON/OFF

### Encoder Module (src/imp_encoder.c)
- **Purpose**: Encode frames to H.264/H.265
- **Key Structures**:
  - `EncChannel`: 0x308 bytes per channel
  - `EncoderState`: Global state with 9 channels
- **Key Functions**:
  - `IMP_Encoder_CreateChn/DestroyChn`: Channel lifecycle
  - `IMP_Encoder_StartRecvPic/StopRecvPic`: Start/stop encoding
  - `IMP_Encoder_GetStream/ReleaseStream`: Retrieve encoded streams
  - `encoder_thread()`: Processes frames through codec
  - `stream_thread()`: Retrieves encoded streams
  - `encoder_update()`: Observer callback for frame notification
- **Threading**:
  - 2 threads per channel (encoder + stream)
  - eventfd for inter-thread signaling
  - 3 semaphores, 2 mutexes per channel

### VBM (Video Buffer Manager) - src/kernel_interface.c
- **Purpose**: Manage frame buffers with DMA
- **Key Structures**:
  - `VBMPool`: 0x180 + (bufcount * 0x428) bytes
  - `VBMFrame`: 0x428 bytes per frame
  - `VBMVolume`: Global array (30 entries)
- **Key Functions**:
  - `VBMCreatePool()`: Allocate frame buffers
  - `VBMGetFrame/ReleaseFrame()`: Get/release frames
  - `VBMFillPool/FlushFrame()`: Pool management
- **Pixel Formats**: NV12, YU12, RGBP, YUYV, UYVY, BGR3, BGR4, etc.

### Codec (src/codec.c)
- **Purpose**: Codec parameter management and frame/stream queuing
- **Key Structures**:
  - `AL_CodecEncode`: 0x924 bytes (2340 bytes)
  - Dual FIFO system (frames + streams)
- **Key Functions**:
  - `AL_Codec_Encode_Create/Destroy`: Codec lifecycle
  - `AL_Codec_Encode_SetDefaultParam()`: 100+ parameter fields
  - `AL_Codec_Encode_Process()`: Queue frame for encoding
  - `AL_Codec_Encode_GetStream/ReleaseStream()`: Stream retrieval

### DMA Allocator (src/dma_alloc.c)
- **Purpose**: Physical memory allocation via kernel driver
- **Key Structures**:
  - `DMABuffer`: 0x94 bytes with virt/phys addresses
- **Key Functions**:
  - `IMP_Alloc/IMP_PoolAlloc()`: Allocate DMA buffer
  - `IMP_Free()`: Free DMA buffer
  - `IMP_Flush_Cache()`: Cache flush
- **Kernel Integration**:
  - Opens `/dev/mem` or `/dev/jz-dma`
  - ioctl: IOCTL_MEM_ALLOC, IOCTL_MEM_FREE, IOCTL_MEM_FLUSH
  - mmap() for physical-to-virtual mapping
  - Fallback to posix_memalign() if kernel unavailable

### Fifo (src/fifo.c)
- **Purpose**: Thread-safe FIFO queue
- **Key Structures**:
  - `Fifo`: 64 bytes with mutex, cond var, semaphore
- **Key Functions**:
  - `Fifo_Init/Deinit()`: Initialize/cleanup
  - `Fifo_Queue/Dequeue()`: Thread-safe operations
  - Timeout support: infinite (-1), no-wait (0), timed (ms)

## Threading Model

### Thread Types

1. **Frame Capture Thread** (FrameSource):
   - One per FrameSource channel
   - Created in `IMP_FrameSource_EnableChn()`
   - Polls kernel driver for frames
   - Notifies observers when frame available
   - Cancelled in `IMP_FrameSource_DisableChn()`

2. **Encoder Thread** (Encoder):
   - One per Encoder channel
   - Created in `IMP_Encoder_CreateChn()`
   - Receives frames via observer callback
   - Processes frames through codec
   - Signals stream thread via eventfd
   - Cancelled in `IMP_Encoder_DestroyChn()`

3. **Stream Thread** (Encoder):
   - One per Encoder channel
   - Created in `IMP_Encoder_CreateChn()`
   - Waits for eventfd signal
   - Retrieves encoded streams from codec
   - Queues streams for user retrieval
   - Cancelled in `IMP_Encoder_DestroyChn()`

### Synchronization Primitives

- **Mutexes**: Protect shared data structures
- **Semaphores**: Resource counting and signaling
- **Condition Variables**: Thread waiting and notification
- **eventfd**: Inter-thread signaling (encoder → stream)
- **select()**: Timeout-based waiting on eventfd

## Memory Management

### DMA Memory Flow

```
User calls IMP_Alloc(name, size, tag)
    ↓
Open /dev/mem or /dev/jz-dma
    ↓
ioctl(IOCTL_MEM_ALLOC) → Get physical address
    ↓
mmap(phys_addr) → Map to virtual address
    ↓
Return DMABuffer with virt/phys addresses
    ↓
Used by VBM for frame buffers
    ↓
User calls IMP_Free(phys_addr)
    ↓
munmap() and ioctl(IOCTL_MEM_FREE)
```

### Safe Struct Member Access

All struct member access uses one of:
1. **Typed access**: `struct->member`
2. **memcpy()**: `memcpy(&value, ptr + offset, sizeof(value))`
3. **Never**: Direct pointer arithmetic like `*(uint32_t*)(ptr + offset)`

## Module Binding (Observer Pattern)

### Binding Flow

```
User calls IMP_System_Bind(&srcCell, &dstCell)
    ↓
system_bind() looks up modules in g_modules[]
    ↓
BindObserverToSubject(src_module, dst_module, output_ptr)
    ↓
Calls src_module->bind_func(src, dst, output_ptr)
    ↓
Creates Observer structure
    ↓
Adds to src_module->observer_list
    ↓
When frame available: notify_observers(src_module, frame)
    ↓
For each observer: call dst_module->func_4c(dst_module)
    ↓
Encoder receives frame notification
```

## Build System

- **Makefile**: Builds libimp.a (static) and libimp.so (shared)
- **Source Files**: 11 modules (4,434 lines)
- **Header Files**: Public API in include/imp/
- **Platform**: T31 (configurable via PLATFORM_* defines)
- **Dependencies**: pthread, rt

## Testing

### Integration with prudynt-t

prudynt-t is an open-source camera streaming application that uses IMP libraries:

1. Initialize system: `IMP_System_Init()`
2. Create FrameSource channel: `IMP_FrameSource_CreateChn()`
3. Create Encoder channel: `IMP_Encoder_CreateChn()`
4. Bind FrameSource to Encoder: `IMP_System_Bind()`
5. Enable FrameSource: `IMP_FrameSource_EnableChn()`
6. Start encoding: `IMP_Encoder_StartRecvPic()`
7. Get streams: `IMP_Encoder_GetStream()` in loop
8. Release streams: `IMP_Encoder_ReleaseStream()`

## Future Work

1. **Hardware Encoder Integration**: AL_Encoder_Create/Process with real Ingenic hardware
2. **Complete Stream Handling**: Full IMPEncoderStream structure population
3. **Observer Management**: Complete observer create/destroy in bind/unbind
4. **Performance Optimization**: Reduce latency, improve throughput
5. **Additional Modules**: OSD, IVS, Audio (currently stubs)

