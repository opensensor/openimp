#!/usr/bin/env python3
"""
Example: OpenIMP Development Workflow

This example demonstrates a complete workflow for using the RE Agent
to develop OpenIMP implementations based on Binary Ninja decompilations.

This follows the actual patterns used in the OpenIMP project.
"""

import sys
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout


def main():
    """Complete OpenIMP development workflow"""
    print("="*70)
    print("OpenIMP Development Workflow with RE Agent")
    print("="*70)
    
    # Initialize the agent
    print("\n1. Initializing agent...")
    agent = MIPSReverseEngineeringAgent(model="gpt-4o")
    
    # Scenario: We need to implement IMP_Encoder_GetStream
    # We have the decompilation from Binary Ninja MCP
    
    print("\n2. Analyzing IMP_Encoder_GetStream decompilation...")
    
    # Real decompilation from Binary Ninja (T31 v1.1.6)
    decompiled_code = """
int32_t IMP_Encoder_GetStream_Impl(int32_t encChn, IMPEncoderStream* stream, int32_t blockFlag) {
    // Validate channel
    if (encChn >= 4) {
        return -1;
    }
    
    // Get channel pointer from global array
    void* channel = g_encoder_channels[encChn];
    if (!channel) {
        return -1;
    }
    
    // Check if channel is started (offset 0x3ac)
    uint8_t started = *(uint8_t*)(channel + 0x3ac);
    if (!started) {
        return -1;
    }
    
    // Get stream buffer metadata
    // Buffer size is at offset 0x1094c4
    uint32_t buf_size = *(uint32_t*)(channel + 0x1094c4);
    
    // Stream buffer metadata is 0x188 bytes per entry
    // Index is stored at offset 0x1094d8
    int32_t index = *(int32_t*)(channel + 0x1094d8);
    
    // Calculate stream buffer pointer
    void* stream_buf = (void*)(channel + 0x1000 + index * 0x188);
    
    // Copy stream data
    memcpy(stream, stream_buf, sizeof(IMPEncoderStream));
    
    // Update index (circular buffer)
    index = (index + 1) % 16;
    *(int32_t*)(channel + 0x1094d8) = index;
    
    return 0;
}
"""
    
    result = agent.analyze_decompilation(decompiled_code, "IMP_Encoder_GetStream")
    
    print("\nAnalysis complete!")
    print(f"  Found {len(result.get('offsets', []))} struct offsets")
    
    # Step 3: Ask the agent to help us understand the structure
    print("\n3. Understanding the EncChannel structure...")
    
    question = """
    Based on the decompilation, I can see:
    - Offset 0x3ac: started flag (uint8_t)
    - Offset 0x1094c4: buffer size (uint32_t)
    - Offset 0x1094d8: current index (int32_t)
    - Offset 0x1000: start of stream buffer array
    - Each stream buffer entry is 0x188 bytes
    - Maximum 16 entries in circular buffer
    
    Can you help me:
    1. Generate a proper EncChannel struct definition
    2. Create safe access code that avoids direct pointer arithmetic
    3. Suggest how to handle the circular buffer safely
    """
    
    response = agent.ask(question)
    print("\nAgent response:")
    print(response[:800] + "..." if len(response) > 800 else response)
    
    # Step 4: Generate the struct definition
    print("\n4. Generating EncChannel struct definition...")
    
    members = [
        StructMember("chn_id", 0x00, 4, "int32_t", "Channel ID, -1 = unused"),
        StructMember("registered", 0x398, 1, "uint8_t", "Channel registered flag"),
        StructMember("started", 0x3ac, 1, "uint8_t", "Channel started flag"),
        StructMember("recv_pic_enabled", 0x400, 1, "uint8_t", "Receive picture enabled"),
        StructMember("recv_pic_started", 0x404, 1, "uint8_t", "Receive picture started"),
        StructMember("stream_buffers", 0x1000, 0x188 * 16, "uint8_t[0x188 * 16]", 
                    "Stream buffer array (16 entries of 0x188 bytes)"),
        StructMember("buf_size", 0x1094c4, 4, "uint32_t", "Stream buffer size"),
        StructMember("buf_index", 0x1094d8, 4, "int32_t", "Current buffer index"),
    ]
    
    struct_def = agent.generate_struct_definition("EncChannel", members)
    print("\nGenerated struct:")
    print(struct_def)
    
    # Step 5: Generate safe implementation
    print("\n5. Generating safe implementation...")
    
    implementation_request = """
    Now generate a safe C implementation of IMP_Encoder_GetStream that:
    1. Uses the EncChannel struct definition (not pointer arithmetic)
    2. Properly validates inputs
    3. Uses memcpy for safe buffer access
    4. Handles the circular buffer correctly
    5. Includes error checking
    6. Follows OpenIMP coding style
    
    The function signature is:
    int IMP_Encoder_GetStream(int encChn, IMPEncoderStream *stream, int blockFlag);
    """
    
    response = agent.ask(implementation_request)
    print("\nGenerated implementation:")
    print(response[:1000] + "..." if len(response) > 1000 else response)
    
    # Step 6: Ask about platform differences
    print("\n6. Checking for platform differences...")
    
    platform_question = """
    Are there any known differences in the EncChannel structure between
    T31, T23, T40, and T41 platforms? Should we add any platform-specific
    conditional compilation directives?
    """
    
    response = agent.ask(platform_question)
    print("\nPlatform analysis:")
    print(response[:600] + "..." if len(response) > 600 else response)
    
    # Step 7: Store in database for future reference
    print("\n7. Storing struct in database...")
    
    enc_channel = StructLayout(
        name="EncChannel",
        total_size=0x1094dc,  # Last offset + size
        members=members,
        platform="T31"
    )
    
    agent.add_struct_to_database(enc_channel)
    print(f"  Stored EncChannel ({enc_channel.total_size} bytes)")
    
    # Step 8: Generate test recommendations
    print("\n8. Getting test recommendations...")
    
    test_question = """
    What tests should we write to verify our IMP_Encoder_GetStream implementation?
    Consider:
    - Edge cases (invalid channel, not started, etc.)
    - Circular buffer wraparound
    - Thread safety
    - Memory safety
    """
    
    response = agent.ask(test_question)
    print("\nTest recommendations:")
    print(response[:600] + "..." if len(response) > 600 else response)
    
    # Summary
    print("\n" + "="*70)
    print("Workflow Summary")
    print("="*70)
    print("""
    ✓ Analyzed Binary Ninja decompilation
    ✓ Identified struct offsets and layout
    ✓ Generated safe struct definition
    ✓ Created safe implementation code
    ✓ Checked platform differences
    ✓ Stored struct in database
    ✓ Got test recommendations
    
    Next steps:
    1. Review generated code
    2. Add to src/imp_encoder.c
    3. Write tests
    4. Verify against actual hardware
    5. Document in REVERSE_ENGINEERING_SUMMARY.md
    """)
    
    print("="*70)


if __name__ == '__main__':
    main()

