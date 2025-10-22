# How the AI Agent Actually Fixes Your Code

## Overview

This agent doesn't just analyze - it **actually edits your source files** with AI-generated corrections.

## The Process

### 1. You Run It

```bash
cd tools/re_agent
export OPENAI_API_KEY="your-key"
python full_review_workflow.py --project-dir /home/matteius/openimp --apply-fixes
```

### 2. It Scans Your Project

```
Finding source files...
  Found 14 source files

[1/14] src/imp_encoder.c
  Found 8 functions
```

### 3. For Each Function, It:

**a) Extracts the function code**
```c
int IMP_Encoder_CreateGroup(int grpNum) {
    void* group = malloc(0x308);
    *(int32_t*)(group + 0x0) = grpNum;  // UNSAFE!
    *(int32_t*)(group + 0x4) = 0;       // UNSAFE!
    return 0;
}
```

**b) Sends it to GPT-4 for analysis**

The AI agent:
- Identifies struct offsets (0x0, 0x4, etc.)
- Detects unsafe pointer arithmetic
- Generates proper struct definition
- Creates corrected implementation with typed access

**c) Receives corrected implementation**
```c
typedef struct EncoderGroup {
    int32_t grp_num;  /* 0x0000 */
    int32_t state;    /* 0x0004 */
    uint8_t padding_0008[0x300];
} EncoderGroup;

int IMP_Encoder_CreateGroup(int grpNum) {
    EncoderGroup* group = malloc(sizeof(EncoderGroup));
    if (!group) return -1;
    
    group->grp_num = grpNum;  // SAFE!
    group->state = 0;         // SAFE!
    return 0;
}
```

**d) Replaces the function in your source file**

The agent:
1. Finds the function in your source file
2. Replaces it with the corrected version
3. Preserves surrounding code
4. Shows: `✓ Applied correction to IMP_Encoder_CreateGroup`

### 4. You Review and Commit

```bash
# See what changed
git diff

# Test it
make test

# Commit if good
git add -A
git commit -m "Apply AI-generated safe struct access patterns"

# Or undo if needed
git restore src/imp_encoder.c
```

## What Gets Fixed

### Unsafe Pattern → Safe Pattern

**Before (UNSAFE):**
```c
*(uint32_t*)(ptr + 0x10) = value;
```

**After (SAFE):**
```c
struct->field = value;  // where field is at offset 0x10
```

### Missing Structs → Proper Definitions

**Before:**
```c
void* opaque_ptr = get_something();
*(int*)(opaque_ptr + 0x3ac) = 1;
```

**After:**
```c
typedef struct Thing {
    uint8_t padding_0000[0x3ac];
    int started;  /* 0x3ac */
} Thing;

Thing* thing = get_something();
thing->started = 1;
```

## AI Analysis

The agent uses GPT-4 to:

1. **Extract struct layouts** from decompiled code
2. **Identify unsafe patterns** (pointer arithmetic, casts)
3. **Generate struct definitions** with proper padding
4. **Create safe implementations** using typed access
5. **Preserve semantics** while improving safety

## Example Session

```
================================================================================
FULL PROJECT WORKFLOW - FIX MODE - EDITING FILES
================================================================================

Project: /home/matteius/openimp
⚠️  WARNING: This will MODIFY your source files!
   Use 'git diff' to review changes
   Use 'git restore <file>' to undo changes

Step 1: Processing logs...
  Found 0 decompilations in logs

Step 2: Reviewing source files...
  Found 14 source files

  [1/14] src/imp_encoder.c
  Found 8 functions
  [1/8] Analyzing: IMP_Encoder_CreateGroup...
    → Found 5 struct offsets
    → Generated struct definition
    → Generated corrected implementation
  [2/8] Analyzing: IMP_Encoder_DestroyGroup...
    → Found 3 struct offsets
    → Generated corrected implementation
  ...
  ✓ Modified src/imp_encoder.c (8 functions corrected)

  [2/14] src/imp_system.c
  ...

================================================================================
SUMMARY
================================================================================
Functions analyzed:        47
Struct definitions found:  12
Issues identified:         23
Implementations generated: 23
Files modified:            5

Modified files:
  • src/imp_encoder.c
  • src/imp_system.c
  • src/imp_framesource.c
  • src/imp_osd.c
  • src/imp_isp.c

Output directory:          full_review_output
================================================================================

✓ Your source files have been corrected!

Next steps:
  1. Review changes: git diff
  2. Test your code to verify corrections
  3. Commit if satisfied: git add -A && git commit -m 'Apply AI corrections'
  4. Undo if needed: git restore <file>
```

## Safety

- **Git-based workflow**: No .bak files, use git for version control
- **Review before commit**: Always check `git diff`
- **Easy undo**: `git restore <file>` to revert
- **Test first**: Run your tests before committing

## When to Use

### Use `--apply-fixes` when:
- ✅ You trust the AI analysis
- ✅ You have git to review/undo changes
- ✅ You want to fix everything in one go
- ✅ You'll test the changes after

### Use review mode (no flag) when:
- 📋 You want to see what it would do first
- 📋 You want to manually review each change
- 📋 You're not ready to edit files yet

## Technical Details

### Function Replacement Algorithm

1. Parse source file to find all functions
2. For each function:
   - Extract function signature and body
   - Send to GPT-4 for analysis
   - Receive corrected implementation
3. Find function in original file using regex
4. Match braces to find function boundaries
5. Replace old function with corrected version
6. Write modified content back to file

### Struct Definition Generation

The AI analyzes decompiled code to:
- Identify all struct member accesses
- Extract offsets from pointer arithmetic
- Determine member types from usage
- Calculate padding between members
- Generate complete struct definition with offset comments

### Safe Access Pattern Enforcement

The AI is instructed to:
- Never use `*(type*)(ptr + offset)` patterns
- Always use typed struct access: `struct->member`
- Use `memcpy()` for unknown layouts
- Add offset comments for verification
- Preserve exact semantics of original code

## Limitations

- **Function-level replacement**: Replaces entire functions, not individual lines
- **Requires valid C**: Source must be parseable
- **AI can make mistakes**: Always review with `git diff`
- **Costs API credits**: Each function analyzed costs OpenAI API credits

## Best Practices

1. **Start small**: Try on one file first
2. **Review everything**: Check `git diff` carefully
3. **Test thoroughly**: Run all tests after applying fixes
4. **Commit incrementally**: Don't fix everything at once
5. **Keep backups**: Git is your backup, but consider branching

## Troubleshooting

**"Could not find function X in file"**
- Function signature might not match regex
- Try review mode first to see the generated code

**"AI analysis failed"**
- Check your OPENAI_API_KEY
- Verify you have API credits
- Function might be too complex

**"Modified code doesn't compile"**
- AI made a mistake - use `git restore`
- Review the generated struct definitions
- May need manual adjustment

## Summary

This agent **actually fixes your code** by:
1. Analyzing each function with GPT-4
2. Generating safe implementations
3. Directly editing your source files
4. Using git for version control

No manual copy-paste, no .bak files, just AI-powered code correction.

