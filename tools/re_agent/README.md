# MIPS Driver Reverse Engineering Agent

An OpenAI-powered agent specialized in reverse engineering MIPS drivers using Binary Ninja MCP smart diff tooling with safe struct member access patterns.

## Features

- 🔍 **Decompilation Analysis**: Analyze Binary Ninja MCP decompilations to extract struct layouts
- 🏗️ **Struct Generation**: Automatically generate C struct definitions from offset information
- ✅ **Safe Access Patterns**: Generate code following safe struct member access patterns
- 🔄 **Version Comparison**: Compare binary versions to identify API and struct changes
- 🤖 **AI-Powered**: Leverages OpenAI GPT-4 for intelligent analysis and recommendations
- 📊 **Smart Diff Integration**: Works with existing Binary Ninja MCP smart diff tooling

## Safe Struct Member Access Patterns

This agent follows critical safety rules when working with reverse-engineered structures:

1. ✅ **Typed access**: `struct->member` (preferred when struct definition is known)
2. ✅ **memcpy()**: `memcpy(&value, ptr + offset, sizeof(value))` (safe for unknown layouts)
3. ❌ **Never**: Direct pointer arithmetic like `*(uint32_t*)(ptr + offset)` (unsafe, breaks on alignment)

## Installation

```bash
cd tools/re_agent
pip install -r requirements.txt
```

Set your OpenAI API key:
```bash
export OPENAI_API_KEY="your-api-key-here"
```

## Quick Start

### Batch Processing (Recommended for Production)

**FIX MODE - Actually edit your code:**

```bash
# Let AI fix your code automatically (uses git for version control)
python full_review_workflow.py \
  --project-dir /home/matteius/openimp \
  --apply-fixes

# Review changes
git diff

# Commit or undo
git add -A && git commit -m "Apply AI corrections"
# OR
git restore src/
```

**REVIEW MODE - Just analyze:**

```bash
# Review without editing
python full_review_workflow.py --project-dir /home/matteius/openimp

# Check the results
cat full_review_output/review_report.txt      # Comprehensive report
cat full_review_output/all_structs.h          # All struct definitions
ls full_review_output/implementations/        # Corrected implementations
```

**What it does:**
- Analyzes every function with GPT-4
- Generates corrected implementations with safe struct access
- **With `--apply-fixes`: Directly edits your source files**
- Without flag: Just generates reports and recommendations

### Batch Review Individual Files

```bash
# Review a specific source file
python batch_review.py --review-file src/imp/imp_encoder.c

# Process a log file
python batch_review.py --input logs/session.log

# Decompile and implement a function
python batch_review.py --decompile IMP_Encoder_CreateGroup --binary port_9009
```

### Interactive Mode

```bash
python re_agent_cli.py -i
```

This launches an interactive session where you can:
- Ask questions about reverse engineering
- Analyze decompiled functions
- Generate struct definitions
- Compare binary versions

### Command-Line Usage

Analyze a function:
```bash
python re_agent_cli.py analyze \
  -b port_9009 \
  -f IMP_Encoder_CreateGroup \
  -o analysis.json
```

Compare function versions:
```bash
python re_agent_cli.py compare \
  -o port_9009 \
  -n port_9012 \
  -f IMP_System_Bind \
  -O comparison.json
```

### Python API

```python
from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient

# Initialize agent
agent = MIPSReverseEngineeringAgent(model="gpt-4o")
mcp = BinaryNinjaMCPClient()

# Analyze decompiled code
decompiled_code = """
int32_t IMP_Encoder_CreateGroup(int32_t grpNum) {
    void* group_ptr = malloc(0x308);
    *(int32_t*)(group_ptr + 0x00) = grpNum;
    sem_init((sem_t*)(group_ptr + 0xe4), 0, 0);
    return 0;
}
"""

result = agent.analyze_decompilation(decompiled_code, "IMP_Encoder_CreateGroup")
print(result)

# Generate struct definition
members = [
    StructMember("grp_num", 0x00, 4, "int32_t", "Group number"),
    StructMember("sem_e4", 0xe4, 32, "sem_t", "Semaphore"),
]

struct_def = agent.generate_struct_definition("EncoderGroup", members)
print(struct_def)

# Generate safe access code
safe_code = agent.generate_safe_access_code("EncoderGroup", "grp_num", "read")
print(safe_code)
```

## Examples

See the `examples/` directory for complete workflows:

- **`analyze_encoder.py`**: Analyze IMP encoder functions and generate struct definitions
- **`compare_versions.py`**: Compare T31 and T23 binary versions to identify changes

Run examples:
```bash
cd examples
python analyze_encoder.py
python compare_versions.py
```

## Architecture

### Components

1. **MIPSReverseEngineeringAgent**: Core AI agent with OpenAI integration
   - Analyzes decompiled code
   - Generates struct definitions
   - Provides safe access patterns
   - Maintains conversation context

2. **BinaryNinjaMCPClient**: Interface to Binary Ninja MCP servers
   - Lists available binaries
   - Decompiles functions
   - Compares binary versions
   - Performs smart diff analysis

3. **SmartDiffAnalyzer**: High-level analysis tools
   - Extracts struct offsets
   - Compares function versions
   - Identifies API changes

### Workflow

```
┌─────────────────────────────────────────────────────────────┐
│                    User Request                             │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│              Binary Ninja MCP Client                        │
│  • Decompile function from MIPS binary                      │
│  • Extract raw decompiled C code                            │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│         MIPS Reverse Engineering Agent (OpenAI)             │
│  • Analyze struct offsets                                   │
│  • Infer member types                                       │
│  • Generate struct definitions                              │
│  • Create safe access code                                  │
│  • Provide recommendations                                  │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  Generated Output                           │
│  • C struct definitions                                     │
│  • Safe access code                                         │
│  • Analysis reports (JSON)                                  │
│  • Recommendations                                          │
└─────────────────────────────────────────────────────────────┘
```

## Use Cases

### 1. Analyzing New Functions

When you encounter a new function in a MIPS binary:

1. Decompile it using Binary Ninja MCP
2. Feed the decompilation to the agent
3. Get struct definitions and safe access code
4. Integrate into your implementation

### 2. Comparing Binary Versions

When analyzing different platform versions (T31, T23, T40, etc.):

1. Decompile the same function from both binaries
2. Use the agent to compare versions
3. Identify offset changes and new fields
4. Update your implementation with platform conditionals

### 3. Struct Layout Discovery

When reverse engineering a complex structure:

1. Analyze multiple functions that access the struct
2. Collect all observed offsets
3. Use the agent to synthesize a complete struct definition
4. Verify against actual binary behavior

### 4. Safe Code Generation

When implementing driver functionality:

1. Provide the agent with struct layout information
2. Request safe access code for specific members
3. Get code that follows proper patterns
4. Avoid unsafe pointer arithmetic

## Best Practices

### 1. Document Offsets

Always include offset comments in generated structs:
```c
typedef struct EncoderGroup {
    int32_t grp_num;        /* 0x00: Group number */
    int32_t state;          /* 0x04: Group state */
    sem_t sem_e4;           /* 0xe4: Semaphore */
} EncoderGroup;
```

### 2. Use Platform Conditionals

Handle platform differences with preprocessor directives:
```c
#if defined(PLATFORM_T31)
    void *bind_func;        /* 0x40 */
#elif defined(PLATFORM_T23)
    uint32_t new_field;     /* 0x40 */
    void *bind_func;        /* 0x44 */
#endif
```

### 3. Verify Struct Sizes

Always verify total struct size matches binary:
```c
_Static_assert(sizeof(EncoderGroup) == 0x308, "EncoderGroup size mismatch");
```

### 4. Preserve Binary Compatibility

Never change discovered offsets - they must match the binary exactly:
```c
// CRITICAL: These offsets are from Binary Ninja decompilation
// Changing them breaks binary compatibility!
typedef struct Module {
    char name[16];          /* 0x00 */
    void *bind_func;        /* 0x40 */
    sem_t sem_e4;           /* 0xe4 */
    pthread_mutex_t mutex;  /* 0x108 */
    uint32_t group_id;      /* 0x130 */
} Module;
```

## Integration with OpenIMP

This agent is designed to work seamlessly with the OpenIMP project:

1. **Analyze OEM binaries**: Decompile functions from stock libimp.so
2. **Generate implementations**: Create C code for OpenIMP
3. **Maintain compatibility**: Ensure struct layouts match OEM binaries
4. **Document findings**: Generate analysis reports for the team

## Troubleshooting

### OpenAI API Key Not Set

```bash
export OPENAI_API_KEY="your-key-here"
```

### Binary Ninja MCP Not Available

The MCP client currently uses placeholder implementations. To integrate with actual Binary Ninja MCP:

1. Ensure Binary Ninja MCP servers are running
2. Update `binja_mcp_client.py` with actual MCP calls
3. Configure MCP server addresses

### Model Not Available

If `gpt-4o` is not available, use an alternative:
```bash
python re_agent_cli.py -i --model gpt-4-turbo
```

## Contributing

When adding new features:

1. Follow safe struct access patterns
2. Document all struct offsets
3. Add examples for new functionality
4. Update this README

## License

Part of the OpenIMP project. See main project LICENSE.

## References

- [OpenIMP Project](../../README.md)
- [Binary Ninja MCP Documentation](https://docs.binary.ninja/)
- [Reverse Engineering Summary](../../REVERSE_ENGINEERING_SUMMARY.md)
- [Architecture Documentation](../../assets/ARCHITECTURE.md)

