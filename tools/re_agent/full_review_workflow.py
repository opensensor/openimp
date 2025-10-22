#!/usr/bin/env python3
"""
Full Review Workflow - One-Shot Implementation Generator

This script processes all your logs, decompilations, and existing code to:
1. Extract all function decompilations from logs
2. Review existing implementations for safety issues
3. Generate corrected implementations with proper struct access
4. Create comprehensive struct definitions
5. Produce a complete implementation package

Usage:
    # Process everything in one go
    python full_review_workflow.py --project-dir /path/to/openimp
    
    # Process specific components
    python full_review_workflow.py --logs logs/*.log --sources src/imp/*.c
    
    # Use Binary Ninja MCP to decompile missing functions
    python full_review_workflow.py --auto-decompile --binary port_9009
"""

import os
import sys
import json
import argparse
import glob
from pathlib import Path
from typing import List, Dict, Set
from dataclasses import dataclass

sys.path.insert(0, str(Path(__file__).parent))

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient
from batch_review import BatchReviewAgent, ReviewResult


@dataclass
class ProjectAnalysis:
    """Complete project analysis results"""
    functions_analyzed: int
    structs_discovered: int
    issues_found: int
    implementations_generated: int
    files_reviewed: int


class FullReviewWorkflow:
    """Complete workflow for reviewing and generating implementations"""

    def __init__(self, output_dir: str = "full_review_output", apply_fixes: bool = False, binary_id: str = "port_9009"):
        """Initialize the workflow

        Args:
            output_dir: Output directory for results
            apply_fixes: If True, directly edit source files with AI corrections
            binary_id: Binary Ninja MCP server ID for context
        """
        self.agent = MIPSReverseEngineeringAgent()
        self.batch_agent = BatchReviewAgent(
            output_dir=output_dir,
            apply_fixes=apply_fixes,
            binary_id=binary_id
        )
        self.mcp = BinaryNinjaMCPClient()
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.apply_fixes = apply_fixes
        self.binary_id = binary_id

        self.all_structs: Dict[str, StructLayout] = {}
        self.all_functions: Set[str] = set()
    
    def process_project(self, project_dir: str):
        """
        Process entire project directory.
        
        Args:
            project_dir: Root directory of the project
        """
        project_path = Path(project_dir)
        
        print("="*80)
        mode = "FIX MODE - EDITING FILES" if self.apply_fixes else "REVIEW MODE - ANALYSIS ONLY"
        print(f"FULL PROJECT WORKFLOW - {mode}")
        print("="*80)
        print(f"\nProject: {project_path}")
        print(f"Binary:  {self.binary_id}")
        if self.apply_fixes:
            print("\n⚠️  WARNING: This will MODIFY your source files!")
            print("   Use 'git diff' to review changes")
            print("   Use 'git restore <file>' to undo changes")
        print()
        
        # Step 1: Find and process all logs
        print("Step 1: Processing logs...")
        log_files = list(project_path.glob("**/*.log"))
        print(f"  Found {len(log_files)} log files")
        
        for log_file in log_files:
            print(f"  Processing: {log_file.name}")
            self.batch_agent.process_log_file(str(log_file))
        
        # Step 2: Review all source files
        print("\nStep 2: Reviewing source files...")
        source_files = list(project_path.glob("src/**/*.c"))
        print(f"  Found {len(source_files)} source files")

        for idx, source_file in enumerate(source_files, 1):
            print(f"\n  [{idx}/{len(source_files)}] {source_file.relative_to(project_path)}")
            self.batch_agent.review_source_file(str(source_file))
        
        # Step 3: Extract all struct definitions
        print("\nStep 3: Consolidating struct definitions...")
        self._consolidate_structs()
        
        # Step 4: Generate final implementations
        print("\nStep 4: Generating final implementations...")
        self._generate_final_implementations()
        
        # Step 5: Save everything
        print("\nStep 5: Saving results...")
        self.save_all_results()
        
        # Print summary
        self.print_summary()
    
    def process_logs_and_sources(self, log_patterns: List[str], source_patterns: List[str]):
        """
        Process specific log and source files.
        
        Args:
            log_patterns: List of log file patterns (glob)
            source_patterns: List of source file patterns (glob)
        """
        print("="*80)
        print("TARGETED REVIEW WORKFLOW")
        print("="*80)
        print()
        
        # Process logs
        if log_patterns:
            print("Processing logs...")
            for pattern in log_patterns:
                for log_file in glob.glob(pattern):
                    print(f"  {log_file}")
                    self.batch_agent.process_log_file(log_file)
        
        # Process sources
        if source_patterns:
            print("\nReviewing sources...")
            for pattern in source_patterns:
                for source_file in glob.glob(pattern):
                    print(f"  {source_file}")
                    self.batch_agent.review_source_file(source_file)
        
        # Consolidate and generate
        self._consolidate_structs()
        self._generate_final_implementations()
        self.save_all_results()
        self.print_summary()
    
    def auto_decompile_missing(self, binary_id: str, function_list: List[str]):
        """
        Auto-decompile missing functions from binary.
        
        Args:
            binary_id: Binary ID (e.g., port_9009)
            function_list: List of function names to decompile
        """
        print(f"\nAuto-decompiling {len(function_list)} functions from {binary_id}...")
        
        for func_name in function_list:
            print(f"  Decompiling: {func_name}")
            result = self.batch_agent.decompile_and_implement(func_name, binary_id)
            self.all_functions.add(func_name)
    
    def _consolidate_structs(self):
        """Consolidate all discovered struct definitions"""
        print("  Consolidating struct definitions...")
        
        struct_count = 0
        for result in self.batch_agent.results:
            for struct_def in result.struct_definitions:
                # Extract struct name
                import re
                match = re.search(r'typedef struct (\w+)', struct_def)
                if match:
                    struct_name = match.group(1)
                    # Store the definition (would parse properly in real implementation)
                    struct_count += 1
        
        print(f"  Found {struct_count} struct definitions")
    
    def _generate_final_implementations(self):
        """Generate final corrected implementations"""
        print("  Generating final implementations...")
        
        impl_count = 0
        for result in self.batch_agent.results:
            if result.corrected_implementation:
                impl_count += 1
                self.all_functions.add(result.function_name)
        
        print(f"  Generated {impl_count} implementations")
    
    def save_all_results(self):
        """Save all results to output directory"""
        # Save batch results
        self.batch_agent.save_results()
        
        # Save consolidated structs
        structs_file = self.output_dir / "all_structs.h"
        with open(structs_file, 'w') as f:
            f.write("/* Auto-generated struct definitions */\n")
            f.write("/* Generated by MIPS RE Agent */\n\n")
            f.write("#ifndef OPENIMP_STRUCTS_H\n")
            f.write("#define OPENIMP_STRUCTS_H\n\n")
            f.write("#include <stdint.h>\n")
            f.write("#include <pthread.h>\n")
            f.write("#include <semaphore.h>\n\n")
            
            for result in self.batch_agent.results:
                for struct_def in result.struct_definitions:
                    f.write(struct_def)
                    f.write("\n\n")
            
            f.write("#endif /* OPENIMP_STRUCTS_H */\n")
        
        print(f"✓ Saved consolidated structs to {structs_file}")
        
        # Save function index
        index_file = self.output_dir / "function_index.json"
        with open(index_file, 'w') as f:
            json.dump({
                "functions": sorted(list(self.all_functions)),
                "count": len(self.all_functions)
            }, f, indent=2)
        
        print(f"✓ Saved function index to {index_file}")
    
    def print_summary(self):
        """Print summary of analysis"""
        total_issues = sum(len(r.issues_found) for r in self.batch_agent.results)
        total_structs = sum(len(r.struct_definitions) for r in self.batch_agent.results)
        total_impls = sum(1 for r in self.batch_agent.results if r.corrected_implementation)

        print("\n" + "="*80)
        print("SUMMARY")
        print("="*80)
        print(f"Functions analyzed:        {len(self.batch_agent.results)}")
        print(f"Struct definitions found:  {total_structs}")
        print(f"Issues identified:         {total_issues}")
        print(f"Implementations generated: {total_impls}")

        if self.apply_fixes:
            print(f"Files modified:            {len(self.batch_agent.files_modified)}")
            if self.batch_agent.files_modified:
                print("\nModified files:")
                for f in self.batch_agent.files_modified:
                    print(f"  • {f}")

        print(f"Output directory:          {self.output_dir}")
        print("="*80)

        if self.apply_fixes:
            print("\n✓ Your source files have been corrected!")
            print("\nNext steps:")
            print("  1. Review changes: git diff")
            print("  2. Test your code to verify corrections")
            print("  3. Commit if satisfied: git add -A && git commit -m 'Apply AI corrections'")
            print("  4. Undo if needed: git restore <file>")
        else:
            print("\nNext steps:")
            print("  1. Review the report: cat review_results/review_report.txt")
            print("  2. Check structs: cat review_results/all_structs.h")
            print("  3. Review implementations: ls review_results/implementations/")
            print("  4. Run with --apply-fixes to automatically correct your code")
        print()


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description="Full review workflow - process everything in one go",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Process entire project
  python full_review_workflow.py --project-dir /home/matteius/openimp
  
  # Process specific logs and sources
  python full_review_workflow.py --logs "logs/*.log" --sources "src/imp/*.c"
  
  # Auto-decompile missing functions
  python full_review_workflow.py --auto-decompile --functions IMP_Encoder_CreateGroup,IMP_System_Bind
        """
    )
    
    parser.add_argument("--project-dir", "-p", help="Project root directory")
    parser.add_argument("--logs", "-l", nargs="+", help="Log file patterns")
    parser.add_argument("--sources", "-s", nargs="+", help="Source file patterns")
    parser.add_argument("--auto-decompile", "-a", action="store_true", help="Auto-decompile missing functions")
    parser.add_argument("--functions", "-f", help="Comma-separated list of functions to decompile")
    parser.add_argument("--binary", "-b", default="port_9009", help="Binary ID for context and decompilation")
    parser.add_argument("--output", "-o", default="full_review_output", help="Output directory")
    parser.add_argument("--apply-fixes", action="store_true", help="Apply AI corrections directly to source files")

    args = parser.parse_args()

    if not any([args.project_dir, args.logs, args.sources, args.auto_decompile]):
        parser.print_help()
        return 1

    workflow = FullReviewWorkflow(
        output_dir=args.output,
        apply_fixes=args.apply_fixes,
        binary_id=args.binary
    )
    
    try:
        if args.project_dir:
            workflow.process_project(args.project_dir)
        elif args.logs or args.sources:
            workflow.process_logs_and_sources(
                args.logs or [],
                args.sources or []
            )
        
        if args.auto_decompile and args.functions:
            function_list = [f.strip() for f in args.functions.split(",")]
            workflow.auto_decompile_missing(args.binary, function_list)
            workflow.save_all_results()
            workflow.print_summary()
        
        return 0
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())

