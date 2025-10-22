# RE-Agent + Auggie CLI Workflow

## Overview

The RE-Agent tooling has been refactored to use a **separation of concerns** approach:

1. **RE-Agent** (Python) - Analysis only
   - Decompiles functions via Binary Ninja MCP
   - Analyzes with GPT to understand struct offsets and generate implementations
   - **Outputs structured artifacts** (JSON files)
   - **Does NOT edit source code** (no fragile regex parsing!)

2. **Auggie CLI** (Augment's CLI tool) - Code editing
   - Reads artifacts from RE-Agent
   - Uses Augment's proper code editing tools (str-replace-editor, codebase-retrieval)
   - Makes precise, safe edits to source files
   - Validates changes

## Installation

### 1. Install Auggie CLI

```bash
# Requires Node.js 22+
npm install -g @augmentcode/auggie
```

Verify installation:
```bash
auggie --version
```

### 2. RE-Agent is already installed

The RE-Agent tools are in this directory (`tools/re_agent/`).

## Workflow

### Quick Start (Auto-Apply)

**NEW!** You can now auto-apply changes with the `--apply` flag:

```bash
python tools/re_agent/re_agent_cli.py
```

In the interactive prompt:
```
RE-Agent> analyze port_9009 IMP_ISP_AddSensor --apply
```

This will:
1. Decompile `IMP_ISP_AddSensor` from Binary Ninja MCP server on port 9009
2. Analyze struct offsets and generate safe C implementation
3. Save artifacts to `tools/re_agent/full_review_output/auggie_artifacts/`
4. **Automatically apply changes via Auggie CLI**

### Manual Workflow (Review Before Apply)

If you want to review artifacts before applying:

#### Step 1: Analyze a Function

```bash
python tools/re_agent/re_agent_cli.py
```

In the interactive prompt:
```
RE-Agent> analyze port_9009 IMP_ISP_AddSensor
```

Or from command line:
```bash
python tools/re_agent/re_agent_cli.py analyze port_9009 IMP_ISP_AddSensor
```

This will:
- Decompile `IMP_ISP_AddSensor` from Binary Ninja MCP server on port 9009
- Analyze struct offsets and generate safe C implementation
- Save artifacts to `tools/re_agent/full_review_output/auggie_artifacts/`

**Output (without --apply):**
```
✓ Analysis complete for IMP_ISP_AddSensor
  ✓ Implementation saved to tools/re_agent/full_review_output/implementations/IMP_ISP_AddSensor.c

⚠ Struct updates recommended:
  • ISPDevice in src/imp_isp.c

✓ Generated 2 Auggie artifact(s) in tools/re_agent/full_review_output/auggie_artifacts

To apply changes, run:
  python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor
  # Or for dry-run:
  python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor --dry-run

  # Or use --apply flag to auto-apply:
  RE-Agent> analyze port_9009 IMP_ISP_AddSensor --apply
```

**Output (with --apply):**
```
✓ Analysis complete for IMP_ISP_AddSensor
  ✓ Implementation saved to tools/re_agent/full_review_output/implementations/IMP_ISP_AddSensor.c

⚠ Struct updates recommended:
  • ISPDevice in src/imp_isp.c

✓ Generated 2 Auggie artifact(s) in tools/re_agent/full_review_output/auggie_artifacts

🔧 Auto-applying changes via Auggie...

  → Applying function implementation...
    ✓ Function implementation applied successfully

  → Applying struct update for ISPDevice...
    ✓ Struct update applied successfully

✅ Auto-apply complete!
```

### Step 2: Review the Artifacts

Check what was generated:

```bash
# View the function implementation artifact
cat tools/re_agent/full_review_output/auggie_artifacts/IMP_ISP_AddSensor.json

# View the struct update artifact (if any)
cat tools/re_agent/full_review_output/auggie_artifacts/ISPDevice_update.json
```

### Step 3: Apply Changes (Dry Run First!)

**Always do a dry-run first** to see what Auggie will do:

```bash
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor --dry-run
```

This will show you the prompt that will be sent to Auggie without making any changes.

### Step 4: Apply Changes for Real

If the dry-run looks good, apply the changes:

```bash
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor
```

Auggie will:
1. Read the artifact
2. Find the function in the source file
3. Replace it with the new implementation using str-replace-editor
4. Report success or failure

### Step 5: Apply Struct Updates (if needed)

If struct updates were recommended:

```bash
# Dry-run first
python tools/re_agent/auggie_apply.py --struct ISPDevice --dry-run

# Apply if it looks good
python tools/re_agent/auggie_apply.py --struct ISPDevice
```

### Step 6: Build and Test

After applying changes, build and test:

```bash
make clean
make
# Run your tests
```

## Batch Processing

To analyze and apply multiple functions:

```bash
# Analyze all functions (one at a time in interactive mode)
python tools/re_agent/re_agent_cli.py
RE-Agent> analyze port_9009 IMP_ISP_AddSensor
RE-Agent> analyze port_9009 IMP_ISP_EnableSensor
RE-Agent> analyze port_9009 IMP_ISP_DisableSensor

# Apply all pending artifacts
python tools/re_agent/auggie_apply.py --apply-all --dry-run  # Review first!
python tools/re_agent/auggie_apply.py --apply-all            # Apply
```

## Artifact Format

### Function Implementation Artifact

`tools/re_agent/full_review_output/auggie_artifacts/IMP_ISP_AddSensor.json`:

```json
{
  "function_name": "IMP_ISP_AddSensor",
  "implementation": "int IMP_ISP_AddSensor(...) {\n  ...\n}",
  "struct_definitions": ["typedef struct { ... } ISPDevice;"],
  "notes": "Analysis notes from GPT",
  "issues_found": ["List of issues discovered"]
}
```

### Struct Update Artifact

`tools/re_agent/full_review_output/auggie_artifacts/ISPDevice_update.json`:

```json
{
  "src_file": "src/imp_isp.c",
  "struct_name": "ISPDevice",
  "discovered_offsets": {
    "0x20": "File descriptor",
    "0xac": "Buffer pointer"
  },
  "proposed_definition": "typedef struct { ... } ISPDevice;",
  "notes": "Struct definition needs update based on discovered offsets"
}
```

## Advantages of This Approach

### ✅ No Regex Parsing
- RE-Agent no longer tries to parse C code with regex
- Auggie uses proper AST-based code understanding

### ✅ Safer Edits
- Auggie's str-replace-editor is designed for precise code edits
- Changes are validated and can be undone

### ✅ Better Error Handling
- Clear separation: analysis errors vs. editing errors
- Auggie can ask for clarification if needed

### ✅ Reviewable Artifacts
- All changes are saved as JSON artifacts
- You can review, modify, or reject before applying

### ✅ Separation of Concerns
- RE-Agent: Reverse engineering and analysis (what it's good at)
- Auggie: Code editing (what it's good at)

## Troubleshooting

### Auggie not found

```bash
# Check if auggie is installed
which auggie

# If not, install it
npm install -g @augmentcode/auggie

# If you have Node.js version issues
nvm install 22
nvm use 22
npm install -g @augmentcode/auggie
```

### RE-Agent analysis fails

```bash
# Check Binary Ninja MCP server is running
curl http://localhost:9009/status

# Check the error in the output
# Common issues:
# - Function doesn't exist in binary
# - GPT API key not set
# - Network issues
```

### Auggie fails to apply changes

- Review the dry-run output carefully
- Check that the source file exists and is writable
- Ensure the function exists in the source file
- Try applying manually based on the artifact

## Advanced Usage

### Custom Artifacts Directory

```bash
# Analyze with custom output directory
python tools/re_agent/re_agent_cli.py analyze port_9009 IMP_ISP_AddSensor --out-dir my_analysis

# Apply from custom directory
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor --artifacts-dir my_analysis/auggie_artifacts
```

### Manual Artifact Creation

You can create artifacts manually for Auggie to apply:

```json
{
  "function_name": "MyFunction",
  "implementation": "int MyFunction() { return 42; }",
  "struct_definitions": [],
  "notes": "Manually created",
  "issues_found": []
}
```

Save as `my_function.json` and apply:
```bash
python tools/re_agent/auggie_apply.py --function MyFunction --artifacts-dir .
```

## Migration from Old Workflow

### Old Way (Fragile)
```bash
# This used regex to edit code directly - error-prone!
python tools/re_agent/re_agent_cli.py
RE-Agent> live-apply port_9009 IMP_ISP_AddSensor
```

### New Way (Robust)
```bash
# Step 1: Analyze (generates artifacts)
python tools/re_agent/re_agent_cli.py
RE-Agent> analyze port_9009 IMP_ISP_AddSensor

# Step 2: Review artifacts
cat tools/re_agent/full_review_output/auggie_artifacts/IMP_ISP_AddSensor.json

# Step 3: Apply with Auggie (proper code editing)
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor --dry-run
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor
```

## See Also

- `STRUCT_UPDATE_LOGIC.md` - Details on struct analysis
- `tools/re_agent/auggie_apply.py` - Auggie integration script
- `tools/re_agent/batch_review.py` - RE-Agent analysis engine

