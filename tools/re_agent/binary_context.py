#!/usr/bin/env python3
"""
Binary Context Manager

Manages context about what exists in the binary vs what's being implemented.
Helps the AI understand:
- Which functions exist in the OEM binary (should be reverse-engineered)
- Which functions are new implementations (don't exist in binary)
- Call chains and dependencies
"""

import sys
import json
from pathlib import Path
from typing import Dict, List, Set, Optional, Tuple
from dataclasses import dataclass, asdict

sys.path.insert(0, str(Path(__file__).parent))

from binja_mcp_client import BinaryNinjaMCPClient


@dataclass
class FunctionContext:
    """Context about a function"""
    name: str
    exists_in_binary: bool
    binary_id: Optional[str] = None
    address: Optional[int] = None
    callers: List[str] = None
    callees: List[str] = None
    is_wrapper: bool = False  # True if this is a wrapper around binary function
    notes: str = ""
    
    def __post_init__(self):
        if self.callers is None:
            self.callers = []
        if self.callees is None:
            self.callees = []


class BinaryContextManager:
    """Manages context about binary functions and call chains"""
    
    def __init__(self, binary_id: str = "port_9009"):
        """Initialize with a binary
        
        Args:
            binary_id: Binary Ninja MCP server ID (e.g., port_9009)
        """
        self.binary_id = binary_id
        self.mcp = BinaryNinjaMCPClient()
        self.functions: Dict[str, FunctionContext] = {}
        self.binary_functions: Set[str] = set()
        
    def load_binary_functions(self) -> int:
        """Load all functions from the binary

        Returns:
            Number of functions loaded
        """
        print(f"Loading functions from binary {self.binary_id}...")

        try:
            # Call Binary Ninja MCP to get function list
            binary_funcs = self.mcp.list_functions(self.binary_id)

            for func_name in binary_funcs:
                self.binary_functions.add(func_name)
                self.functions[func_name] = FunctionContext(
                    name=func_name,
                    exists_in_binary=True,
                    binary_id=self.binary_id
                )

            print(f"  Loaded {len(self.binary_functions)} functions from binary")
            return len(self.binary_functions)

        except Exception as e:
            print(f"  Warning: Could not load binary functions: {e}")
            print(f"  Continuing without binary context (will process all functions)")
            return 0
    
    def analyze_call_chain(self, function_name: str, max_depth: int = 3) -> Dict[str, any]:
        """Analyze the call chain for a function
        
        Args:
            function_name: Function to analyze
            max_depth: Maximum depth to follow call chain
            
        Returns:
            Dict with call chain analysis
        """
        if function_name not in self.functions:
            # Check if it exists in binary
            if function_name in self.binary_functions:
                ctx = self.functions[function_name]
            else:
                # New function being implemented
                ctx = FunctionContext(
                    name=function_name,
                    exists_in_binary=False
                )
                self.functions[function_name] = ctx
        else:
            ctx = self.functions[function_name]
        
        # Get decompilation to find callees
        if ctx.exists_in_binary:
            decompiled = self.mcp.decompile_function(self.binary_id, function_name)
            if decompiled:
                # Extract function calls from decompiled code
                callees = self._extract_callees(decompiled)
                ctx.callees = callees
        
        # Build call chain
        call_chain = {
            "function": function_name,
            "exists_in_binary": ctx.exists_in_binary,
            "callees": [],
            "depth": 0
        }
        
        if max_depth > 0 and ctx.callees:
            for callee in ctx.callees:
                callee_chain = self.analyze_call_chain(callee, max_depth - 1)
                callee_chain["depth"] = 1
                call_chain["callees"].append(callee_chain)
        
        return call_chain
    
    def _extract_callees(self, decompiled_code: str) -> List[str]:
        """Extract function calls from decompiled code
        
        Args:
            decompiled_code: Decompiled C code
            
        Returns:
            List of function names called
        """
        import re
        
        # Find function calls: function_name(...)
        pattern = r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\('
        matches = re.findall(pattern, decompiled_code)
        
        # Filter out common C keywords and operators
        keywords = {'if', 'while', 'for', 'switch', 'sizeof', 'return', 'malloc', 'free', 'memcpy', 'memset'}
        callees = [m for m in matches if m not in keywords]
        
        # Deduplicate
        return list(set(callees))
    
    def should_skip_function(self, function_name: str) -> bool:
        """Check if a function should be skipped (doesn't exist in binary)

        Args:
            function_name: Function to check

        Returns:
            True if function should be skipped
        """
        # If we have no binary functions loaded, don't skip anything
        # (fail-safe: process everything if binary context unavailable)
        if not self.binary_functions:
            return False

        ctx = self.functions.get(function_name)
        if not ctx:
            # Check if it exists in binary
            if function_name in self.binary_functions:
                # Create context entry for it
                self.functions[function_name] = FunctionContext(
                    name=function_name,
                    exists_in_binary=True,
                    binary_id=self.binary_id
                )
                return False  # Exists, don't skip
            else:
                return True  # Doesn't exist, skip it

        return not ctx.exists_in_binary

    def get_implementation_guidance(self, function_name: str, existing_code: str = "") -> str:
        """Get guidance for implementing a function

        Args:
            function_name: Function to get guidance for
            existing_code: Existing implementation code (if any)

        Returns:
            Guidance string for the AI
        """
        ctx = self.functions.get(function_name)

        if not ctx:
            return f"Function '{function_name}' not found in binary or context."

        guidance = []

        if ctx.exists_in_binary:
            guidance.append(f"✓ Function '{function_name}' EXISTS in OEM binary {self.binary_id}")
            guidance.append("  → REVERSE-ENGINEER this from the binary decompilation")
            guidance.append("  → Use Binary Ninja MCP to get the decompilation")
            guidance.append("  → Extract struct offsets and generate safe implementation")
            guidance.append("  → Match the OEM behavior exactly")
        else:
            guidance.append(f"✗ Function '{function_name}' DOES NOT EXIST in OEM binary")
            guidance.append("  → SKIP THIS FUNCTION - it probably shouldn't exist")
            guidance.append("  → This may be dead code or incorrect abstraction")
            guidance.append("  → Callers should call OEM functions directly instead")

            # Analyze existing code to understand intent
            if existing_code:
                import re

                # Find OEM function calls
                oem_calls = []
                for callee in ctx.callees:
                    callee_ctx = self.functions.get(callee)
                    if callee_ctx and callee_ctx.exists_in_binary:
                        oem_calls.append(callee)

                if oem_calls:
                    guidance.append(f"\n  → This function calls {len(oem_calls)} OEM functions:")
                    for oem_func in oem_calls:
                        guidance.append(f"     • {oem_func} (from binary)")
                    guidance.append("  → Ensure these calls are correct and safe")

                # Check for unsafe patterns
                if "*(uint32_t*)" in existing_code or "*(int32_t*)" in existing_code:
                    guidance.append("\n  ⚠️  UNSAFE PATTERN DETECTED: Direct pointer arithmetic")
                    guidance.append("  → Replace with typed struct access")

                # Check for struct access
                if "->" in existing_code:
                    guidance.append("\n  → Uses struct member access")
                    guidance.append("  → Verify struct definitions are correct")

        if ctx.callees:
            guidance.append(f"\nCall chain analysis:")
            for callee in ctx.callees:
                callee_ctx = self.functions.get(callee)
                if callee_ctx and callee_ctx.exists_in_binary:
                    guidance.append(f"  • {callee} (OEM function - reverse-engineer separately)")
                elif callee_ctx:
                    guidance.append(f"  • {callee} (your implementation)")
                else:
                    guidance.append(f"  • {callee} (unknown - check if in binary)")

        return "\n".join(guidance)
    
    def get_functions_to_implement(self, start_function: str) -> List[Tuple[str, bool]]:
        """Get ordered list of functions to implement based on call chain
        
        Args:
            start_function: Starting function
            
        Returns:
            List of (function_name, exists_in_binary) tuples in dependency order
        """
        # Analyze call chain
        call_chain = self.analyze_call_chain(start_function, max_depth=5)
        
        # Flatten to dependency order (leaves first)
        to_implement = []
        visited = set()
        
        def visit(chain):
            func_name = chain["function"]
            if func_name in visited:
                return
            
            # Visit callees first (dependencies)
            for callee_chain in chain.get("callees", []):
                visit(callee_chain)
            
            # Then add this function
            ctx = self.functions.get(func_name)
            if ctx:
                to_implement.append((func_name, ctx.exists_in_binary))
                visited.add(func_name)
        
        visit(call_chain)
        return to_implement
    
    def save_context(self, output_file: str):
        """Save context to JSON file
        
        Args:
            output_file: Path to output file
        """
        data = {
            "binary_id": self.binary_id,
            "functions": {name: asdict(ctx) for name, ctx in self.functions.items()},
            "binary_functions": list(self.binary_functions)
        }
        
        with open(output_file, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"Saved context to {output_file}")
    
    def load_context(self, input_file: str):
        """Load context from JSON file
        
        Args:
            input_file: Path to input file
        """
        with open(input_file, 'r') as f:
            data = json.load(f)
        
        self.binary_id = data["binary_id"]
        self.binary_functions = set(data["binary_functions"])
        
        self.functions = {}
        for name, ctx_dict in data["functions"].items():
            self.functions[name] = FunctionContext(**ctx_dict)
        
        print(f"Loaded context from {input_file}")
        print(f"  Binary: {self.binary_id}")
        print(f"  Functions: {len(self.functions)}")


def main():
    """Test the binary context manager"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Binary context manager")
    parser.add_argument("--binary", "-b", default="port_9009", help="Binary ID")
    parser.add_argument("--function", "-f", help="Function to analyze")
    parser.add_argument("--load", help="Load context from file")
    parser.add_argument("--save", help="Save context to file")
    
    args = parser.parse_args()
    
    ctx_mgr = BinaryContextManager(binary_id=args.binary)
    
    if args.load:
        ctx_mgr.load_context(args.load)
    else:
        ctx_mgr.load_binary_functions()
    
    if args.function:
        print(f"\nAnalyzing function: {args.function}")
        print("="*80)
        
        # Get guidance
        guidance = ctx_mgr.get_implementation_guidance(args.function)
        print(guidance)
        
        # Get call chain
        print("\n\nCall chain analysis:")
        print("-"*80)
        call_chain = ctx_mgr.analyze_call_chain(args.function)
        print(json.dumps(call_chain, indent=2))
        
        # Get implementation order
        print("\n\nImplementation order (dependencies first):")
        print("-"*80)
        to_implement = ctx_mgr.get_functions_to_implement(args.function)
        for i, (func, in_binary) in enumerate(to_implement, 1):
            status = "RE from binary" if in_binary else "New implementation"
            print(f"{i}. {func} ({status})")
    
    if args.save:
        ctx_mgr.save_context(args.save)


if __name__ == "__main__":
    main()

