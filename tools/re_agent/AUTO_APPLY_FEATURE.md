# Auto-Apply Feature

## Overview

The RE-Agent now supports **automatic application** of changes via the `--apply` flag. This eliminates the need for a separate manual step to apply changes.

## Usage

### Quick Start

```bash
python tools/re_agent/re_agent_cli.py
```

In the interactive prompt:
```
RE-Agent> analyze port_9009 IMP_ISP_AddSensor --apply
```

That's it! The changes will be automatically applied via Auggie CLI.

### What Happens

When you use the `--apply` flag:

1. **Analysis Phase** (RE-Agent)
   - Decompiles function via Binary Ninja MCP
   - Analyzes with GPT to understand struct offsets
   - Generates safe C implementation
   - Saves artifacts to `auggie_artifacts/` directory

2. **Auto-Apply Phase** (Auggie CLI)
   - Checks if Auggie CLI is installed
   - Reads the generated artifacts
   - Applies function implementation to source file
   - Applies struct updates (if any)
   - Reports success or failure

### Example Output

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

## Manual Mode (Review Before Apply)

If you want to review artifacts before applying, just omit the `--apply` flag:

```
RE-Agent> analyze port_9009 IMP_ISP_AddSensor
```

Then manually apply:
```bash
# Review artifacts first
cat tools/re_agent/full_review_output/auggie_artifacts/IMP_ISP_AddSensor.json

# Dry-run to see what will happen
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor --dry-run

# Apply if it looks good
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor
```

## Requirements

### Auggie CLI Must Be Installed

The auto-apply feature requires Auggie CLI to be installed:

```bash
npm install -g @augmentcode/auggie
```

Verify installation:
```bash
auggie --version
```

If Auggie is not installed, the tool will:
- Skip auto-apply
- Show a warning message
- Provide instructions for manual application

### Binary Ninja MCP Server Must Be Running

The analysis phase requires Binary Ninja MCP server to be running on the specified port (e.g., `port_9009`).

## Error Handling

### Auggie Not Found

```
⚠ Auggie CLI not found - skipping auto-apply
  Install with: npm install -g @augmentcode/auggie
  Then manually run: python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor
```

### Analysis Failed

```
Analysis failed: <error message>
```

The tool will stop and not attempt to apply changes.

### Apply Failed

```
  → Applying function implementation...
    ✗ Failed to apply function: <error message>
```

The tool will report the error but continue with other artifacts (e.g., struct updates).

## Comparison: Old vs New

### Old Workflow (Manual)

```bash
# Step 1: Analyze
python tools/re_agent/re_agent_cli.py
RE-Agent> analyze port_9009 IMP_ISP_AddSensor

# Step 2: Review artifacts
cat tools/re_agent/full_review_output/auggie_artifacts/IMP_ISP_AddSensor.json

# Step 3: Apply manually
python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor
```

**3 separate steps**

### New Workflow (Auto-Apply)

```bash
python tools/re_agent/re_agent_cli.py
RE-Agent> analyze port_9009 IMP_ISP_AddSensor --apply
```

**1 step!**

## When to Use Auto-Apply

### ✅ Use Auto-Apply When:

- You trust the RE-Agent analysis
- You're iterating quickly on implementations
- You want to minimize manual steps
- You're working on well-understood functions

### ⚠️ Use Manual Mode When:

- You want to review artifacts before applying
- You're working on critical/complex functions
- You want to modify artifacts before applying
- You're debugging or testing the workflow

## Implementation Details

The auto-apply feature is implemented in `analyze_command()` in `re_agent_cli.py`:

1. After analysis completes and artifacts are saved
2. If `auto_apply=True` and artifacts exist:
   - Import `auggie_apply` module
   - Check if Auggie CLI is available
   - Apply function implementation artifact
   - Apply struct update artifacts (if any)
   - Report results

The actual application logic is in `tools/re_agent/auggie_apply.py`, which:
- Reads JSON artifacts
- Constructs prompts for Auggie CLI
- Calls Auggie via subprocess
- Parses and reports results

## Troubleshooting

### Auto-apply doesn't work

1. **Check Auggie is installed:**
   ```bash
   which auggie
   auggie --version
   ```

2. **Check artifacts were generated:**
   ```bash
   ls -la tools/re_agent/full_review_output/auggie_artifacts/
   ```

3. **Try manual mode to see detailed errors:**
   ```bash
   python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor --dry-run
   ```

### Changes not applied correctly

1. **Review the artifacts:**
   ```bash
   cat tools/re_agent/full_review_output/auggie_artifacts/IMP_ISP_AddSensor.json
   ```

2. **Check the source file:**
   ```bash
   git diff src/imp_isp.c
   ```

3. **Revert and try manual mode:**
   ```bash
   git checkout src/imp_isp.c
   python tools/re_agent/auggie_apply.py --function IMP_ISP_AddSensor --dry-run
   ```

## Future Enhancements

Potential improvements to the auto-apply feature:

1. **Interactive confirmation** - Ask before applying each change
2. **Automatic testing** - Run tests after applying changes
3. **Rollback on failure** - Automatically revert if tests fail
4. **Batch mode** - Apply multiple functions at once
5. **Git integration** - Automatically commit successful changes

## See Also

- `README_AUGGIE_WORKFLOW.md` - Complete workflow documentation
- `REFACTOR_SUMMARY.md` - Refactoring details
- `auggie_apply.py` - Auggie integration implementation

