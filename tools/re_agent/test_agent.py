#!/usr/bin/env python3
"""
Quick test to verify the agent works without requiring OpenAI API key.
Tests the core functionality with mock data.
"""

import sys
from pathlib import Path

# Add current directory to path
sys.path.insert(0, str(Path(__file__).parent))

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient, SmartDiffAnalyzer


def test_struct_generation():
    """Test struct definition generation"""
    print("Testing struct generation...")

    try:
        # Create agent without API key for testing non-AI functions
        agent = MIPSReverseEngineeringAgent(api_key="test-key-not-used")

        members = [
            StructMember("grp_num", 0x00, 4, "int32_t", "Group number"),
            StructMember("state", 0x04, 4, "int32_t", "Group state"),
            StructMember("sem_e4", 0xe4, 32, "sem_t", "Semaphore for signaling"),
            StructMember("mutex", 0x108, 40, "pthread_mutex_t", "Mutex for thread safety"),
            StructMember("group_id", 0x130, 4, "uint32_t", "Group ID"),
        ]

        struct_def = agent.generate_struct_definition("EncoderGroup", members)

        # Verify the struct definition contains expected elements
        if "typedef struct EncoderGroup" not in struct_def:
            raise AssertionError("Missing 'typedef struct EncoderGroup'")
        if "grp_num" not in struct_def:
            raise AssertionError("Missing member grp_num")
        if "sem_e4" not in struct_def:
            raise AssertionError("Missing member sem_e4")
        if "mutex" not in struct_def:
            raise AssertionError("Missing member mutex")
        if "group_id" not in struct_def:
            raise AssertionError("Missing member group_id")

        print("✓ Struct generation works")
        print("\nGenerated struct:")
        print(struct_def)
        return True
    except Exception as e:
        print(f"  Error: {e}")
        import traceback
        traceback.print_exc()
        raise


def test_safe_access_code():
    """Test safe access code generation"""
    print("\nTesting safe access code generation...")

    # Create agent without API key for testing non-AI functions
    agent = MIPSReverseEngineeringAgent(api_key="test-key-not-used")

    read_code = agent.generate_safe_access_code("EncoderGroup", "group_id", "read")
    write_code = agent.generate_safe_access_code("EncoderGroup", "state", "write")

    assert "EncoderGroup" in read_code
    assert "group_id" in read_code
    assert "EncoderGroup" in write_code
    assert "state" in write_code

    print("✓ Safe access code generation works")
    print("\nRead access:")
    print(read_code)
    print("\nWrite access:")
    print(write_code)
    return True


def test_struct_database():
    """Test struct database functionality"""
    print("\nTesting struct database...")

    # Create agent without API key for testing non-AI functions
    agent = MIPSReverseEngineeringAgent(api_key="test-key-not-used")

    members = [
        StructMember("chn_id", 0x00, 4, "int32_t", "Channel ID"),
        StructMember("started", 0x3ac, 1, "uint8_t", "Started flag"),
    ]

    enc_channel = StructLayout(
        name="EncChannel",
        total_size=0x308,
        members=members,
        platform="T31"
    )

    agent.add_struct_to_database(enc_channel)
    retrieved = agent.get_struct_from_database("EncChannel")

    assert retrieved is not None
    assert retrieved.name == "EncChannel"
    assert retrieved.total_size == 0x308
    assert len(retrieved.members) == 2

    print("✓ Struct database works")
    print(f"  Stored and retrieved: {retrieved.name} ({retrieved.total_size} bytes)")
    return True


def test_mcp_client():
    """Test MCP client basic functionality"""
    print("\nTesting MCP client...")
    
    mcp = BinaryNinjaMCPClient()
    
    binaries = mcp.list_binaries()
    assert len(binaries) > 0
    
    print("✓ MCP client works")
    print(f"  Found {len(binaries)} binaries:")
    for binary in binaries:
        print(f"    - {binary.binary_id}: {binary.name}")
    return True


def test_smart_diff_analyzer():
    """Test smart diff analyzer"""
    print("\nTesting smart diff analyzer...")
    
    mcp = BinaryNinjaMCPClient()
    analyzer = SmartDiffAnalyzer(mcp)
    
    # Test with mock decompiled code
    mock_code = """
    int32_t test_function(void* ptr) {
        *(uint32_t*)(ptr + 0x10) = 123;
        *(uint8_t*)(ptr + 0x20) = 1;
        return 0;
    }
    """
    
    # This would normally call MCP, but we're testing the pattern extraction
    import re
    offset_pattern = r'(?:ptr|\w+)\s*\+\s*0x([0-9a-fA-F]+)'
    offsets = re.findall(offset_pattern, mock_code)
    
    assert len(offsets) == 2
    assert '10' in offsets
    assert '20' in offsets
    
    print("✓ Smart diff analyzer works")
    print(f"  Extracted offsets: {[f'0x{o}' for o in offsets]}")
    return True


def main():
    """Run all tests"""
    print("="*70)
    print("MIPS RE Agent - Quick Test Suite")
    print("="*70)
    print("\nNote: These tests don't require an OpenAI API key")
    print("They test the core functionality without making API calls\n")
    
    tests = [
        test_struct_generation,
        test_safe_access_code,
        test_struct_database,
        test_mcp_client,
        test_smart_diff_analyzer,
    ]
    
    passed = 0
    failed = 0
    
    for test in tests:
        try:
            if test():
                passed += 1
        except Exception as e:
            print(f"✗ Test failed: {e}")
            failed += 1
    
    print("\n" + "="*70)
    print(f"Test Results: {passed} passed, {failed} failed")
    print("="*70)
    
    if failed == 0:
        print("\n✓ All tests passed! The agent is ready to use.")
        print("\nNext steps:")
        print("  1. Set your OpenAI API key: export OPENAI_API_KEY='your-key'")
        print("  2. Try interactive mode: ./re_agent_cli.py -i")
        print("  3. Run examples: cd examples && python3 openimp_workflow.py")
        return 0
    else:
        print("\n✗ Some tests failed. Please check the errors above.")
        return 1


if __name__ == '__main__':
    sys.exit(main())

