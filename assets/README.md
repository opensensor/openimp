# OpenIMP Architecture Assets

This directory contains comprehensive architecture documentation and diagrams for the OpenIMP library.

## Files

### Documentation

- **ARCHITECTURE.md**: Complete architecture documentation with ASCII diagrams
  - Data flow from hardware to user application
  - Module architecture and responsibilities
  - Threading model and synchronization
  - Memory management with DMA
  - Module binding via observer pattern
  - Build system and testing

### Mermaid Diagrams

All `.mmd` files are Mermaid diagrams that can be rendered in various tools:

- **data-flow.mmd**: End-to-end data flow diagram
  - Shows complete pipeline from kernel driver to user application
  - Includes all intermediate steps and data structures
  - Color-coded by component type

- **module-architecture.mmd**: Module relationship diagram
  - Shows all 11 modules and their dependencies
  - Distinguishes between core modules, infrastructure, and stubs
  - Illustrates module registration and binding

- **threading-model.mmd**: Threading and synchronization diagram
  - Shows all thread types (frame capture, encoder, stream)
  - Illustrates synchronization primitives (mutex, semaphore, condvar, eventfd)
  - Thread lifecycle and creation/cancellation

- **memory-management.mmd**: DMA allocation and buffer management
  - Shows complete flow from IMP_Alloc to VBM frame buffers
  - Kernel driver integration with ioctl and mmap
  - Fallback mechanism for non-DMA systems

## Viewing Mermaid Diagrams

### Online Viewers

1. **Mermaid Live Editor**: https://mermaid.live/
   - Copy/paste the `.mmd` file content
   - Interactive editing and rendering
   - Export to PNG/SVG

2. **GitHub**: 
   - GitHub automatically renders `.mmd` files in markdown
   - View directly in the repository

### VS Code

Install the "Mermaid Preview" extension:
```bash
code --install-extension bierner.markdown-mermaid
```

Then open any `.mmd` file and use the preview pane.

### Command Line

Install mermaid-cli:
```bash
npm install -g @mermaid-js/mermaid-cli
```

Render to PNG:
```bash
mmdc -i data-flow.mmd -o data-flow.png
```

Render to SVG:
```bash
mmdc -i data-flow.mmd -o data-flow.svg
```

## Architecture Overview

### Key Components

1. **Hardware Layer**: Ingenic ISP and kernel drivers
2. **Kernel Interface**: ioctl wrappers for /dev/framechan%d
3. **DMA Allocator**: Physical memory allocation via /dev/mem
4. **VBM**: Video buffer manager with frame pools
5. **FrameSource**: Frame capture with polling thread
6. **Encoder**: H.264/H.265 encoding with dual threads
7. **Codec**: Parameter management and FIFO queuing
8. **System**: Module registry and observer pattern
9. **Fifo**: Thread-safe queue implementation

### Data Flow Summary

```
Hardware → Kernel Driver → Frame Capture Thread → VBM Pool
    → Observer Pattern → Encoder Thread → Codec Frame FIFO
    → Stream Thread → Codec Stream FIFO → User Application
```

### Threading Summary

- **Frame Capture Threads**: 1 per FrameSource channel (max 5)
- **Encoder Threads**: 1 per Encoder channel (max 9)
- **Stream Threads**: 1 per Encoder channel (max 9)
- **Total**: Up to 23 threads (5 + 9 + 9)

### Memory Summary

- **DMA Buffers**: Allocated via kernel driver with mmap
- **VBM Pools**: 0x180 + (bufcount * 0x428) bytes per pool
- **VBM Frames**: 0x428 bytes per frame
- **Codec**: 0x924 bytes per codec instance
- **Channels**: 0x2e8 bytes (FS) or 0x308 bytes (Encoder)

## Implementation Status

- **Total Lines**: 4,434 lines of C code
- **Library Size**: 125KB (static), 88KB (shared)
- **Modules**: 11 modules (8 core + 3 infrastructure)
- **Functions**: 105+ functions implemented
- **Build Status**: ✅ Compiles cleanly

## Next Steps

1. **Hardware Encoder Integration**: Connect to actual Ingenic hardware codec
2. **Complete Stream Handling**: Full IMPEncoderStream structure
3. **Observer Management**: Complete observer lifecycle
4. **Testing**: Integration with prudynt-t on real hardware
5. **Performance**: Optimize latency and throughput

## References

- **Binary**: libimp.so v1.1.6 for T31 platform
- **Decompiler**: Binary Ninja MCP
- **Target Application**: prudynt-t (open-source camera streaming)
- **Hardware**: Ingenic T31/T40/T41 SoCs

## License

This is a reverse-engineered implementation for educational and interoperability purposes.

