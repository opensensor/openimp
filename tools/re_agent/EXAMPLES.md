# Examples - How the Agent Handles Different Function Types

## Example 1: OEM Function (Exists in Binary)

### Function: `IMP_Encoder_CreateGroup`

**Binary Context:**
```
✓ Function 'IMP_Encoder_CreateGroup' EXISTS in OEM binary port_9009
  → REVERSE-ENGINEER this from the binary decompilation
  → Extract struct offsets and generate safe implementation
```

**Input (from Binary Ninja decompilation):**
```c
int32_t IMP_Encoder_CreateGroup(int32_t grpNum) {
    void* group = malloc(0x308);
    if (!group) {
        return -1;
    }
    
    *(int32_t*)(group + 0x0) = grpNum;
    *(int32_t*)(group + 0x4) = 0;
    *(uint8_t*)(group + 0x3ac) = 0;
    
    sem_init((sem_t*)(group + 0xe4), 0, 0);
    
    return IMP_Encoder_RegisterGroup(grpNum, group);
}
```

**AI Analysis:**
- Function exists in OEM binary → reverse-engineer it
- Identifies struct offsets: 0x0, 0x4, 0x3ac, 0xe4
- Detects unsafe pointer arithmetic
- Generates struct definition
- Creates safe implementation

**Output (corrected implementation):**
```c
typedef struct EncoderGroup {
    int32_t grp_num;        /* 0x0000: Group number */
    int32_t state;          /* 0x0004: Group state */
    uint8_t padding_0008[0xdc];
    sem_t sem_e4;           /* 0x00e4: Semaphore */
    uint8_t padding_00fc[0x2b0];
    uint8_t started;        /* 0x03ac: Started flag */
    uint8_t padding_03ad[0x5b];
} EncoderGroup;  /* Total size: 0x308 */

int32_t IMP_Encoder_CreateGroup(int32_t grpNum) {
    EncoderGroup* group = malloc(sizeof(EncoderGroup));
    if (!group) {
        return -1;
    }
    
    // Safe typed access - no pointer arithmetic!
    group->grp_num = grpNum;
    group->state = 0;
    group->started = 0;
    
    sem_init(&group->sem_e4, 0, 0);
    
    return IMP_Encoder_RegisterGroup(grpNum, group);
}
```

---

## Example 2: Wrapper Function (Doesn't Exist in Binary)

### Function: `IMP_Encoder_Init_Wrapper`

**Binary Context:**
```
✗ Function 'IMP_Encoder_Init_Wrapper' DOES NOT EXIST in OEM binary
  → This is YOUR implementation (not in OEM)
  → REVIEW the existing code for correctness
  → Check if it properly calls OEM functions
  → Fix any unsafe pointer arithmetic

Call chain analysis:
  • IMP_Encoder_CreateGroup (OEM function - reverse-engineer separately)
  • IMP_Encoder_SetDefaultParams (OEM function - reverse-engineer separately)
```

**Input (existing wrapper code with issues):**
```c
int IMP_Encoder_Init_Wrapper(int grpNum, EncoderConfig* config) {
    // Create the encoder group
    int ret = IMP_Encoder_CreateGroup(grpNum);
    if (ret < 0) {
        return ret;
    }
    
    // UNSAFE: Direct pointer arithmetic!
    *(int32_t*)((void*)config + 0x10) = 1920;
    *(int32_t*)((void*)config + 0x14) = 1080;
    
    // Set default parameters
    return IMP_Encoder_SetDefaultParams(grpNum, config);
}
```

**AI Analysis:**
- Function doesn't exist in OEM binary → this is a wrapper
- Detects unsafe pointer arithmetic at offsets 0x10, 0x14
- Identifies calls to OEM functions (IMP_Encoder_CreateGroup, IMP_Encoder_SetDefaultParams)
- Needs to fix unsafe access to config struct

**Output (corrected wrapper):**
```c
int IMP_Encoder_Init_Wrapper(int grpNum, EncoderConfig* config) {
    // Create the encoder group (calls OEM function)
    int ret = IMP_Encoder_CreateGroup(grpNum);
    if (ret < 0) {
        return ret;
    }
    
    // Safe typed access - assuming EncoderConfig is defined
    config->width = 1920;   /* 0x10 */
    config->height = 1080;  /* 0x14 */
    
    // Set default parameters (calls OEM function)
    return IMP_Encoder_SetDefaultParams(grpNum, config);
}
```

---

## Example 3: Helper Function (Doesn't Exist in Binary)

### Function: `validate_encoder_params`

**Binary Context:**
```
✗ Function 'validate_encoder_params' DOES NOT EXIST in OEM binary
  → This is YOUR implementation (helper function)
  → REVIEW the existing code for correctness
  → No OEM functions to reverse-engineer
```

**Input (existing helper with issues):**
```c
int validate_encoder_params(void* params) {
    // UNSAFE: Accessing unknown struct with pointer arithmetic
    int width = *(int32_t*)(params + 0x10);
    int height = *(int32_t*)(params + 0x14);
    int bitrate = *(int32_t*)(params + 0x20);
    
    if (width <= 0 || width > 1920) return -1;
    if (height <= 0 || height > 1080) return -1;
    if (bitrate <= 0) return -1;
    
    return 0;
}
```

**AI Analysis:**
- Function doesn't exist in OEM binary → this is a helper
- Detects unsafe pointer arithmetic
- No OEM functions called (pure validation logic)
- Needs proper struct definition

**Output (corrected helper):**
```c
int validate_encoder_params(EncoderParams* params) {
    if (!params) {
        return -1;
    }
    
    // Safe typed access
    if (params->width <= 0 || params->width > 1920) {
        return -1;
    }
    if (params->height <= 0 || params->height > 1080) {
        return -1;
    }
    if (params->bitrate <= 0) {
        return -1;
    }
    
    return 0;
}
```

---

## Example 4: Mixed Function (Calls OEM Functions)

### Function: `setup_encoder_channel`

**Binary Context:**
```
✗ Function 'setup_encoder_channel' DOES NOT EXIST in OEM binary
  → This is YOUR implementation
  → Calls 3 OEM functions:
     • IMP_Encoder_CreateChn (OEM function)
     • IMP_Encoder_RegisterChn (OEM function)
     • IMP_Encoder_StartRecvPic (OEM function)
```

**Input (existing code with issues):**
```c
int setup_encoder_channel(int chnNum, void* attr) {
    // UNSAFE: Direct pointer arithmetic on attr
    *(int32_t*)(attr + 0x0) = chnNum;
    *(int32_t*)(attr + 0x4) = 1;  // H264
    
    int ret = IMP_Encoder_CreateChn(chnNum, attr);
    if (ret < 0) return ret;
    
    ret = IMP_Encoder_RegisterChn(chnNum, 0);
    if (ret < 0) return ret;
    
    return IMP_Encoder_StartRecvPic(chnNum);
}
```

**AI Analysis:**
- Function doesn't exist in OEM binary → this is your implementation
- Calls OEM functions (CreateChn, RegisterChn, StartRecvPic)
- Detects unsafe access to attr struct
- Needs to fix struct access while preserving OEM function calls

**Output (corrected implementation):**
```c
int setup_encoder_channel(int chnNum, IMPEncoderChnAttr* attr) {
    if (!attr) {
        return -1;
    }
    
    // Safe typed access
    attr->chn_num = chnNum;      /* 0x00 */
    attr->payload_type = 1;      /* 0x04: H264 */
    
    // Call OEM functions
    int ret = IMP_Encoder_CreateChn(chnNum, attr);
    if (ret < 0) {
        return ret;
    }
    
    ret = IMP_Encoder_RegisterChn(chnNum, 0);
    if (ret < 0) {
        return ret;
    }
    
    return IMP_Encoder_StartRecvPic(chnNum);
}
```

---

## Summary

The AI agent handles functions differently based on binary context:

### OEM Functions (exist in binary):
1. ✅ Reverse-engineer from Binary Ninja decompilation
2. ✅ Extract struct offsets from pointer arithmetic
3. ✅ Generate struct definitions with padding
4. ✅ Create safe implementation matching OEM behavior

### Your Functions (don't exist in binary):
1. ✅ Review existing implementation
2. ✅ Fix unsafe pointer arithmetic
3. ✅ Ensure proper struct access
4. ✅ Verify OEM function calls are correct
5. ✅ Preserve your logic while making it safe

The key is: **the AI knows what to do because it has binary context!**

