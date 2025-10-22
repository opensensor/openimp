#!/usr/bin/env python3
"""
Batch Review and Implementation Generator

This agent processes logs, decompilations, and existing implementations to:
1. Review current implementation against Binary Ninja decompilations
2. Identify missing offsets, incorrect struct layouts, unsafe access patterns
3. Generate corrected implementations with safe struct access
4. Expand implementations with missing functionality

Usage:
    python batch_review.py --input logs/session.log --output review_results/
    python batch_review.py --decompile IMP_Encoder_CreateGroup --binary port_9009
    python batch_review.py --review-file src/imp/imp_encoder.c
"""

import os
import sys
import json
import re
import argparse
import time
from pathlib import Path
from typing import List, Dict, Any, Optional, Tuple
from dataclasses import dataclass, asdict

sys.path.insert(0, str(Path(__file__).parent))

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient
from binary_context import BinaryContextManager


@dataclass
class ReviewResult:
    """Result of reviewing a function/struct"""
    function_name: str
    issues_found: List[str]
    struct_definitions: List[str]
    corrected_implementation: str
    safe_access_patterns: List[str]
    notes: str


class BatchReviewAgent:
    """Agent for batch processing and reviewing implementations"""

    def __init__(self, output_dir: str = "review_results", apply_fixes: bool = False, binary_id: str = "port_9009"):
        """Initialize the batch review agent

        Args:
            output_dir: Directory for reports
            apply_fixes: If True, directly edit source files with corrections
            binary_id: Binary Ninja MCP server ID for context
        """
        self.agent = MIPSReverseEngineeringAgent()
        self.mcp = BinaryNinjaMCPClient()
        self.binary_context = BinaryContextManager(binary_id=binary_id)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.apply_fixes = apply_fixes

        # Load binary context
        print(f"Loading binary context from {binary_id}...")
        try:
            self.binary_context.load_binary_functions()
        except Exception as e:
            print(f"  Warning: Could not load binary functions: {e}")

        self.results: List[ReviewResult] = []
        self.files_modified: List[str] = []

    def process_log_file(self, log_file: str) -> List[ReviewResult]:
        """
        Process a log file to extract decompilations and generate implementations.

        Args:
            log_file: Path to log file containing decompilations

        Returns:
            List of review results
        """
        print(f"Processing log file: {log_file}")

        with open(log_file, 'r') as f:
            content = f.read()

        # Extract decompilations from log (looking for common patterns)
        decompilations = self._extract_decompilations_from_log(content)

        print(f"Found {len(decompilations)} decompilations in log")

        for func_name, code in decompilations.items():
            print(f"\nProcessing: {func_name}")
            result = self.review_decompilation(func_name, code)
            self.results.append(result)

        return self.results

    def _extract_decompilations_from_log(self, content: str) -> Dict[str, str]:
        """Extract decompilations from log content"""
        decompilations = {}

        # Look for common patterns in logs
        # Pattern 1: Function name followed by decompiled code
        import re

        # Match function definitions
        pattern = r'(?:Function:|Decompiling:|Analyzing:)\s*(\w+)\s*\n((?:.*\n)*?)(?=\n(?:Function:|Decompiling:|Analyzing:)|\Z)'
        matches = re.finditer(pattern, content, re.MULTILINE)

        for match in matches:
            func_name = match.group(1)
            code = match.group(2).strip()
            if code and len(code) > 50:  # Filter out noise
                decompilations[func_name] = code

        return decompilations

    def review_decompilation(self, function_name: str, decompiled_code: str) -> ReviewResult:
        """
        Review a decompilation and generate corrected implementation.

        Args:
            function_name: Name of the function
            decompiled_code: Decompiled code from Binary Ninja

        Returns:
            ReviewResult with findings and corrections
        """
        print(f"  Analyzing decompilation...")

        # Analyze the decompilation
        analysis = self.agent.analyze_decompilation(decompiled_code, function_name)

        issues = []
        struct_defs = []
        safe_patterns = []

        # Extract struct information
        if "offsets" in analysis and analysis["offsets"]:
            print(f"  Found {len(analysis['offsets'])} struct offsets")

            # Check for unsafe access patterns
            if "*(uint32_t*)" in decompiled_code or "*(int32_t*)" in decompiled_code:
                issues.append("UNSAFE: Direct pointer arithmetic found - must use typed struct access")

            # Generate struct definition if we have offset information
            if "struct_definition" in analysis and analysis["struct_definition"]:
                struct_defs.append(analysis["struct_definition"])

        # Get corrected implementation
        corrected = analysis.get("safe_implementation", "")
        notes = analysis.get("notes", "")

        # Fallbacks if corrected implementation is missing
        if not corrected:
            # 1) Try to extract a C code block from notes/raw analysis
            import re
            blob = notes or analysis.get("raw_analysis", "")
            m = re.search(r"```(?:c|C)?\s*(.*?)```", blob, re.DOTALL)
            if m:
                candidate = m.group(1).strip()
                if candidate:
                    corrected = candidate
            # 2) Last resort: directly request only the C implementation from the agent
            if not corrected:
                try:
                    prompt = f"""Based on this decompilation of {function_name}, output ONLY a safe C implementation.
Use typed struct access or memcpy for offsets. Do not include any commentary — just the code in a C block.

Decompiled code:
```c
{decompiled_code}
```
"""
                    resp = self.agent.ask(prompt)
                    m2 = re.search(r"```(?:c|C)?\s*(.*?)```", resp, re.DOTALL)
                    if m2:
                        candidate2 = m2.group(1).strip()
                        if candidate2:
                            corrected = candidate2
                except Exception:
                    pass


        # Normalize corrected implementation to a C code string
        def _join_line_map(d: Dict[str, Any]) -> Optional[str]:
            try:
                # Heuristic: dict where keys are code lines (strings) and values are mostly empty/ignored
                if all(isinstance(k, str) for k in d.keys()):
                    # preserve insertion order
                    return "\n".join(d.keys())
            except Exception:
                return None
            return None

        def _extract_c_code_from_json_string(s: str) -> Optional[str]:
            """Try to parse a JSON string and extract C code from safe_implementation field."""
            try:
                # Remove leading "json" or "```json" markers
                s = s.strip()
                if s.startswith("json"):
                    s = s[4:].strip()
                if s.startswith("```json"):
                    s = s[7:].strip()
                if s.endswith("```"):
                    s = s[:-3].strip()

                # Try to parse as JSON
                parsed = json.loads(s)
                if isinstance(parsed, dict):
                    # Look for safe_implementation field
                    if "safe_implementation" in parsed:
                        impl = parsed["safe_implementation"]
                        if isinstance(impl, dict):
                            # Dict-of-lines pattern
                            return _join_line_map(impl)
                        elif isinstance(impl, str):
                            return impl
            except Exception:
                pass
            return None

        # First, handle the case where corrected is a string containing JSON
        if isinstance(corrected, str):
            # Check if it looks like JSON (starts with "json" or "{")
            stripped = corrected.strip()
            if stripped.startswith("json") or (stripped.startswith("{") and "safe_implementation" in stripped):
                extracted = _extract_c_code_from_json_string(corrected)
                if extracted and extracted.strip():
                    corrected = extracted
                else:
                    # Failed to extract - reject this implementation
                    print(f"  WARNING: GPT returned JSON instead of C code, rejecting implementation")
                    corrected = ""
        elif not isinstance(corrected, str):
            if isinstance(corrected, dict):
                # Case 1: safe_implementation itself is a dict-of-lines
                joined = _join_line_map(corrected)
                if joined and joined.strip():
                    corrected = joined
                else:
                    # Case 2: nested common keys that might hold the code (string or dict-of-lines)
                    for k in ["safe_implementation", "code", "implementation", "c", "text", "content", "lines"]:
                        v = corrected.get(k)
                        if isinstance(v, str) and v.strip():
                            corrected = v
                            break
                        if isinstance(v, dict):
                            j2 = _join_line_map(v)
                            if j2 and j2.strip():
                                corrected = j2
                                break
                        if isinstance(v, list):
                            try:
                                corrected = "\n".join(str(x) for x in v)
                                break
                            except Exception:
                                pass
                    else:
                        # As a last resort, do NOT dump JSON (would corrupt C files). Leave empty.
                        corrected = ""
            elif isinstance(corrected, list):
                try:
                    corrected = "\n".join(str(x) for x in corrected)
                except Exception:
                    corrected = ""
            else:
                corrected = str(corrected) if corrected is not None else ""

        # Final validation: ensure it looks like C code, not JSON
        if corrected and corrected.strip():
            stripped = corrected.strip()
            # Reject if it still looks like JSON
            if (stripped.startswith("{") and stripped.endswith("}") and
                ("offsets" in stripped or "struct_definition" in stripped or "notes" in stripped)):
                print(f"  WARNING: Implementation still contains JSON structure, rejecting")
                corrected = ""

        # Generate safe access patterns for common operations
        if "read" in decompiled_code.lower() or "get" in function_name.lower():
            safe_patterns.append("Use typed struct access: struct->member")
        if "write" in decompiled_code.lower() or "set" in function_name.lower():
            safe_patterns.append("Use typed struct access: struct->member = value")

        # Final safeguard: ensure the corrected implementation looks like C for this function
        import re as _re
        if corrected and not _re.search(rf"\b{_re.escape(function_name)}\s*\(", corrected):
            # If it doesn't even contain the function name signature, discard to avoid corrupting C files
            corrected = ""

        result = ReviewResult(
            function_name=function_name,
            issues_found=issues,
            struct_definitions=struct_defs,
            corrected_implementation=corrected,
            safe_access_patterns=safe_patterns,
            notes=notes
        )

        return result

    def analyze_struct_updates_needed(self, analysis_result: Dict[str, Any], src_file: str) -> Optional[Dict[str, Any]]:
        """
        Analyze if struct definitions need to be updated based on discovered offsets.

        This is now analysis-only - it doesn't parse or edit code, just reports findings.

        Args:
            analysis_result: The GPT analysis result containing offsets and struct_definition
            src_file: Path to the source file containing the struct definition

        Returns:
            Dict with struct update info if needed, None otherwise
        """
        # Extract discovered offsets
        offsets = analysis_result.get("offsets", {})
        if not offsets or not isinstance(offsets, dict):
            return None

        # Get proposed struct definition from analysis
        proposed_struct = analysis_result.get("struct_definition", "")

        if isinstance(proposed_struct, dict):
            # Dict-of-lines pattern
            proposed_struct = "\n".join(proposed_struct.keys())
        elif not isinstance(proposed_struct, str):
            proposed_struct = str(proposed_struct)

        if not proposed_struct.strip():
            return None

        # Return structured info for Auggie to apply
        return {
            "src_file": src_file,
            "struct_name": "ISPDevice",  # Could be extracted from proposed_struct
            "discovered_offsets": offsets,
            "proposed_definition": proposed_struct.strip(),
            "notes": "Struct definition needs update based on discovered offsets"
        }

    def review_source_file(self, source_file: str) -> List[ReviewResult]:
        """
        Review an existing source file for issues using AI analysis.
        If apply_fixes is True, directly edits the file with corrections.

        Args:
            source_file: Path to C source file

        Returns:
            List of review results
        """
        action = "Fixing" if self.apply_fixes else "Reviewing"
        print(f"{action} source file: {source_file}")

        with open(source_file, 'r') as f:
            content = f.read()

        # Extract functions from the source file
        import re

        # Find all function definitions
        func_pattern = r'(?:^|\n)(?:static\s+)?(?:inline\s+)?(\w+(?:\s*\*)?)\s+(\w+)\s*\([^)]*\)\s*\{'
        functions = list(re.finditer(func_pattern, content, re.MULTILINE))

        print(f"  Found {len(functions)} functions")

        file_results = []

        for idx, match in enumerate(functions, 1):
            return_type = match.group(1).strip()
            func_name = match.group(2)

            # Check if we should skip this function (not in OEM binary)
            if self.binary_context.should_skip_function(func_name):
                print(f"  [{idx}/{len(functions)}] Skipping: {func_name} (not in OEM binary)")
                continue

            # Extract the function body (simplified - just get next 100 lines)
            start_pos = match.start()
            # Find matching closing brace (simplified)
            func_end = content.find('\n}\n', start_pos)
            if func_end == -1:
                func_end = start_pos + 2000  # Limit to reasonable size

            func_code = content[start_pos:func_end + 3]

            # Skip very short functions
            if len(func_code) < 50:
                continue

            print(f"  [{idx}/{len(functions)}] Analyzing: {func_name}...")

            # Get binary context guidance with existing code
            guidance = self.binary_context.get_implementation_guidance(func_name, func_code)
            first_line = guidance.split('\n')[0]
            print(f"    {first_line}")

            # Use AI to analyze the function with context (with retry logic)
            max_retries = 5
            retry_delay = 1.0
            analysis = None

            for attempt in range(max_retries):
                try:
                    # Add context to the analysis
                    context_prompt = f"""
BINARY CONTEXT AND GUIDANCE:
{guidance}

EXISTING CODE TO REVIEW/FIX:
"""
                    analysis = self.agent.analyze_decompilation(
                        context_prompt + func_code,
                        func_name
                    )
                    break  # Success, exit retry loop

                except Exception as e:
                    error_str = str(e)

                    # Check if it's a rate limit error
                    if "rate_limit_exceeded" in error_str or "429" in error_str:
                        if attempt < max_retries - 1:
                            # Extract wait time from error message if available
                            import re
                            wait_match = re.search(r'try again in ([\d.]+)s', error_str)
                            if wait_match:
                                wait_time = float(wait_match.group(1)) + 1.0  # Add 1s buffer
                            else:
                                wait_time = retry_delay * (2 ** attempt)  # Exponential backoff

                            print(f"    ⏳ Rate limit hit, waiting {wait_time:.1f}s (attempt {attempt + 1}/{max_retries})...")
                            time.sleep(wait_time)
                            continue
                        else:
                            print(f"    ✗ Error: {e}")
                            analysis = None
                            break
                    else:
                        # Non-rate-limit error
                        print(f"    ✗ Error: {e}")
                        analysis = None
                        break

            if not analysis:
                # Skip this function if analysis failed
                continue

            issues = []
            struct_defs = []
            safe_patterns = []

            # Extract AI findings
            if "offsets" in analysis and analysis["offsets"]:
                print(f"    → Found {len(analysis['offsets'])} struct offsets")

            if "struct_definition" in analysis and analysis["struct_definition"]:
                struct_defs.append(analysis["struct_definition"])
                print(f"    → Generated struct definition")

            if "issues" in analysis:
                issues.extend(analysis["issues"])

            corrected = analysis.get("safe_implementation", "")
            if corrected:
                print(f"    → Generated corrected implementation")

            notes = analysis.get("notes", "")

            result = ReviewResult(
                function_name=func_name,
                issues_found=issues,
                struct_definitions=struct_defs,
                corrected_implementation=corrected,
                safe_access_patterns=safe_patterns,
                notes=notes or f"AI analysis of {func_name} from {source_file}"
            )

            file_results.append(result)
            self.results.append(result)

            # Small delay to avoid rate limits (0.5s between requests)
            time.sleep(0.5)

        if not file_results:
            # No functions found, create a summary result
            issues = []
            unsafe_casts = re.findall(r'\*\((?:uint\d+_t|int\d+_t)\s*\*\)\s*\([^)]+\s*\+\s*0x[0-9a-fA-F]+\)', content)
            if unsafe_casts:
                issues.append(f"Found {len(unsafe_casts)} unsafe pointer arithmetic operations")

            result = ReviewResult(
                function_name=Path(source_file).stem,
                issues_found=issues,
                struct_definitions=[],
                corrected_implementation="",
                safe_access_patterns=[],
                notes=f"File-level review of {source_file}"
            )
            self.results.append(result)

        # Apply fixes if requested
        if self.apply_fixes and file_results:
            self._apply_fixes_to_file(source_file, file_results, content)

        return self.results

    def _apply_fixes_to_file(self, source_file: str, results: List[ReviewResult], original_content: str):
        """
        Apply AI-generated fixes directly to the source file.
        Uses git for version control - no backup files created.

        Args:
            source_file: Path to source file
            results: List of review results with corrected implementations
            original_content: Original file content
        """
        # Build a map of function names to corrected implementations
        corrections = {}
        for result in results:
            if result.corrected_implementation:
                corrections[result.function_name] = result.corrected_implementation

        if not corrections:
            print(f"  → No corrections to apply")
            return

        # Apply corrections
        modified_content = original_content
        corrections_applied = 0

        for func_name, corrected_impl in corrections.items():
            # Find the function in the original content
            import re

            # Pattern to match the entire function
            # This is simplified - matches function_name(...) { ... }
            pattern = rf'(\w+\s+{re.escape(func_name)}\s*\([^)]*\)\s*\{{)'
            match = re.search(pattern, modified_content)

            if match:
                # Find the matching closing brace
                start_pos = match.start()
                brace_count = 0
                pos = match.end() - 1  # Start at the opening brace

                while pos < len(modified_content):
                    if modified_content[pos] == '{':
                        brace_count += 1
                    elif modified_content[pos] == '}':
                        brace_count -= 1
                        if brace_count == 0:
                            end_pos = pos + 1
                            break
                    pos += 1
                else:
                    print(f"  ✗ Could not find end of function {func_name}")
                    continue

                # Replace the function
                old_func = modified_content[start_pos:end_pos]
                modified_content = modified_content[:start_pos] + corrected_impl + modified_content[end_pos:]
                corrections_applied += 1
                print(f"  ✓ Applied correction to {func_name}")
            else:
                print(f"  ✗ Could not find function {func_name} in file")

        if corrections_applied > 0:
            # Write the modified content back
            with open(source_file, 'w') as f:
                f.write(modified_content)

            self.files_modified.append(source_file)
            print(f"  ✓ Modified {source_file} ({corrections_applied} functions corrected)")
        else:
            print(f"  → No corrections applied to {source_file}")

    def decompile_and_implement(self, function_name: str, binary_id: str, src_file: Optional[str] = None) -> ReviewResult:
        """
        Decompile a function and generate implementation.

        Args:
            function_name: Name of function to decompile
            binary_id: Binary ID (e.g., port_9009)
            src_file: Optional source file path for struct analysis

        Returns:
            ReviewResult with implementation
        """
        print(f"Decompiling {function_name} from {binary_id}...")

        # Call Binary Ninja MCP to get decompilation
        decompiled = self.mcp.decompile_function(binary_id, function_name)

        if decompiled:
            result = self.review_decompilation(function_name, decompiled)

            # Analyze if struct updates are needed (if source file provided)
            if src_file and os.path.exists(src_file):
                # Get the raw analysis result for struct analysis
                analysis = self.agent.analyze_decompilation(decompiled, function_name)
                struct_update = self.analyze_struct_updates_needed(analysis, src_file)

                if struct_update:
                    print(f"  ⚠ Struct update recommended for {struct_update['struct_name']}")
                    print(f"    See artifacts for proposed definition")
                    # Add to notes
                    if isinstance(result.notes, str):
                        result.notes += f"\n\nSTRUCT UPDATE NEEDED:\n{struct_update['struct_name']} has uncovered offsets.\nProposed definition:\n{struct_update['proposed_definition']}"
                    # Store struct update info for Auggie to apply
                    if not hasattr(self, 'struct_updates'):
                        self.struct_updates = []
                    self.struct_updates.append(struct_update)

            # Ensure results are persisted for save_results()
            self.results.append(result)
            return result
        else:
            result = ReviewResult(
                function_name=function_name,
                issues_found=["Could not decompile function"],
                struct_definitions=[],
                corrected_implementation="",
                safe_access_patterns=[],
                notes="Decompilation failed"
            )
            self.results.append(result)
            return result

    def generate_report(self) -> str:
        """Generate a comprehensive report of all results"""
        report = []
        report.append("="*80)
        report.append("MIPS REVERSE ENGINEERING BATCH REVIEW REPORT")
        report.append("="*80)
        report.append("")

        for i, result in enumerate(self.results, 1):
            report.append(f"\n{i}. {result.function_name}")
            report.append("-" * 80)

            if result.issues_found:
                report.append("\nISSUES FOUND:")
                for issue in result.issues_found:
                    report.append(f"  • {issue}")

            if result.struct_definitions:
                report.append("\nSTRUCT DEFINITIONS:")
                for struct_def in result.struct_definitions:
                    try:
                        if isinstance(struct_def, str):
                            report.append(struct_def)
                        else:
                            # Coerce dicts/lists to pretty JSON for the report
                            report.append(json.dumps(struct_def, indent=2))
                    except Exception:
                        report.append(str(struct_def))

            if result.safe_access_patterns:
                report.append("\nSAFE ACCESS PATTERNS:")
                for pattern in result.safe_access_patterns:
                    try:
                        report.append(f"  • {pattern}")
                    except Exception:
                        report.append(f"  • {str(pattern)}")

            if result.corrected_implementation:
                report.append("\nCORRECTED IMPLEMENTATION:")
                report.append(str(result.corrected_implementation))

            if result.notes:
                report.append(f"\nNOTES: {str(result.notes)}")

            report.append("")

        report.append("="*80)
        report.append(f"SUMMARY: Reviewed {len(self.results)} functions")
        report.append("="*80)

        return "\n".join(report)

    def save_results(self):
        """Save results to output directory"""
        # Save JSON
        json_file = self.output_dir / "review_results.json"
        with open(json_file, 'w') as f:
            json.dump([asdict(r) for r in self.results], f, indent=2)
        print(f"\n✓ Saved JSON results to {json_file}")

        # Save report
        report_file = self.output_dir / "review_report.txt"
        with open(report_file, 'w') as f:
            f.write(self.generate_report())
        print(f"✓ Saved report to {report_file}")

        # Save individual implementations
        impl_dir = self.output_dir / "implementations"
        impl_dir.mkdir(exist_ok=True)

        for result in self.results:
            if result.corrected_implementation:
                impl_file = impl_dir / f"{result.function_name}.c"
                with open(impl_file, 'w') as f:
                    f.write(str(result.corrected_implementation))

        print(f"✓ Saved {len([r for r in self.results if r.corrected_implementation])} implementations to {impl_dir}")

        # Save Auggie-compatible artifacts (for automated application)
        if hasattr(self, 'struct_updates') and self.struct_updates:
            auggie_dir = self.output_dir / "auggie_artifacts"
            auggie_dir.mkdir(exist_ok=True)

            for update in self.struct_updates:
                artifact_file = auggie_dir / f"{update['struct_name']}_update.json"
                with open(artifact_file, 'w') as f:
                    json.dump(update, f, indent=2)

            print(f"✓ Saved {len(self.struct_updates)} struct update artifacts to {auggie_dir}")

        # Save function implementation artifacts for Auggie
        if self.results:
            auggie_dir = self.output_dir / "auggie_artifacts"
            auggie_dir.mkdir(exist_ok=True)

            for result in self.results:
                if result.corrected_implementation:
                    artifact = {
                        "function_name": result.function_name,
                        "implementation": result.corrected_implementation,
                        "struct_definitions": result.struct_definitions,
                        "notes": result.notes,
                        "issues_found": result.issues_found
                    }
                    artifact_file = auggie_dir / f"{result.function_name}.json"
                    with open(artifact_file, 'w') as f:
                        json.dump(artifact, f, indent=2)

            print(f"✓ Saved {len([r for r in self.results if r.corrected_implementation])} function artifacts for Auggie to {auggie_dir}")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="Batch review and implementation generator")
    parser.add_argument("--input", "-i", help="Input log file to process")
    parser.add_argument("--review-file", "-r", help="Review existing source file")
    parser.add_argument("--decompile", "-d", help="Function name to decompile")
    parser.add_argument("--binary", "-b", default="port_9009", help="Binary ID (default: port_9009)")
    parser.add_argument("--output", "-o", default="review_results", help="Output directory")

    args = parser.parse_args()

    if not any([args.input, args.review_file, args.decompile]):
        parser.print_help()
        print("\nError: Must specify --input, --review-file, or --decompile")
        return 1

    agent = BatchReviewAgent(output_dir=args.output)

    try:
        if args.input:
            agent.process_log_file(args.input)

        if args.review_file:
            agent.review_source_file(args.review_file)

        if args.decompile:
            agent.decompile_and_implement(args.decompile, args.binary)

        # Generate and save results
        print("\n" + agent.generate_report())
        agent.save_results()

        return 0

    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())

