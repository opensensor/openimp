# MIPS Reverse Engineering Agent - Complete Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Installation & Setup](#installation--setup)
4. [Basic Usage](#basic-usage)
5. [Advanced Workflows](#advanced-workflows)
6. [Best Practices](#best-practices)
7. [Integration with OpenIMP](#integration-with-openimp)
8. [Troubleshooting](#troubleshooting)

## Introduction

The MIPS Reverse Engineering Agent is an AI-powered tool designed to accelerate the reverse engineering of MIPS drivers, specifically for the Ingenic IMP (Ingenic Media Platform) library used in the OpenIMP project.

### What It Does

- **Analyzes** Binary Ninja MCP decompilations to extract struct layouts
- **Generates** safe C code following proper struct access patterns
- **Compares** binary versions to identify API changes
- **Recommends** implementation strategies based on decompiled code
- **Documents** findings in a structured, reusable format

### Why It's Needed

Reverse engineering MIPS binaries is challenging:
- Struct layouts must be discovered from decompiled code
- Offsets must be preserved exactly for binary compatibility
- Safe access patterns must be used to avoid crashes
- Platform differences (T31, T23, T40, etc.) must be handled
- Documentation must be maintained for the team

This agent automates much of this work while ensuring safety and correctness.

## Core Concepts

### Safe Struct Member Access

The agent enforces three rules for struct access:

#### ✅ Rule 1: Typed Access (Preferred)

```c
// Define the struct
typedef struct EncChannel {
    int32_t chn_id;         /* 0x00 */
    uint8_t started;        /* 0x3ac */
} EncChannel;

// Use typed access
EncChannel *channel = (EncChannel *)ptr;
if (channel->started) {
    // Safe!
}
```

#### ✅ Rule 2: memcpy() for Unknown Layouts

```c
// When struct definition is not available
uint8_t started;
memcpy(&started, ptr + 0x3ac, sizeof(started));
if (started) {
    // Safe!
}
```

#### ❌ Rule 3: Never Direct Pointer Arithmetic

```c
// NEVER DO THIS - unsafe and breaks on alignment
uint8_t started = *(uint8_t*)(ptr + 0x3ac);  // BAD!
```

### Struct Offset Discovery

The agent helps discover struct layouts from decompiled code:

```c
// Decompiled code shows:
*(uint8_t*)(channel + 0x3ac) = 1;
*(uint32_t*)(channel + 0x1094c4) = buf_size;

// Agent identifies:
// - Offset 0x3ac: uint8_t (1 byte)
// - Offset 0x1094c4: uint32_t (4 bytes)

// Agent generates:
typedef struct EncChannel {
    uint8_t padding_0000[0x3ac];
    uint8_t started;                /* 0x3ac */
    uint8_t padding_03ad[0x109117]; /* Fill gap */
    uint32_t buf_size;              /* 0x1094c4 */
} EncChannel;
```

### Platform Differences

The agent helps handle platform-specific variations:

```c
#if defined(PLATFORM_T31)
    void *bind_func;        /* 0x40 */
    void *unbind_func;      /* 0x44 */
#elif defined(PLATFORM_T23)
    uint32_t new_field;     /* 0x40: New in T23 */
    void *bind_func;        /* 0x44: Shifted by 4 */
    void *unbind_func;      /* 0x48: Shifted by 4 */
#endif
```

## Installation & Setup

### Prerequisites

- Python 3.8 or higher
- OpenAI API key
- Binary Ninja MCP (optional, for live decompilation)

### Quick Setup

```bash
cd tools/re_agent
./setup.sh
```

This will:
1. Install Python dependencies
2. Check for OpenAI API key
3. Make scripts executable

### Manual Setup

```bash
# Install dependencies
pip install -r requirements.txt

# Set API key
export OPENAI_API_KEY="your-api-key-here"

# Make scripts executable
chmod +x re_agent_cli.py examples/*.py
```

## Basic Usage

### Interactive Mode

The easiest way to get started:

```bash
./re_agent_cli.py -i
```

Example session:
```
RE-Agent> help
[Shows available commands]

RE-Agent> list-binaries
Available binaries:
  - port_9009: libimp.so (T31 v1.1.6) (mips32)
  - port_9012: libimp.so (T23) (mips32)

RE-Agent> What struct offsets are used in IMP_Encoder_GetStream?
[Agent analyzes and responds]

RE-Agent> Generate safe access code for EncChannel at offset 0x3ac
[Agent generates code]

RE-Agent> exit
```

### Command-Line Mode

For batch processing and automation:

```bash
# Analyze a function
./re_agent_cli.py analyze \
  -b port_9009 \
  -f IMP_Encoder_CreateGroup \
  -o results/encoder_analysis.json

# Compare versions
./re_agent_cli.py compare \
  -o port_9009 \
  -n port_9012 \
  -f IMP_System_Bind \
  -O results/bind_comparison.json
```

### Python API

For integration into scripts:

```python
from mips_re_agent import MIPSReverseEngineeringAgent

agent = MIPSReverseEngineeringAgent()

# Analyze decompiled code
result = agent.analyze_decompilation(decompiled_code, "function_name")

# Generate struct definition
struct_def = agent.generate_struct_definition("MyStruct", members)

# Ask questions
response = agent.ask("How should I handle this offset pattern?")
```

## Advanced Workflows

### Workflow 1: Implementing a New Function

**Goal**: Implement `IMP_Encoder_GetStream` based on Binary Ninja decompilation

**Steps**:

1. **Decompile** the function using Binary Ninja MCP
2. **Analyze** with the agent to extract struct offsets
3. **Generate** struct definition
4. **Create** safe implementation
5. **Test** and verify

**Example**:
```bash
# Run the complete workflow example
cd examples
python3 openimp_workflow.py
```

### Workflow 2: Comparing Platform Versions

**Goal**: Identify differences between T31 and T23 implementations

**Steps**:

1. **Decompile** function from both platforms
2. **Compare** using the agent
3. **Identify** offset changes
4. **Generate** platform-conditional code
5. **Document** findings

**Example**:
```bash
cd examples
python3 compare_versions.py
```

### Workflow 3: Discovering Complex Structs

**Goal**: Build complete struct definition from multiple functions

**Steps**:

1. **Analyze** multiple functions that access the struct
2. **Collect** all observed offsets
3. **Ask** the agent to synthesize complete definition
4. **Verify** total size matches binary
5. **Add** to struct database

**Example**:
```python
# Analyze multiple functions
offsets_from_func1 = agent.analyze_decompilation(code1, "func1")
offsets_from_func2 = agent.analyze_decompilation(code2, "func2")

# Ask agent to synthesize
question = f"""
I found these offsets from different functions:
Function 1: {offsets_from_func1}
Function 2: {offsets_from_func2}

Please create a complete struct definition.
"""
response = agent.ask(question)
```

## Best Practices

### 1. Always Document Offsets

```c
typedef struct EncChannel {
    int32_t chn_id;         /* 0x00: Channel ID */
    uint8_t started;        /* 0x3ac: Started flag */
    uint32_t buf_size;      /* 0x1094c4: Buffer size */
} EncChannel;
```

### 2. Verify Struct Sizes

```c
// Add static assertion
_Static_assert(sizeof(EncChannel) == 0x308, "Size mismatch");
```

### 3. Use Platform Conditionals

```c
#if defined(PLATFORM_T31)
    // T31-specific layout
#elif defined(PLATFORM_T23)
    // T23-specific layout
#endif
```

### 4. Preserve Binary Compatibility

```c
// CRITICAL: These offsets are from Binary Ninja decompilation
// DO NOT CHANGE - must match binary exactly!
```

### 5. Test Thoroughly

- Test edge cases (NULL pointers, invalid indices)
- Test circular buffer wraparound
- Test thread safety
- Verify against actual hardware

## Integration with OpenIMP

### Adding New Implementations

1. **Analyze** with the agent
2. **Generate** code
3. **Add** to appropriate source file (e.g., `src/imp_encoder.c`)
4. **Update** header (e.g., `include/imp/imp_encoder.h`)
5. **Document** in `REVERSE_ENGINEERING_SUMMARY.md`

### Example Integration

```c
// In src/imp_encoder.c

// Struct definition from agent
typedef struct EncChannel {
    int32_t chn_id;         /* 0x00 */
    uint8_t started;        /* 0x3ac */
    uint32_t buf_size;      /* 0x1094c4 */
} EncChannel;

// Implementation from agent
int IMP_Encoder_GetStream(int encChn, IMPEncoderStream *stream, int blockFlag) {
    // Validate inputs
    if (encChn < 0 || encChn >= MAX_ENC_CHN) {
        return -1;
    }
    
    // Safe access using struct
    EncChannel *channel = g_encoder_channels[encChn];
    if (!channel || !channel->started) {
        return -1;
    }
    
    // ... rest of implementation
}
```

### Documentation Template

Add to `REVERSE_ENGINEERING_SUMMARY.md`:

```markdown
### IMP_Encoder_GetStream

**Binary Ninja Analysis**: Decompiled from libimp.so v1.1.6 at address 0x12345

**Struct Offsets**:
- 0x3ac: started flag (uint8_t)
- 0x1094c4: buffer size (uint32_t)
- 0x1094d8: buffer index (int32_t)

**Implementation**: See src/imp_encoder.c line 123

**Platform Differences**: None identified between T31/T23
```

## Troubleshooting

### Issue: "OpenAI API key not set"

**Solution**:
```bash
export OPENAI_API_KEY="your-key-here"
```

### Issue: "Model not available"

**Solution**: Use a different model:
```bash
./re_agent_cli.py -i --model gpt-4-turbo
```

### Issue: "Binary Ninja MCP not responding"

**Solution**: The MCP client currently uses placeholders. For live integration:
1. Ensure Binary Ninja MCP servers are running
2. Update `binja_mcp_client.py` with actual MCP calls

### Issue: Agent gives incorrect struct layout

**Solution**:
1. Provide more context from multiple functions
2. Ask the agent to verify against known sizes
3. Cross-reference with other decompilations
4. Manually verify critical offsets

### Issue: Generated code doesn't compile

**Solution**:
1. Check for missing includes
2. Verify struct definitions are complete
3. Ask the agent to fix specific errors
4. Review and adjust manually

## Tips & Tricks

### Tip 1: Use Conversation Context

The agent maintains conversation history, so you can build on previous questions:

```
> Analyze this decompilation: [paste code]
> Now generate a struct definition
> Add platform conditionals for T23
> Generate test cases
```

### Tip 2: Save Important Results

Always save analysis results for future reference:

```bash
./re_agent_cli.py analyze -b port_9009 -f MyFunc -o results/myfunc.json
```

### Tip 3: Batch Process Multiple Functions

Create a script to analyze multiple functions:

```bash
for func in IMP_Encoder_CreateGroup IMP_Encoder_DestroyGroup IMP_Encoder_GetStream; do
    ./re_agent_cli.py analyze -b port_9009 -f $func -o results/${func}.json
done
```

### Tip 4: Use Examples as Templates

The examples in `examples/` are designed to be copied and modified for your needs.

## Conclusion

The MIPS Reverse Engineering Agent is a powerful tool for accelerating driver reverse engineering while maintaining safety and correctness. By following the patterns and workflows in this guide, you can efficiently analyze MIPS binaries and generate high-quality implementations for OpenIMP.

For more information:
- See [README.md](README.md) for quick reference
- Check [examples/](examples/) for complete workflows
- Review [OpenIMP documentation](../../README.md) for project context

