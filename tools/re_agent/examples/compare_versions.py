#!/usr/bin/env python3
"""
Example: Comparing Binary Versions

This example demonstrates how to compare different versions of the same
function to identify API changes and struct layout modifications.
"""

import sys
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from mips_re_agent import MIPSReverseEngineeringAgent
from binja_mcp_client import BinaryNinjaMCPClient


def main():
    """Main example workflow"""
    print("="*70)
    print("Example: Comparing Binary Versions")
    print("="*70)
    
    # Initialize the agent
    print("\n1. Initializing agent...")
    agent = MIPSReverseEngineeringAgent(model="gpt-4o")
    mcp = BinaryNinjaMCPClient()
    
    # Example: Compare IMP_System_Bind between T31 and T23
    print("\n2. Comparing IMP_System_Bind between T31 v1.1.6 and T23...")
    
    # T31 version (v1.1.6)
    t31_code = """
int32_t IMP_System_Bind(IMPCell* srcCell, IMPCell* dstCell) {
    if (!srcCell || !dstCell) {
        return -1;
    }
    
    // Validate device IDs
    if (srcCell->deviceID >= 8 || dstCell->deviceID >= 8) {
        return -1;
    }
    
    // Get modules from registry
    void* src_module = g_modules[srcCell->deviceID][srcCell->groupID];
    void* dst_module = g_modules[dstCell->deviceID][dstCell->groupID];
    
    if (!src_module || !dst_module) {
        return -1;
    }
    
    // Get bind function from source module at offset 0x40
    void* (*bind_func)(void*, void*, void*) = *(void**)(src_module + 0x40);
    
    if (!bind_func) {
        return -1;
    }
    
    // Get output pointer at offset 0x134 + outputID * 4
    void** output_ptr = (void**)(src_module + 0x134 + srcCell->outputID * 4);
    
    // Call bind function
    return bind_func(src_module, dst_module, output_ptr);
}
"""
    
    # T23 version (hypothetical changes)
    t23_code = """
int32_t IMP_System_Bind(IMPCell* srcCell, IMPCell* dstCell) {
    if (!srcCell || !dstCell) {
        return -1;
    }
    
    // Validate device IDs (T23 has 10 device types)
    if (srcCell->deviceID >= 10 || dstCell->deviceID >= 10) {
        return -1;
    }
    
    // Get modules from registry
    void* src_module = g_modules[srcCell->deviceID][srcCell->groupID];
    void* dst_module = g_modules[dstCell->deviceID][dstCell->groupID];
    
    if (!src_module || !dst_module) {
        return -1;
    }
    
    // Get bind function from source module at offset 0x44 (CHANGED!)
    void* (*bind_func)(void*, void*, void*) = *(void**)(src_module + 0x44);
    
    if (!bind_func) {
        return -1;
    }
    
    // Get output pointer at offset 0x138 + outputID * 4 (CHANGED!)
    void** output_ptr = (void**)(src_module + 0x138 + srcCell->outputID * 4);
    
    // Call bind function
    return bind_func(src_module, dst_module, output_ptr);
}
"""
    
    result = agent.compare_binary_versions(t31_code, t23_code, "IMP_System_Bind")
    
    print("\nComparison Result:")
    print(f"  Offset changes: {result.get('offset_changes', [])}")
    print(f"  Member changes: {result.get('member_changes', [])}")
    print(f"  Logic changes: {result.get('logic_changes', [])}")
    print(f"  API changes: {result.get('api_changes', [])}")
    
    if 'recommendations' in result:
        print(f"\nRecommendations:")
        for rec in result.get('recommendations', []):
            print(f"  - {rec}")
    
    # Example 2: Ask about platform differences
    print("\n3. Asking about platform-specific differences...")
    
    question = """
    Based on the comparison, what are the key differences between T31 and T23
    in the module structure? How should we handle these differences in our
    implementation to maintain compatibility with both platforms?
    """
    
    response = agent.ask(question)
    print("\nAgent response:")
    print(response[:600] + "..." if len(response) > 600 else response)
    
    # Example 3: Analyze struct offset changes
    print("\n4. Analyzing struct offset changes...")
    
    offset_question = """
    I noticed these offset changes between T31 and T23:
    - bind_func: 0x40 -> 0x44 (shifted by 4 bytes)
    - output_ptr base: 0x134 -> 0x138 (shifted by 4 bytes)
    
    This suggests a new 4-byte field was added before offset 0x40.
    Can you help me identify what this field might be and generate
    a platform-conditional struct definition?
    """
    
    response = agent.ask(offset_question)
    print("\nAgent response:")
    print(response[:600] + "..." if len(response) > 600 else response)
    
    print("\n" + "="*70)
    print("Example completed successfully!")
    print("="*70)


if __name__ == '__main__':
    main()

