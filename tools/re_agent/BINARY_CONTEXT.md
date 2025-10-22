# Binary Context - Understanding What to Reverse Engineer

## The Problem

When implementing OpenIMP, you have two types of functions:

1. **Functions that exist in the OEM binary** (libimp.so)
   - These should be reverse-engineered from Binary Ninja decompilations
   - Extract struct offsets, understand the implementation
   - Generate safe C code that matches the binary behavior
   - **These are the ONLY functions the agent should process**

2. **Functions that DON'T exist in the OEM binary**
   - These are likely dead code or incorrect abstractions
   - They probably shouldn't exist in the codebase
   - Callers should call OEM functions directly instead
   - **The agent SKIPS these automatically**

**The AI needs to know which is which, and only process OEM functions!**

## How Binary Context Works

### 1. Load Binary Functions

When you run the workflow, it loads all functions from the binary:

```bash
python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --binary port_9009 \
  --apply-fixes
```

Output:
```
Loading binary context from port_9009...
  Loaded 1247 functions from binary
```

### 2. Analyze Each Function

For each function in your source code, the agent:

**a) Checks if it exists in the binary**

```
[1/8] Analyzing: IMP_Encoder_CreateGroup...
  Context: ✓ Function 'IMP_Encoder_CreateGroup' EXISTS in OEM binary port_9009
```

**b) Provides guidance to the AI**

```
✓ Function 'IMP_Encoder_CreateGroup' EXISTS in OEM binary port_9009
  → This should be reverse-engineered from the binary decompilation
  → Use Binary Ninja MCP to get the decompilation
  → Extract struct offsets and generate safe implementation

This function calls 3 other functions:
  • malloc (standard library)
  • sem_init (standard library)
  • IMP_Encoder_RegisterChn (exists in binary - needs RE)
```

**c) AI generates appropriate implementation**

- If exists in binary: Reverse-engineer from decompilation
- If doesn't exist: Implement as wrapper/helper

### 3. Follow Call Chains

The agent recursively analyzes call chains:

```
IMP_Encoder_CreateGroup (exists in binary)
  ├─ malloc (stdlib)
  ├─ sem_init (stdlib)
  └─ IMP_Encoder_RegisterChn (exists in binary)
      ├─ IMP_System_GetVersion (exists in binary)
      └─ register_channel_internal (exists in binary)
```

This helps the AI understand:
- Which dependencies need to be reverse-engineered
- What order to implement functions
- Which functions are wrappers vs OEM functions

## Using Binary Context Directly

### Analyze a Function

```bash
cd tools/re_agent

# Analyze a specific function
python binary_context.py \
  --binary port_9009 \
  --function IMP_Encoder_CreateGroup
```

Output:
```
Analyzing function: IMP_Encoder_CreateGroup
================================================================================
✓ Function 'IMP_Encoder_CreateGroup' EXISTS in OEM binary port_9009
  → This should be reverse-engineered from the binary decompilation
  → Use Binary Ninja MCP to get the decompilation
  → Extract struct offsets and generate safe implementation

This function calls 3 other functions:
  • malloc (exists in binary - needs RE)
  • sem_init (exists in binary - needs RE)
  • IMP_Encoder_RegisterChn (exists in binary - needs RE)


Call chain analysis:
--------------------------------------------------------------------------------
{
  "function": "IMP_Encoder_CreateGroup",
  "exists_in_binary": true,
  "callees": [
    {
      "function": "malloc",
      "exists_in_binary": true,
      "callees": [],
      "depth": 1
    },
    {
      "function": "IMP_Encoder_RegisterChn",
      "exists_in_binary": true,
      "callees": [...],
      "depth": 1
    }
  ],
  "depth": 0
}


Implementation order (dependencies first):
--------------------------------------------------------------------------------
1. malloc (RE from binary)
2. sem_init (RE from binary)
3. IMP_Encoder_RegisterChn (RE from binary)
4. IMP_Encoder_CreateGroup (RE from binary)
```

### Save/Load Context

```bash
# Save context for later use
python binary_context.py \
  --binary port_9009 \
  --save context_t31.json

# Load saved context
python binary_context.py \
  --load context_t31.json \
  --function IMP_System_Init
```

## How It Helps the AI

### Example 1: Function Exists in Binary

**Function:** `IMP_Encoder_CreateGroup`

**AI receives:**
```
BINARY CONTEXT:
✓ Function 'IMP_Encoder_CreateGroup' EXISTS in OEM binary port_9009
  → This should be reverse-engineered from the binary decompilation
  → Use Binary Ninja MCP to get the decompilation
  → Extract struct offsets and generate safe implementation

[decompiled code here]
```

**AI generates:**
- Reverse-engineered implementation
- Struct definitions from offsets
- Safe typed access patterns

### Example 2: Function Doesn't Exist in Binary

**Function:** `IMP_Encoder_Init_Wrapper`

**AI receives:**
```
BINARY CONTEXT:
✗ Function 'IMP_Encoder_Init_Wrapper' DOES NOT EXIST in OEM binary
  → This is a NEW implementation (wrapper, helper, or abstraction)
  → Do NOT try to reverse-engineer from binary
  → Implement based on API requirements and call chain

This function calls 2 other functions:
  • IMP_Encoder_CreateGroup (exists in binary - needs RE)
  • IMP_Encoder_SetDefaultParams (exists in binary - needs RE)

[existing code here]
```

**AI generates:**
- Wrapper implementation
- Calls to OEM functions
- No struct reverse-engineering (not needed)

## Benefits

### 1. Prevents Mistakes

Without context:
```c
// AI might try to reverse-engineer a wrapper function
// that doesn't exist in the binary!
```

With context:
```c
// AI knows this is a wrapper, implements it correctly
int MyWrapper(int param) {
    // Call the actual OEM function
    return IMP_Encoder_CreateGroup(param);
}
```

### 2. Correct Implementation Strategy

**Exists in binary:**
- Decompile with Binary Ninja MCP
- Extract struct offsets
- Generate safe implementation

**Doesn't exist in binary:**
- Implement as wrapper/helper
- Call OEM functions as needed
- No reverse-engineering needed

### 3. Dependency Order

The agent knows to implement dependencies first:

```
Implementation order:
1. IMP_System_GetVersion (dependency)
2. IMP_Encoder_RegisterChn (dependency)
3. IMP_Encoder_CreateGroup (uses above)
```

## Integration with Workflow

The binary context is automatically used in the full workflow:

```bash
python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --binary port_9009 \
  --apply-fixes
```

For each function analyzed:
1. Check if it exists in binary
2. Provide context to AI
3. Generate appropriate implementation
4. Apply fixes to source file

## Advanced Usage

### Multiple Binaries

Compare implementations across platforms:

```bash
# T31 binary
python binary_context.py --binary port_9009 --save context_t31.json

# T23 binary
python binary_context.py --binary port_9012 --save context_t23.json

# Compare
diff context_t31.json context_t23.json
```

### Custom Function Lists

Create a list of functions to implement:

```bash
# Get implementation order for a feature
python binary_context.py \
  --binary port_9009 \
  --function IMP_Encoder_CreateGroup \
  > encoder_implementation_order.txt
```

## Summary

Binary context helps the AI:
- ✅ Know which functions exist in the OEM binary
- ✅ Understand call chains and dependencies
- ✅ Generate appropriate implementations (RE vs wrapper)
- ✅ Implement in correct dependency order
- ✅ Avoid trying to reverse-engineer functions that don't exist

This makes the AI much smarter about what to do with each function!

