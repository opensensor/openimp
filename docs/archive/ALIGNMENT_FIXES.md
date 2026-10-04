# AVPU Alignment Fixes for MIPS Kernel Crashes

## Problem
After the initial AVPU refactoring, the kernel was still crashing with unaligned access errors:

```
[ 1087.676140] CPU: 0 PID: 9516 Comm: dropbear Tainted: G           O 3.10.14__isvp_swan_1.0__ #1
[ 1087.739711] epc   : c03ca918 0xc03ca918
Unhandled kernel unaligned access[#2]:
[ 1087.852149] BadVA : 013fd8ff
Unhandled kernel unaligned access[#3]:
[ 1087.951405] BadVA : ffffd8ff
```

## Root Cause
MIPS architecture requires **4-byte alignment** for all 32-bit memory accesses. The kernel driver was receiving unaligned data structures from userspace, causing unaligned access exceptions.

## Critical Fixes Applied

### 1. Command List Buffer Alignment Verification
**File**: `src/codec.c` lines 619-673

**Problem**: Command list buffer allocated via IMP_Alloc might not be properly aligned.

**Fix**: Added alignment verification after allocation:
```c
if ((phys & 3) != 0 || ((uintptr_t)virt & 3) != 0) {
    LOG_CODEC("ERROR: cmdlist buffer not 4-byte aligned: phys=0x%08x virt=%p", phys, virt);
} else {
    // Proceed with initialization
}
```

### 2. Command List Entry Alignment Check
**File**: `src/codec.c` lines 797-830

**Problem**: Individual command list entries (512 bytes each) might not be aligned when accessed.

**Fix**: Added per-entry alignment verification:
```c
uint8_t* entry = (uint8_t*)ctx->cl_ring.map + (size_t)idx * ctx->cl_entry_size;

/* Verify entry alignment */
if (((uintptr_t)entry & 3) != 0) {
    LOG_CODEC("ERROR: CL entry not 4-byte aligned: %p", (void*)entry);
    free(hw_stream);
    return -1;
}
```

### 3. Command Register Fill Function Alignment
**File**: `src/codec.c` lines 69-110

**Problem**: The `fill_cmd_regs_enc1` function was writing to potentially unaligned buffers.

**Fix**: Added alignment check at function entry:
```c
static void fill_cmd_regs_enc1(const ALAvpuContext* ctx, uint32_t* cmd)
{
    if (!ctx || !cmd) return;
    
    /* Ensure cmd pointer is 4-byte aligned (MIPS requirement) */
    if (((uintptr_t)cmd & 3) != 0) {
        LOG_CODEC("ERROR: cmd buffer not 4-byte aligned: %p", (void*)cmd);
        return;
    }
    
    /* Initialize entire command buffer to zero first (512 bytes = 128 uint32_t) */
    memset(cmd, 0, 512);
    
    // ... rest of function
}
```

### 4. Cache Flush for Command List (MIPS Requirement)
**File**: `src/codec.c` lines 797-830

**Problem**: MIPS CPUs have separate instruction and data caches. Command list data written by CPU must be flushed to memory before hardware can read it.

**Fix**: Added cache flush after filling command list:
```c
/* Fill Enc1 command registers (includes memset) */
fill_cmd_regs_enc1(ctx, cmd);

/* Flush cache for command list entry (MIPS requirement) */
/* Note: OEM uses Rtos_FlushCacheMemory(arg3, 0x100000) */
/* We flush just the CL entry to avoid excessive cache operations */
__builtin___clear_cache((char*)entry, (char*)entry + ctx->cl_entry_size);
```

### 5. Stream Buffer Queueing Order Fix
**File**: `src/codec.c` lines 797-830

**Problem**: Stream buffer was being queued AFTER starting the encode, causing race condition.

**Fix**: Moved stream buffer queueing BEFORE command list programming:
```c
/* Ensure at least one STRM buffer is queued BEFORE starting encode */
int any_in_hw = 0;
for (int i = 0; i < ctx->stream_bufs_used; ++i) {
    if (ctx->stream_in_hw[i]) { any_in_hw = 1; break; }
}
if (!any_in_hw && ctx->stream_bufs_used > 0) {
    avpu_write_reg(fd, AVPU_REG_STRM_PUSH, ctx->stream_bufs[0].phy_addr);
    ctx->stream_in_hw[0] = 1;
    LOG_CODEC("AVPU: queued stream buf[0] phys=0x%08x", ctx->stream_bufs[0].phy_addr);
}

/* NOW program CL and start encode */
uint32_t cl_phys = ctx->cl_ring.phy_addr + (idx * ctx->cl_entry_size);
avpu_write_reg(fd, AVPU_REG_CL_ADDR, cl_phys);
avpu_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000002);
avpu_write_reg(fd, AVPU_REG_SRC_PUSH, phys_addr);
```

### 6. Interrupt Enable Moved to End
**File**: `src/codec.c` lines 797-830

**Problem**: Interrupts were being enabled before all registers were programmed, causing race conditions.

**Fix**: Moved interrupt enable to AFTER all register programming:
```c
/* Push source frame address (OEM parity: direct ioctl) */
avpu_write_reg(fd, AVPU_REG_SRC_PUSH, phys_addr);

/* Unmask interrupts AFTER everything is programmed */
avpu_enable_interrupts(fd, 0);
```

## MIPS Alignment Requirements

### Why 4-Byte Alignment?
MIPS architecture requires:
- **32-bit (4-byte) values** must be aligned to 4-byte boundaries
- **16-bit (2-byte) values** must be aligned to 2-byte boundaries
- **8-bit (1-byte) values** can be at any address

### What Happens on Unaligned Access?
1. CPU attempts unaligned load/store
2. Hardware exception is triggered
3. Kernel exception handler is invoked
4. If kernel can't handle it → kernel panic/crash
5. Error message: "Unhandled kernel unaligned access"

### How to Verify Alignment?
```c
/* Check if address is 4-byte aligned */
if ((address & 3) != 0) {
    // NOT aligned - will cause crash on MIPS
}

/* Check if pointer is 4-byte aligned */
if (((uintptr_t)ptr & 3) != 0) {
    // NOT aligned - will cause crash on MIPS
}
```

## OEM Parity Notes

### From Binary Ninja Decompilation:

**AL_EncCore_Encode1** (0x6cbf0):
```c
Rtos_FlushCacheMemory(arg3, 0x100000)  // Flush 1MB of cache
```
- OEM flushes entire 1MB region
- We flush only the 512-byte CL entry (more efficient)

**AL_EncCore_Init** (0x6c8d8):
```c
(*(**arg1 + 8))()  // Write register 0x8054 with value 0x80
arg1[8] = 1
arg1[9] = 0
arg1[0x10] = 2
```
- OEM initializes core state machine
- We replicate this in our lazy-start initialization

## Testing Checklist

After these fixes, verify:
- [ ] No kernel crashes on first frame
- [ ] No "Unhandled kernel unaligned access" messages in dmesg
- [ ] Command list buffer phys/virt addresses are 4-byte aligned
- [ ] Command list entries are 4-byte aligned
- [ ] Stream buffers are queued before encode starts
- [ ] Cache is flushed after filling command list
- [ ] Interrupts are enabled after all registers are programmed

## Expected Behavior

**Before fixes:**
```
[Codec] Process: AVPU queued frame 1920x1080 phys=0x6778000 CL[0]
[ 1087.676140] CPU: 0 PID: 9516 Comm: dropbear Tainted: G           O
Unhandled kernel unaligned access[#2]:
[ 1087.852149] BadVA : 013fd8ff
```

**After fixes:**
```
[Codec] Process: AVPU queued frame 1920x1080 phys=0x6778000 CL[0]
[Codec] AVPU: queued stream buf[0] phys=0x06d72000
[Encoder] GetStream[AVPU]: got stream phys=0x6d72000 len=12345
```

## Files Modified
1. `src/codec.c` - Added alignment checks and cache flush
   - Lines 69-110: `fill_cmd_regs_enc1` with alignment check
   - Lines 619-673: Command list allocation with alignment verification
   - Lines 797-830: Frame queueing with alignment checks and proper ordering

## Compilation Status
✅ No compilation errors - verified via diagnostics

## Next Steps
1. Build the project: `make clean && make`
2. Deploy to T31 device
3. Test ISP streaming
4. Monitor dmesg for kernel messages
5. Verify no unaligned access crashes
6. Confirm encoded streams are retrieved successfully

