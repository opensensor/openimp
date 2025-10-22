#!/usr/bin/env python3
"""
Example: Analyzing IMP Encoder Functions

This example demonstrates how to use the MIPS RE Agent to analyze
encoder functions from the Ingenic IMP library.
"""

import sys
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient, SmartDiffAnalyzer


def main():
    """Main example workflow"""
    print("="*70)
    print("Example: Analyzing IMP Encoder Functions")
    print("="*70)
    
    # Initialize the agent
    print("\n1. Initializing agent...")
    agent = MIPSReverseEngineeringAgent(model="gpt-4o")
    mcp = BinaryNinjaMCPClient()
    analyzer = SmartDiffAnalyzer(mcp)
    
    # Example 1: Analyze a decompiled function
    print("\n2. Analyzing IMP_Encoder_CreateGroup...")
    
    # This is example decompiled code from Binary Ninja
    decompiled_code = """
int32_t IMP_Encoder_CreateGroup(int32_t grpNum) {
    if (grpNum >= 4) {
        return -1;
    }
    
    void* group_ptr = malloc(0x308);
    if (!group_ptr) {
        return -1;
    }
    
    memset(group_ptr, 0, 0x308);
    
    // Initialize group
    *(int32_t*)(group_ptr + 0x00) = grpNum;
    *(int32_t*)(group_ptr + 0x04) = 0;  // state
    
    // Initialize semaphore at offset 0xe4
    sem_init((sem_t*)(group_ptr + 0xe4), 0, 0);
    
    // Initialize semaphore at offset 0xf4 with value 16
    sem_init((sem_t*)(group_ptr + 0xf4), 0, 16);
    
    // Initialize mutex at offset 0x108
    pthread_mutex_init((pthread_mutex_t*)(group_ptr + 0x108), NULL);
    
    // Store group_id at offset 0x130
    *(uint32_t*)(group_ptr + 0x130) = grpNum;
    
    // Store in global array
    g_encoder_groups[grpNum] = group_ptr;
    
    return 0;
}
"""
    
    result = agent.analyze_decompilation(decompiled_code, "IMP_Encoder_CreateGroup")
    
    print("\nAnalysis Result:")
    print(f"  Identified offsets: {result.get('offsets', [])}")
    print(f"  Notes: {result.get('notes', 'N/A')[:200]}...")
    
    # Example 2: Generate struct definition
    print("\n3. Generating struct definition for EncoderGroup...")
    
    members = [
        StructMember("grp_num", 0x00, 4, "int32_t", "Group number"),
        StructMember("state", 0x04, 4, "int32_t", "Group state"),
        StructMember("sem_e4", 0xe4, 32, "sem_t", "Semaphore for signaling"),
        StructMember("sem_f4", 0xf4, 32, "sem_t", "Semaphore initialized to 16"),
        StructMember("mutex", 0x108, 40, "pthread_mutex_t", "Mutex for thread safety"),
        StructMember("group_id", 0x130, 4, "uint32_t", "Group ID"),
    ]
    
    struct_def = agent.generate_struct_definition("EncoderGroup", members)
    print("\nGenerated struct:")
    print(struct_def)
    
    # Example 3: Generate safe access code
    print("\n4. Generating safe access code...")
    
    read_code = agent.generate_safe_access_code("EncoderGroup", "group_id", "read")
    print("\nSafe read access:")
    print(read_code)
    
    write_code = agent.generate_safe_access_code("EncoderGroup", "state", "write")
    print("\nSafe write access:")
    print(write_code)
    
    # Example 4: Ask the agent a question
    print("\n5. Asking the agent about struct layout...")
    
    question = """
    I found these offsets in the decompiled IMP_Encoder_GetStream function:
    - ptr + 0x1094d8 (seems to be an index)
    - result * 0x188 (multiplication factor)
    - ptr + 0x1094c4 (buffer size storage)
    
    What does this tell us about the stream buffer structure?
    """
    
    response = agent.ask(question)
    print("\nAgent response:")
    print(response[:500] + "..." if len(response) > 500 else response)
    
    # Example 5: Store struct in database
    print("\n6. Storing struct in database...")
    
    encoder_group = StructLayout(
        name="EncoderGroup",
        total_size=0x308,
        members=members,
        platform="T31"
    )
    
    agent.add_struct_to_database(encoder_group)
    
    retrieved = agent.get_struct_from_database("EncoderGroup")
    print(f"  Stored and retrieved: {retrieved.name} ({retrieved.total_size} bytes)")
    
    print("\n" + "="*70)
    print("Example completed successfully!")
    print("="*70)


if __name__ == '__main__':
    main()

