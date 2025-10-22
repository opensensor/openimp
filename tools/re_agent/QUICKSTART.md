# Quick Start - MIPS RE Agent

## TL;DR - What You Want

**Let AI fix your code automatically:**

```bash
cd tools/re_agent
export OPENAI_API_KEY="your-key"

# This will EDIT your source files with AI corrections
python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --binary port_9009 \
  --apply-fixes
```

**What it does:**
- Loads binary context (which functions exist in OEM binary)
- **Skips functions that don't exist in OEM binary** (likely dead code)
- Analyzes only OEM functions with GPT-4
- Reverse-engineers from Binary Ninja decompilations
- Generates corrected implementations with safe struct access
- **Directly edits your source files** with the corrections
- Use `git diff` to review changes, `git restore` to undo

**Just want to review first?** Omit `--apply-fixes`:

```bash
# Review mode - no edits, just analysis
python full_review_workflow.py --project-dir /home/matteius/openimp
```

## Setup (One Time)

```bash
cd tools/re_agent
./setup.sh
export OPENAI_API_KEY="your-openai-key"
```

## Common Tasks

### 1. Fix Everything (Recommended)

```bash
# Let AI fix all your code
python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --apply-fixes
```

### 2. Fix Specific Files

```bash
# Fix specific sources
python full_review_workflow.py \
  --sources "src/imp/imp_encoder.c" "src/imp/imp_system.c" \
  --apply-fixes
```

### 3. Review Only (No Edits)

```bash
# Just analyze, don't edit
python full_review_workflow.py --project-dir /home/matteius/openimp
```

### 3. Review One File

```bash
# Check a single source file for issues
python batch_review.py --review-file src/imp/imp_encoder.c
```

### 4. Process a Log

```bash
# Extract and implement from a log
python batch_review.py --input logs/my_session.log
```

### 5. Decompile and Implement

```bash
# Auto-decompile from Binary Ninja MCP
python batch_review.py \
  --decompile IMP_Encoder_CreateGroup \
  --binary port_9009
```

## What It Does

1. **Finds Issues**
   - Unsafe pointer arithmetic
   - Missing offset documentation
   - Incorrect struct layouts

2. **Generates Structs**
   - Proper padding
   - Offset comments
   - Safe types

3. **Creates Implementations**
   - Safe struct access
   - No pointer arithmetic
   - Proper error handling

## Output Files

```
full_review_output/
├── review_report.txt          # Read this first
├── all_structs.h              # Copy to your project
├── implementations/           # Corrected functions
│   ├── IMP_Encoder_CreateGroup.c
│   └── ...
└── review_results.json        # For automation
```

## Example Workflow

```bash
# 1. Do your reverse engineering work
# ... use Binary Ninja, take notes, save logs ...

# 2. Run the agent to fix your code
cd tools/re_agent
python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --apply-fixes

# 3. Review what changed
git diff

# 4. Test your code
cd ../..
make test

# 5. Commit if satisfied
git add -A
git commit -m "Apply AI-generated safe struct access patterns"

# Or undo if needed
git restore src/
```

## Safe Struct Access Rules

The agent enforces these patterns:

```c
// ✅ GOOD: Typed access
typedef struct {
    uint32_t field;  /* 0x10 */
} MyStruct;

MyStruct *s = ptr;
value = s->field;

// ✅ GOOD: memcpy for safety
uint32_t value;
memcpy(&value, ptr + 0x10, sizeof(value));

// ❌ BAD: Direct pointer arithmetic
value = *(uint32_t*)(ptr + 0x10);  // UNSAFE!
```

## Troubleshooting

**"No decompilations found"**
- Check your log format
- Ensure logs contain function code
- Try adding more context

**"API key error"**
- Set: `export OPENAI_API_KEY="your-key"`
- Check you have credits
- Verify network access

**"No issues found"**
- Good! Your code is safe
- Check the report anyway for recommendations

## Next Steps

- Read [BATCH_USAGE.md](BATCH_USAGE.md) for detailed guide
- See [README.md](README.md) for all features
- Check [examples/](examples/) for workflows

## One-Liner Reference

```bash
# Full project review
python full_review_workflow.py --project-dir /home/matteius/openimp

# Specific files
python full_review_workflow.py --logs "logs/*.log" --sources "src/imp/*.c"

# Single file review
python batch_review.py --review-file src/imp/imp_encoder.c

# Process log
python batch_review.py --input logs/session.log

# Decompile function
python batch_review.py --decompile IMP_Encoder_CreateGroup --binary port_9009
```

That's it! Feed it your logs and code, get safe implementations back.

