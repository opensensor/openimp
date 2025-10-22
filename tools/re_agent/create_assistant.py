#!/usr/bin/env python3
"""
Create OpenAI Assistant for MIPS Reverse Engineering

This script creates a persistent OpenAI Assistant that you can interact with
directly through the OpenAI Playground, API, or compatible clients.

The assistant will have access to custom functions for:
- Analyzing decompiled code
- Generating struct definitions
- Creating safe access patterns
- Comparing binary versions
"""

import os
import json
from openai import OpenAI


def create_re_assistant():
    """Create the MIPS RE Assistant"""
    
    client = OpenAI(api_key=os.getenv("OPENAI_API_KEY"))
    
    # Define the assistant's instructions
    instructions = """You are an expert MIPS reverse engineering assistant specializing in Ingenic SoC drivers.

Your expertise includes:
- Analyzing Binary Ninja MCP decompilations of MIPS binaries
- Understanding Ingenic IMP (Ingenic Media Platform) driver architecture
- Identifying struct layouts and member offsets from decompiled code
- Generating safe C code that follows proper struct access patterns
- Comparing binary versions to identify API changes

CRITICAL RULES FOR STRUCT ACCESS:
1. ALWAYS use typed access (struct->member) when the struct definition is known
2. Use memcpy() for safe access when working with offsets: memcpy(&value, ptr + offset, sizeof(value))
3. NEVER use direct pointer arithmetic like *(uint32_t*)(ptr + offset) - this is unsafe
4. Document all struct offsets discovered from decompilation with comments
5. Preserve exact byte offsets - changing them breaks binary compatibility

WORKFLOW:
1. Analyze decompiled code to identify struct offsets
2. Create struct definitions with proper member types
3. Generate safe access code using typed members
4. Document findings with offset comments for verification

PLATFORMS:
- T31 (primary): Ingenic T31 SoC
- T23, T40, T41, C100: Other Ingenic platforms with slight variations

You have access to custom functions for:
- analyze_decompilation: Extract struct information from decompiled code
- generate_struct_definition: Create C struct definitions
- generate_safe_access_code: Generate safe member access code
- compare_binary_versions: Compare function versions
- list_binaries: List available binaries in Binary Ninja MCP

Always provide detailed explanations and safe code examples."""

    # Define custom functions (tools)
    tools = [
        {
            "type": "function",
            "function": {
                "name": "analyze_decompilation",
                "description": "Analyze decompiled MIPS code to extract struct offsets and generate safe implementations",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "decompiled_code": {
                            "type": "string",
                            "description": "The decompiled C code from Binary Ninja"
                        },
                        "function_name": {
                            "type": "string",
                            "description": "Name of the function being analyzed"
                        }
                    },
                    "required": ["decompiled_code", "function_name"]
                }
            }
        },
        {
            "type": "function",
            "function": {
                "name": "generate_struct_definition",
                "description": "Generate a C struct definition from member information",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "struct_name": {
                            "type": "string",
                            "description": "Name of the struct"
                        },
                        "members": {
                            "type": "array",
                            "description": "List of struct members",
                            "items": {
                                "type": "object",
                                "properties": {
                                    "name": {"type": "string"},
                                    "offset": {"type": "integer"},
                                    "size": {"type": "integer"},
                                    "type_name": {"type": "string"},
                                    "description": {"type": "string"}
                                },
                                "required": ["name", "offset", "size", "type_name"]
                            }
                        }
                    },
                    "required": ["struct_name", "members"]
                }
            }
        },
        {
            "type": "function",
            "function": {
                "name": "generate_safe_access_code",
                "description": "Generate safe struct member access code",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "struct_name": {
                            "type": "string",
                            "description": "Name of the struct"
                        },
                        "member_name": {
                            "type": "string",
                            "description": "Name of the member to access"
                        },
                        "access_type": {
                            "type": "string",
                            "enum": ["read", "write"],
                            "description": "Type of access (read or write)"
                        }
                    },
                    "required": ["struct_name", "member_name"]
                }
            }
        },
        {
            "type": "function",
            "function": {
                "name": "compare_binary_versions",
                "description": "Compare two versions of a decompiled function to identify changes",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "old_decompilation": {
                            "type": "string",
                            "description": "Decompiled code from old binary"
                        },
                        "new_decompilation": {
                            "type": "string",
                            "description": "Decompiled code from new binary"
                        },
                        "function_name": {
                            "type": "string",
                            "description": "Name of the function"
                        }
                    },
                    "required": ["old_decompilation", "new_decompilation", "function_name"]
                }
            }
        },
        {
            "type": "function",
            "function": {
                "name": "list_binaries",
                "description": "List available binaries in Binary Ninja MCP servers",
                "parameters": {
                    "type": "object",
                    "properties": {}
                }
            }
        }
    ]
    
    # Create the assistant
    assistant = client.beta.assistants.create(
        name="MIPS Reverse Engineering Expert",
        instructions=instructions,
        tools=tools,
        model="gpt-4o"
    )
    
    print(f"✓ Created assistant: {assistant.id}")
    print(f"  Name: {assistant.name}")
    print(f"  Model: {assistant.model}")
    print(f"  Tools: {len(assistant.tools)} custom functions")
    
    # Save assistant ID to file
    config = {
        "assistant_id": assistant.id,
        "name": assistant.name,
        "model": assistant.model,
        "created_at": assistant.created_at
    }
    
    config_file = "assistant_config.json"
    with open(config_file, 'w') as f:
        json.dump(config, f, indent=2)
    
    print(f"\n✓ Saved configuration to {config_file}")
    print(f"\nYou can now interact with this assistant using:")
    print(f"  - OpenAI Playground: https://platform.openai.com/playground")
    print(f"  - Assistant ID: {assistant.id}")
    print(f"  - Or use the chat_with_assistant.py script")
    
    return assistant


def main():
    """Main entry point"""
    print("="*70)
    print("Creating MIPS Reverse Engineering Assistant")
    print("="*70)
    print()
    
    if not os.getenv("OPENAI_API_KEY"):
        print("Error: OPENAI_API_KEY environment variable not set")
        print("Please set it with: export OPENAI_API_KEY='your-key-here'")
        return 1
    
    try:
        assistant = create_re_assistant()
        return 0
    except Exception as e:
        print(f"Error creating assistant: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    import sys
    sys.exit(main())

