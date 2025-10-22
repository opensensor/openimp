# Skip Behavior - Only Process OEM Functions

## The Strategy

The agent now **only processes functions that exist in the OEM binary** (libimp.so).

Functions that don't exist in the binary are **skipped automatically** because:
1. They probably shouldn't exist (dead code, incorrect abstractions)
2. Callers should call OEM functions directly instead
3. No point in analyzing/fixing code that should be removed

## How It Works

### 1. Load Binary Function List

When the agent starts, it loads all known functions from the OEM binary:

```bash
python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --binary port_9009 \
  --apply-fixes
```

Output:
```
Loading functions from binary port_9009...
  Loaded 62 functions from binary
```

### 2. Check Each Function

For each function in your source code:

**If it EXISTS in the binary:**
```
[1/8] Analyzing: IMP_Encoder_CreateGroup...
  ✓ Function 'IMP_Encoder_CreateGroup' EXISTS in OEM binary port_9009
  → Analyzing with GPT-4...
  → Generating corrected implementation...
  ✓ Applied correction
```

**If it DOESN'T exist in the binary:**
```
[2/8] Skipping: my_wrapper_function (not in OEM binary)
```

### 3. Only Process OEM Functions

The AI only receives functions that exist in the OEM binary.

It will:
- Reverse-engineer from Binary Ninja decompilation
- Extract struct offsets
- Generate safe implementations
- Apply fixes to your source files

## Example Run

```bash
cd tools/re_agent
export OPENAI_API_KEY="your-key"

python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --binary port_9009 \
  --apply-fixes
```

Output:
```
================================================================================
FULL PROJECT WORKFLOW - FIX MODE - EDITING FILES
================================================================================

Project: /home/matteius/openimp
Binary:  port_9009

⚠️  WARNING: This will MODIFY your source files!
   Use 'git diff' to review changes
   Use 'git restore <file>' to undo changes

Loading functions from binary port_9009...
  Loaded 62 functions from binary

Step 1: Processing source files...
  Found 15 source files

Processing: src/imp_encoder.c
  Found 8 functions
  [1/8] Analyzing: IMP_Encoder_CreateGroup...
    ✓ Function 'IMP_Encoder_CreateGroup' EXISTS in OEM binary port_9009
    → Analyzing with GPT-4...
    → Found 5 struct offsets
    → Generated corrected implementation
    ✓ Applied correction to IMP_Encoder_CreateGroup
  
  [2/8] Skipping: encoder_init_wrapper (not in OEM binary)
  
  [3/8] Analyzing: IMP_Encoder_CreateChn...
    ✓ Function 'IMP_Encoder_CreateChn' EXISTS in OEM binary port_9009
    → Analyzing with GPT-4...
    → Found 3 struct offsets
    → Generated corrected implementation
    ✓ Applied correction to IMP_Encoder_CreateChn
  
  [4/8] Skipping: setup_encoder_channel (not in OEM binary)
  
  [5/8] Analyzing: IMP_Encoder_StartRecvPic...
    ✓ Function 'IMP_Encoder_StartRecvPic' EXISTS in OEM binary port_9009
    → Analyzing with GPT-4...
    → Found 2 struct offsets
    → Generated corrected implementation
    ✓ Applied correction to IMP_Encoder_StartRecvPic

  ... (continues for all functions)

================================================================================
SUMMARY
================================================================================

Functions analyzed:        5
Functions skipped:         3
Implementations generated: 5
Files modified:            1

Modified files:
  • src/imp_encoder.c

✓ Your source files have been corrected!

Next steps:
  1. Review changes: git diff
  2. Test your code to verify corrections
  3. Commit if satisfied: git add -A && git commit -m 'Apply AI corrections'
  4. Undo if needed: git restore <file>
```

## Known OEM Functions

The agent knows about these OEM functions (from libimp.so):

### System Functions
- IMP_System_Init
- IMP_System_Exit
- IMP_System_GetVersion
- IMP_System_Bind
- IMP_System_UnBind

### Encoder Functions
- IMP_Encoder_CreateGroup
- IMP_Encoder_DestroyGroup
- IMP_Encoder_CreateChn
- IMP_Encoder_DestroyChn
- IMP_Encoder_RegisterChn
- IMP_Encoder_UnRegisterChn
- IMP_Encoder_StartRecvPic
- IMP_Encoder_StopRecvPic
- IMP_Encoder_Query
- IMP_Encoder_GetStream
- IMP_Encoder_ReleaseStream

### FrameSource Functions
- IMP_FrameSource_CreateChn
- IMP_FrameSource_DestroyChn
- IMP_FrameSource_SetChnAttr
- IMP_FrameSource_GetChnAttr
- IMP_FrameSource_EnableChn
- IMP_FrameSource_DisableChn
- IMP_FrameSource_SetFrameDepth
- IMP_FrameSource_GetFrameDepth

### ISP Functions
- IMP_ISP_Open
- IMP_ISP_Close
- IMP_ISP_AddSensor
- IMP_ISP_DelSensor
- IMP_ISP_EnableSensor
- IMP_ISP_DisableSensor
- IMP_ISP_SetSensorRegister
- IMP_ISP_GetSensorRegister
- IMP_ISP_Tuning_SetContrast
- IMP_ISP_Tuning_GetContrast
- IMP_ISP_Tuning_SetBrightness
- IMP_ISP_Tuning_GetBrightness
- IMP_ISP_Tuning_SetSaturation
- IMP_ISP_Tuning_GetSaturation
- IMP_ISP_Tuning_SetSharpness
- IMP_ISP_Tuning_GetSharpness

### OSD Functions
- IMP_OSD_CreateGroup
- IMP_OSD_DestroyGroup
- IMP_OSD_RegisterRgn
- IMP_OSD_UnRegisterRgn
- IMP_OSD_SetRgnAttr
- IMP_OSD_GetRgnAttr
- IMP_OSD_ShowRgn
- IMP_OSD_HideRgn

### IVS Functions
- IMP_IVS_CreateGroup
- IMP_IVS_DestroyGroup
- IMP_IVS_CreateChn
- IMP_IVS_DestroyChn
- IMP_IVS_RegisterChn
- IMP_IVS_UnRegisterChn
- IMP_IVS_StartRecvPic
- IMP_IVS_StopRecvPic
- IMP_IVS_PollingResult
- IMP_IVS_GetResult
- IMP_IVS_ReleaseResult

**Total: 62 OEM functions**

## What Gets Skipped

Any function NOT in the above list will be skipped:

- Wrapper functions (e.g., `encoder_init_wrapper`)
- Helper functions (e.g., `validate_encoder_params`)
- Utility functions (e.g., `setup_encoder_channel`)
- Internal functions (e.g., `_internal_helper`)

These are likely dead code or incorrect abstractions that should be removed.

## Fail-Safe Behavior

If the binary function list fails to load (e.g., Binary Ninja MCP not available):

```
Loading functions from binary port_9009...
  Warning: Could not load binary functions: Connection refused
  Continuing without binary context (will process all functions)
```

The agent will **process all functions** as a fail-safe, rather than skipping everything.

## Benefits

1. ✅ **Focuses on what matters** - Only OEM functions that need reverse-engineering
2. ✅ **Saves time and API costs** - Doesn't analyze dead code
3. ✅ **Cleaner output** - Only shows relevant functions
4. ✅ **Identifies dead code** - Shows you what functions shouldn't exist

## Summary

The agent is now **smart about what to process**:
- ✅ Process OEM functions (exist in binary)
- ❌ Skip non-OEM functions (don't exist in binary)
- ✅ Fail-safe: process everything if binary context unavailable

