#!/usr/bin/env python3
"""
Augment Tool Wrapper for MIPS RE Agent

This provides a simple command-line interface that Augment can call
using the launch-process tool.

Usage from Augment:
    launch-process: python tools/re_agent/augment_tool.py analyze "decompiled_code" "function_name"
    launch-process: python tools/re_agent/augment_tool.py struct "struct_name" '{"members": [...]}'
    launch-process: python tools/re_agent/augment_tool.py ask "your question here"
"""

import sys
import json
import os
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient


def print_json(data):
    """Print data as formatted JSON"""
    print(json.dumps(data, indent=2))


def cmd_analyze(decompiled_code: str, function_name: str):
    """Analyze decompiled code"""
    agent = MIPSReverseEngineeringAgent()
    result = agent.analyze_decompilation(decompiled_code, function_name)
    print_json(result)


def cmd_struct(struct_name: str, members_json: str):
    """Generate struct definition"""
    agent = MIPSReverseEngineeringAgent()
    
    # Parse members JSON
    members_data = json.loads(members_json)
    members = [
        StructMember(
            name=m["name"],
            offset=m["offset"],
            size=m["size"],
            type_name=m["type_name"],
            description=m.get("description", "")
        )
        for m in members_data
    ]
    
    struct_def = agent.generate_struct_definition(struct_name, members)
    print(struct_def)


def cmd_safe_access(struct_name: str, member_name: str, access_type: str = "read"):
    """Generate safe access code"""
    agent = MIPSReverseEngineeringAgent()
    code = agent.generate_safe_access_code(struct_name, member_name, access_type)
    print(code)


def cmd_compare(old_code: str, new_code: str, function_name: str):
    """Compare two versions of a function"""
    agent = MIPSReverseEngineeringAgent()
    result = agent.compare_binary_versions(old_code, new_code, function_name)
    print_json(result)


def cmd_ask(question: str):
    """Ask the agent a question"""
    agent = MIPSReverseEngineeringAgent()
    response = agent.ask(question)
    print(response)


def cmd_list_binaries():
    """List available binaries"""
    mcp = BinaryNinjaMCPClient()
    binaries = mcp.list_binaries()
    
    result = {
        "binaries": [
            {
                "binary_id": b.binary_id,
                "name": b.name,
                "architecture": b.architecture
            }
            for b in binaries
        ]
    }
    print_json(result)


def show_usage():
    """Show usage information"""
    usage = """
MIPS RE Agent - Augment Tool Wrapper

Usage:
    python augment_tool.py <command> [args...]

Commands:
    analyze <decompiled_code> <function_name>
        Analyze decompiled code and extract struct information
        
    struct <struct_name> <members_json>
        Generate struct definition from members
        Example members_json: '[{"name":"field","offset":0,"size":4,"type_name":"uint32_t"}]'
        
    safe-access <struct_name> <member_name> [read|write]
        Generate safe access code for a struct member
        
    compare <old_code> <new_code> <function_name>
        Compare two versions of a function
        
    ask <question>
        Ask the agent a question
        
    list-binaries
        List available binaries in Binary Ninja MCP

Examples:
    # Analyze decompiled code
    python augment_tool.py analyze "int foo() { return 0; }" "foo"
    
    # Generate struct
    python augment_tool.py struct "MyStruct" '[{"name":"id","offset":0,"size":4,"type_name":"int32_t"}]'
    
    # Ask a question
    python augment_tool.py ask "How should I handle offset 0x3ac in EncChannel?"
    
    # List binaries
    python augment_tool.py list-binaries

Environment:
    OPENAI_API_KEY - Required for AI-powered features
"""
    print(usage)


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        show_usage()
        return 1
    
    command = sys.argv[1].lower()
    
    try:
        if command == "analyze":
            if len(sys.argv) < 4:
                print("Error: analyze requires <decompiled_code> <function_name>")
                return 1
            cmd_analyze(sys.argv[2], sys.argv[3])
            
        elif command == "struct":
            if len(sys.argv) < 4:
                print("Error: struct requires <struct_name> <members_json>")
                return 1
            cmd_struct(sys.argv[2], sys.argv[3])
            
        elif command == "safe-access":
            if len(sys.argv) < 4:
                print("Error: safe-access requires <struct_name> <member_name> [access_type]")
                return 1
            access_type = sys.argv[4] if len(sys.argv) > 4 else "read"
            cmd_safe_access(sys.argv[2], sys.argv[3], access_type)
            
        elif command == "compare":
            if len(sys.argv) < 5:
                print("Error: compare requires <old_code> <new_code> <function_name>")
                return 1
            cmd_compare(sys.argv[2], sys.argv[3], sys.argv[4])
            
        elif command == "ask":
            if len(sys.argv) < 3:
                print("Error: ask requires <question>")
                return 1
            # Join all remaining args as the question
            question = " ".join(sys.argv[2:])
            cmd_ask(question)
            
        elif command == "list-binaries":
            cmd_list_binaries()
            
        elif command in ["help", "-h", "--help"]:
            show_usage()
            
        else:
            print(f"Error: Unknown command '{command}'")
            show_usage()
            return 1
            
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc(file=sys.stderr)
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())

