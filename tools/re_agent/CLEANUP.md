# Cleanup After AI Agent Run

## Problem

The AI agent generated code with hallucinated struct members and types that don't exist in the actual codebase, causing compilation errors.

## Errors Found

### 1. imp_isp.c
- **Error**: References to non-existent `IMPSensorInfo` members: `i2c_type`, `i2c_addr`, `i2c_adapter`
- **Cause**: AI hallucinated struct members that don't exist in the actual struct definition

### 2. imp_system.c  
- **Error**: Uses undefined types `IMP_System` and `Module`
- **Error**: Calls undefined function `get_system_instance()`
- **Cause**: AI generated code using types/functions that don't exist in the codebase

### 3. imp_osd.c
- **Error**: References to non-existent `IMPOSDRgnAttr` members: `pos_x`, `pos_y`, `width`, `height`, `color`
- **Error**: References to non-existent `OSDRegion` members: `attributes`, `show_flag`
- **Cause**: AI hallucinated struct members

### 4. codec.c
- **Warning**: Use-after-free in `AL_Codec_Encode_ReleaseStream`
- **Cause**: LOG_CODEC uses `stream` pointer after `free(hw_stream)`

## Solution

Revert the AI-generated changes and restore the working code:

```bash
# Check what files were modified
git status

# Review the changes
git diff src/imp_isp.c
git diff src/imp_system.c
git diff src/imp_osd.c

# Revert all AI-generated changes
git restore src/imp_isp.c
git restore src/imp_system.c
git restore src/imp_osd.c
git restore src/codec.c

# Or revert everything at once
git restore src/
```

## Root Cause

The AI agent is sending too much context (30,000+ tokens) which causes:
1. **Rate limit errors** - Requests exceed 30,000 TPM limit
2. **Hallucinations** - AI makes up struct members when context is too large
3. **Poor quality** - AI doesn't have enough "room" to properly analyze

## Fixes Needed

### 1. Reduce Context Size
The agent needs to send less context per request:
- Don't send entire file history
- Don't send all previous functions
- Just send the current function + minimal context

### 2. Use Binary Ninja Decompilations
Instead of asking AI to "guess" struct layouts:
- Actually call Binary Ninja MCP to get decompilations
- Extract struct offsets from real decompiled code
- Don't let AI hallucinate struct members

### 3. Validate Against Headers
Before applying fixes:
- Check that struct members actually exist in headers
- Validate function signatures match declarations
- Don't apply changes that reference undefined types

## Immediate Action

1. **Revert all changes**:
   ```bash
   git restore src/
   ```

2. **Verify build works**:
   ```bash
   make clean
   make
   ```

3. **Fix the agent** before running again:
   - Reduce context size
   - Add validation
   - Use actual Binary Ninja decompilations

## Prevention

Before running the agent again:
1. Test on a single small file first
2. Review changes before committing
3. Run `make` to verify compilation
4. Use `git diff` to inspect changes

## Files to Revert

Based on logs.txt, these files were modified and have errors:
- `src/imp_isp.c` - Hallucinated struct members
- `src/imp_system.c` - Undefined types and functions
- `src/imp_osd.c` - Hallucinated struct members
- `src/codec.c` - Use-after-free (minor)

Other files that were processed but may have issues:
- `src/imp_encoder.c` - Check for errors
- `src/imp_framesource.c` - Check for errors
- `src/imp_ivs.c` - Check for errors
- `src/imp_audio.c` - Check for errors

## Summary

The AI agent needs significant improvements before it can be used safely:
1. ❌ Currently hallucinates struct members
2. ❌ Sends too much context (causes rate limits and poor quality)
3. ❌ Doesn't validate against actual headers
4. ❌ Doesn't use actual Binary Ninja decompilations

**DO NOT USE** until these issues are fixed.

For now, revert all changes with `git restore src/`.

