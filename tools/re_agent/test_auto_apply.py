#!/usr/bin/env python3
"""
Test script for auto-apply feature.
This simulates the auto-apply workflow without actually calling Auggie.
"""

import sys
import json
from pathlib import Path

# Add tools/re_agent to path
sys.path.insert(0, str(Path(__file__).parent))

from auggie_apply import apply_function_implementation, apply_struct_update, find_auggie

def test_find_auggie():
    """Test if Auggie CLI can be found"""
    print("Testing Auggie CLI detection...")
    auggie_path = find_auggie()
    if auggie_path:
        print(f"  ✓ Auggie found at: {auggie_path}")
        return True
    else:
        print(f"  ✗ Auggie not found")
        return False

def test_artifact_loading():
    """Test loading artifacts"""
    print("\nTesting artifact loading...")
    
    # Create a test artifact
    test_artifact = {
        "function_name": "IMP_ISP_AddSensor",
        "implementation": "int IMP_ISP_AddSensor(void) { return 0; }",
        "struct_definitions": [],
        "notes": "Test implementation",
        "issues_found": []
    }
    
    test_dir = Path("tools/re_agent/test_artifacts")
    test_dir.mkdir(exist_ok=True)
    
    test_file = test_dir / "IMP_ISP_AddSensor.json"
    with open(test_file, 'w') as f:
        json.dump(test_artifact, f, indent=2)
    
    print(f"  ✓ Created test artifact: {test_file}")
    
    # Try to load it
    try:
        with open(test_file, 'r') as f:
            loaded = json.load(f)
        print(f"  ✓ Loaded artifact successfully")
        print(f"    Function: {loaded['function_name']}")
        return True
    except Exception as e:
        print(f"  ✗ Failed to load artifact: {e}")
        return False
    finally:
        # Clean up
        test_file.unlink()
        test_dir.rmdir()

def test_return_types():
    """Test that functions return tuples"""
    print("\nTesting return types...")
    
    # Create a test artifact
    test_artifact = {
        "function_name": "IMP_ISP_AddSensor",
        "implementation": "int IMP_ISP_AddSensor(void) { return 0; }",
        "struct_definitions": [],
        "notes": "Test implementation",
        "issues_found": []
    }
    
    test_dir = Path("tools/re_agent/test_artifacts")
    test_dir.mkdir(exist_ok=True)
    
    test_file = test_dir / "IMP_ISP_AddSensor.json"
    with open(test_file, 'w') as f:
        json.dump(test_artifact, f, indent=2)
    
    try:
        # Test with string path
        result = apply_function_implementation(str(test_file), dry_run=True)
        if isinstance(result, tuple) and len(result) == 2:
            success, msg = result
            print(f"  ✓ apply_function_implementation returns tuple: ({success}, '{msg[:50]}...')")
            return True
        else:
            print(f"  ✗ apply_function_implementation returns wrong type: {type(result)}")
            return False
    except Exception as e:
        print(f"  ✗ Error calling apply_function_implementation: {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        # Clean up
        test_file.unlink()
        test_dir.rmdir()

def main():
    print("=" * 60)
    print("Auto-Apply Feature Test")
    print("=" * 60)
    
    results = []
    
    # Test 1: Find Auggie
    results.append(("Find Auggie", test_find_auggie()))
    
    # Test 2: Artifact loading
    results.append(("Artifact Loading", test_artifact_loading()))
    
    # Test 3: Return types
    results.append(("Return Types", test_return_types()))
    
    # Summary
    print("\n" + "=" * 60)
    print("Test Summary")
    print("=" * 60)
    
    for name, passed in results:
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"  {status}: {name}")
    
    total = len(results)
    passed = sum(1 for _, p in results if p)
    
    print(f"\nTotal: {passed}/{total} tests passed")
    
    if passed == total:
        print("\n✅ All tests passed!")
        return 0
    else:
        print(f"\n⚠ {total - passed} test(s) failed")
        return 1

if __name__ == "__main__":
    sys.exit(main())

