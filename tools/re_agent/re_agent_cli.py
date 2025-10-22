#!/usr/bin/env python3
"""
Reverse Engineering Agent CLI

Command-line interface for the MIPS reverse engineering agent.
Provides interactive and batch modes for analyzing MIPS drivers.
"""

import sys
import argparse
import json
import os
import re
import shutil
import shlex

from pathlib import Path
from typing import Optional

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient, SmartDiffAnalyzer
from batch_review import BatchReviewAgent


def print_banner():
    """Print the agent banner"""
    banner = """
╔═══════════════════════════════════════════════════════════════╗
║   MIPS Driver Reverse Engineering Agent                      ║
║   Powered by OpenAI + Binary Ninja MCP                       ║
║   Safe Struct Member Access Patterns                         ║
╚═══════════════════════════════════════════════════════════════╝
"""
    print(banner)


def interactive_mode(agent: MIPSReverseEngineeringAgent, mcp: BinaryNinjaMCPClient):
    """Run the agent in interactive mode"""
    print("\nInteractive Mode - Type 'help' for commands, 'exit' to quit\n")

    while True:
        try:
            user_input = input("RE-Agent> ").strip()

            if not user_input:
                continue

            if user_input.lower() in ['exit', 'quit', 'q']:
                print("Goodbye!")
                break

            if user_input.lower() == 'help':
                print_help()
                continue

            if user_input.lower() == 'reset':
                agent.reset_conversation()
                print("Conversation reset.")
                continue

            if user_input.lower() == 'list-binaries':
                binaries = mcp.list_binaries()
                print("\nAvailable binaries:")
                for binary in binaries:
                    print(f"  - {binary.binary_id}: {binary.name} ({binary.architecture})")
                continue

            if user_input.lower().startswith('decompile '):
                parts = user_input.split(maxsplit=2)
                if len(parts) < 3:
                    print("Usage: decompile <binary_id> <function_name>")
                    continue

                _, binary_id, function_name = parts
                code = mcp.decompile_function(binary_id, function_name)
                if code:
                    print(f"\nDecompiled {function_name}:")
                    print(code)
                else:
                    print(f"Failed to decompile {function_name}")
                continue

            if user_input.lower().startswith('analyze-only '):
                parts = user_input.split(maxsplit=2)
                if len(parts) < 3:
                    print("Usage: analyze-only <binary_id> <function_name>")
                    print("  (Just decompiles and analyzes, doesn't generate artifacts)")
                    continue

                _, binary_id, function_name = parts
                code = mcp.decompile_function(binary_id, function_name)
                if code:
                    print(f"\nAnalyzing {function_name}...")
                    result = agent.analyze_decompilation(code, function_name)
                    print(json.dumps(result, indent=2))
                else:
                    print(f"Failed to decompile {function_name}")
                continue
            if user_input.lower().startswith('apply '):
                parts = user_input.split(maxsplit=2)
                if len(parts) < 2:
                    print("Usage: apply <function_name> [src_file]")
                    continue
                _, function_name, *rest = parts
                src_file = rest[0] if rest else None
                _, msg = apply_function_implementation(function_name, src_file)
                print(msg)
                continue

            if user_input.lower().startswith('analyze '):
                tokens = shlex.split(user_input)
                if len(tokens) < 3:
                    print("Usage: analyze <binary_id> <function_name> [src_file] [--apply]")
                    print("  Analyzes a function and generates artifacts for Auggie to apply")
                    print("  --apply: Automatically apply changes via Auggie after analysis")
                    continue
                _, binary_id, function_name, *rest = tokens
                src_file = None
                auto_apply = False
                for token in rest:
                    if token == '--apply':
                        auto_apply = True
                    elif not token.startswith('-'):
                        src_file = token
                _, msg = analyze_command(function_name, binary_id, src_file,
                                        impl_root='tools/re_agent/full_review_output',
                                        auto_apply=auto_apply)
                print(msg)
                continue


            # Default: ask the agent
            response = agent.ask(user_input)
            print(f"\n{response}\n")

        except KeyboardInterrupt:
            print("\n\nUse 'exit' to quit.")
        except Exception as e:
            print(f"Error: {e}")


def print_help():
    """Print help information"""
    help_text = """
Available Commands:
  help                                    - Show this help message
  exit, quit, q                           - Exit the agent
  reset                                   - Reset conversation history
  list-binaries                           - List available binaries in MCP
  decompile <binary_id> <function>        - Decompile a function
  analyze-only <binary_id> <function>     - Just analyze (no artifacts)
  analyze <binary_id> <function> [--apply] - Analyze and generate artifacts
                                            --apply: Auto-apply via Auggie
  apply <function> [src_file]             - Apply generated impl into src (legacy)

  Any other input will be sent to the AI agent for analysis.

Examples:
  > decompile port_9009 IMP_Encoder_CreateGroup
  > analyze port_9009 IMP_ISP_AddSensor
  > analyze port_9009 IMP_ISP_AddSensor --apply  (auto-apply changes)
  > live-apply port_9009 IMP_ISP_AddSensor --dry-run
  > live-apply port_9009 IMP_OSD_ShowRgn src/imp_osd.c --no-adapt
  > What struct offsets are used in IMP_Encoder_GetStream?
  > Generate safe access code for EncChannel at offset 0x398
"""
    print(help_text)


def analyze_function_command(agent: MIPSReverseEngineeringAgent,
                             mcp: BinaryNinjaMCPClient,
                             binary_id: str,
                             function_name: str,
                             output_file: Optional[str] = None):
    """Analyze a function and optionally save results"""
    print(f"Analyzing {function_name} from {binary_id}...")

    # Decompile the function
    code = mcp.decompile_function(binary_id, function_name)
    if not code:
        print(f"Error: Failed to decompile {function_name}")
        return

    # Analyze with the agent
    result = agent.analyze_decompilation(code, function_name)

    # Print results
    print("\n" + "="*70)
    print(f"Analysis Results for {function_name}")
    print("="*70)
    print(json.dumps(result, indent=2))

    # Save to file if requested
    if output_file:
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)

        with open(output_path, 'w') as f:
            json.dump({
                "function": function_name,
                "binary": binary_id,
                "decompiled_code": code,
                "analysis": result
            }, f, indent=2)

        print(f"\nResults saved to {output_file}")


def compare_functions_command(agent: MIPSReverseEngineeringAgent,
                              mcp: BinaryNinjaMCPClient,
                              old_binary: str,
                              new_binary: str,
                              function_name: str,
                              output_file: Optional[str] = None):
    """Compare two versions of a function"""
    print(f"Comparing {function_name} between {old_binary} and {new_binary}...")

    # Decompile both versions
    old_code = mcp.decompile_function(old_binary, function_name)
    new_code = mcp.decompile_function(new_binary, function_name)

    if not old_code or not new_code:
        print("Error: Failed to decompile one or both functions")
        return

    # Compare with the agent
    result = agent.compare_binary_versions(old_code, new_code, function_name)

    # Print results
    print("\n" + "="*70)
    print(f"Comparison Results for {function_name}")
    print("="*70)
    print(json.dumps(result, indent=2))

    # Save to file if requested
    if output_file:
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'w') as f:
            json.dump({
                "function": function_name,
                "old_binary": old_binary,
                "new_binary": new_binary,
                "old_code": old_code,
                "new_code": new_code,
                "comparison": result
            }, f, indent=2)


def analyze_command(function_name: str,
                    binary_id: str,
                    src_file: Optional[str],
                    impl_root: str,
                    auto_apply: bool = False) -> tuple[bool, str]:
    """Analyze a function and generate artifacts for Auggie to apply.

    Args:
        function_name: Name of the function to analyze
        binary_id: Binary Ninja MCP server ID (e.g., 'port_9009')
        src_file: Source file path (optional, will be inferred if not provided)
        impl_root: Root directory for output artifacts
        auto_apply: If True, automatically apply changes via Auggie after analysis

    Returns:
        Tuple of (success, message)
    """
    # Determine source file if not provided
    if not src_file:
        src_file = infer_source_file(function_name, 'src')

    # Generate implementation into impl_root using BatchReviewAgent
    agent = BatchReviewAgent(output_dir=impl_root, apply_fixes=False, binary_id=binary_id)

    # Generate decompilation + AI implementation (with struct analysis)
    try:
        agent.decompile_and_implement(function_name, binary_id, src_file=src_file)
    except Exception as e:
        return False, f"Analysis failed: {e}"

    # Save artifacts
    agent.save_results()

    # Report what was generated
    msg_parts = [f"\n✓ Analysis complete for {function_name}"]

    # Check if implementation was generated
    impl_dir = Path(impl_root) / 'implementations'
    impl_path = impl_dir / f"{function_name}.c"
    if impl_path.exists():
        msg_parts.append(f"  ✓ Implementation saved to {impl_path}")

    # Report struct updates if any were discovered
    if hasattr(agent, 'struct_updates') and agent.struct_updates:
        msg_parts.append(f"\n⚠ Struct updates recommended:")
        for update in agent.struct_updates:
            msg_parts.append(f"  • {update['struct_name']} in {update['src_file']}")

    # Report Auggie artifacts
    auggie_dir = Path(impl_root) / 'auggie_artifacts'
    artifact_count = 0
    if auggie_dir.exists():
        artifact_count = len(list(auggie_dir.glob('*.json')))
        msg_parts.append(f"\n✓ Generated {artifact_count} Auggie artifact(s) in {auggie_dir}")

    # Auto-apply if requested
    if auto_apply and artifact_count > 0:
        msg_parts.append(f"\n🔧 Auto-applying changes via Auggie...")

        # Import auggie_apply module
        import sys
        sys.path.insert(0, str(Path(__file__).parent))
        try:
            from auggie_apply import apply_function_implementation, apply_struct_update, find_auggie

            # Check if Auggie is available
            auggie_path = find_auggie()
            if not auggie_path:
                msg_parts.append(f"\n⚠ Auggie CLI not found - skipping auto-apply")
                msg_parts.append(f"  Install with: npm install -g @augmentcode/auggie")
                msg_parts.append(f"  Then manually run: python tools/re_agent/auggie_apply.py --function {function_name}")
                return True, "\n".join(msg_parts)

            # Apply function implementation
            func_artifact = auggie_dir / f"{function_name}.json"
            if func_artifact.exists():
                msg_parts.append(f"\n  → Applying function implementation...")
                success, apply_msg = apply_function_implementation(str(func_artifact), dry_run=False)
                if success:
                    msg_parts.append(f"    ✓ Function implementation applied successfully")
                else:
                    msg_parts.append(f"    ✗ Failed to apply function: {apply_msg}")

            # Apply struct updates if any
            if hasattr(agent, 'struct_updates') and agent.struct_updates:
                for update in agent.struct_updates:
                    struct_artifact = auggie_dir / f"{update['struct_name']}_update.json"
                    if struct_artifact.exists():
                        msg_parts.append(f"\n  → Applying struct update for {update['struct_name']}...")
                        success, apply_msg = apply_struct_update(str(struct_artifact), dry_run=False)
                        if success:
                            msg_parts.append(f"    ✓ Struct update applied successfully")
                        else:
                            msg_parts.append(f"    ✗ Failed to apply struct: {apply_msg}")

            msg_parts.append(f"\n✅ Auto-apply complete!")

        except ImportError as e:
            msg_parts.append(f"\n⚠ Failed to import auggie_apply: {e}")
            msg_parts.append(f"  Manually run: python tools/re_agent/auggie_apply.py --function {function_name}")
    elif not auto_apply:
        msg_parts.append(f"\nTo apply changes, run:")
        msg_parts.append(f"  python tools/re_agent/auggie_apply.py --function {function_name}")
        msg_parts.append(f"  # Or for dry-run:")
        msg_parts.append(f"  python tools/re_agent/auggie_apply.py --function {function_name} --dry-run")
        msg_parts.append(f"\n  # Or use --apply flag to auto-apply:")
        msg_parts.append(f"  RE-Agent> analyze port_9009 {function_name} --apply")

    return True, "\n".join(msg_parts)


def infer_source_file(function_name: str, src_root: str = 'src') -> Optional[str]:
    """Infer source file path from function name.

    Args:
        function_name: Function name like IMP_ISP_AddSensor
        src_root: Root directory for source files

    Returns:
        Full path to source file, or None if cannot infer
    """
    # Expect names like IMP_OSD_ShowRgn -> module = OSD
    parts = function_name.split('_', 2)
    if len(parts) < 3 or parts[0] != 'IMP':
        return None
    module = parts[1]
    mapping = {
        'OSD': 'imp_osd.c',
        'Encoder': 'imp_encoder.c',
        'System': 'imp_system.c',
        'ISP': 'imp_isp.c',
        'IVS': 'imp_ivs.c',
        'Audio': 'imp_audio.c',
        'FrameSource': 'imp_framesource.c',
    }
    filename = mapping.get(module)
    if filename:
        return str(Path(src_root) / filename)
    return None


def apply_function_implementation(function_name: str,
                                  src_file: Optional[str] = None,
                                  impl_root: str = 'tools/re_agent/full_review_output/implementations',
                                  src_root: str = 'src',
                                  dry_run: bool = False,
                                  adapt: bool = True) -> tuple[bool, str]:
    """Apply a generated implementation into the real src tree.

    Returns (ok, message).
    """
    # Resolve implementation path
    impl_path = Path(impl_root) / f"{function_name}.c"
    if not impl_path.exists():
        return False, f"Implementation not found: {impl_path}"

    impl_code = impl_path.read_text().strip()
    if not impl_code:
        return False, f"Implementation file is empty: {impl_path}"

    # Infer target src file if not provided
    target_src = src_file or infer_source_file(function_name, src_root)
    if target_src is None:
        return False, f"Could not guess target src file from function name {function_name}; please supply --src-file"

    src_path = Path(src_root) / target_src if not Path(target_src).is_absolute() else Path(target_src)
    if not src_path.exists():
        return False, f"Source file not found: {src_path}"

    src_text = src_path.read_text()

    # Find the existing function definition range
    m = re.search(rf"(?m)^\s*[\w\*\s]+\b{re.escape(function_name)}\s*\(", src_text)
    if not m:
        return False, f"Function {function_name} not found in {src_path}"

    # From the match, find the opening brace of the function body
    idx = m.start()
    brace_open = src_text.find('{', m.end())
    if brace_open == -1:
        return False, f"Malformed function (no opening '{{') for {function_name} in {src_path}"

    # Walk to find matching closing brace
    depth = 0
    end_idx = -1
    for i in range(brace_open, len(src_text)):
        c = src_text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                end_idx = i + 1  # include closing brace
                break
    if end_idx == -1:
        return False, f"Malformed function (no matching '}}') for {function_name} in {src_path}"

    # Adapt implementation to current structs if requested
    new_func = impl_code
    if adapt:
        # Example adaptation: IMP_OSD_ShowRgn needs show_flag -> data_36[0] if missing
        if function_name == 'IMP_OSD_ShowRgn':
            if 'show_flag' not in src_text:
                new_func = re.sub(r"\brgn->show_flag\s*=\s*showFlag\s*;",
                                  "rgn->data_36[0] = (uint8_t)(showFlag ? 1 : 0);",
                                  new_func)

    # Ensure trailing newline
    if not new_func.endswith('\n'):
        new_func += '\n'

    if dry_run:
        return True, f"[DRY-RUN] Would replace {function_name} in {src_path} with implementation from {impl_path} (bytes {idx}-{end_idx})"

    # Backup first
    backup_path = src_path.with_suffix(src_path.suffix + '.bak')
    try:
        shutil.copyfile(src_path, backup_path)
    except Exception as e:
        return False, f"Failed to create backup {backup_path}: {e}"

    # Perform replacement and write
    updated = src_text[:idx] + new_func + src_text[end_idx:]
    src_path.write_text(updated)

    return True, f"Replaced {function_name} in {src_path} using {impl_path}. Backup at {backup_path}"



def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description="MIPS Driver Reverse Engineering Agent",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Interactive mode
  %(prog)s -i

  # Analyze a function
  %(prog)s analyze -b port_9009 -f IMP_Encoder_CreateGroup -o analysis.json

  # Compare function versions
  %(prog)s compare -o port_9009 -n port_9012 -f IMP_System_Bind -O comparison.json
        """
    )

    parser.add_argument('-i', '--interactive', action='store_true',
                       help='Run in interactive mode')
    parser.add_argument('--model', default='gpt-4o',
                       help='OpenAI model to use (default: gpt-4o)')
    parser.add_argument('--mcp-base', default=None,
                       help='Base URL for Binary Ninja Smart-Diff MCP HTTP aggregator (e.g., http://127.0.0.1:9309)')

    subparsers = parser.add_subparsers(dest='command', help='Command to execute')

    # Analyze command
    analyze_parser = subparsers.add_parser('analyze', help='Analyze a function')
    analyze_parser.add_argument('-b', '--binary', required=True,
                               help='Binary ID (e.g., port_9009)')
    analyze_parser.add_argument('-f', '--function', required=True,
                               help='Function name to analyze')
    analyze_parser.add_argument('-o', '--output',
                               help='Output file for results (JSON)')

    # Apply command
    apply_parser = subparsers.add_parser('apply', help='Apply generated implementation into src')
    apply_parser.add_argument('-f', '--function', required=True,
                              help='Function name to apply (e.g., IMP_OSD_ShowRgn)')
    apply_parser.add_argument('-s', '--src-file',
                              help='Target src file (guessed from function name if omitted)')
    apply_parser.add_argument('--impl-root', default='tools/re_agent/full_review_output/implementations',
                              help='Directory containing generated implementations')
    apply_parser.add_argument('--src-root', default='src',
                              help='Root directory of source tree to patch')
    apply_parser.add_argument('--dry-run', action='store_true',
                              help='Do not write changes; only report what would change')
    apply_parser.add_argument('--no-adapt', action='store_true',
                              help='Disable adaptation to current structs (adapt is ON by default)')

    # Compare command
    compare_parser = subparsers.add_parser('compare', help='Compare function versions')
    compare_parser.add_argument('-o', '--old-binary', required=True,
                               help='Old binary ID')
    compare_parser.add_argument('-n', '--new-binary', required=True,
                               help='New binary ID')
    compare_parser.add_argument('-f', '--function', required=True,
                               help='Function name to compare')
    compare_parser.add_argument('-O', '--output',
                               help='Output file for results (JSON)')

    # Live-apply command: end-to-end single-function pipeline
    live_apply_parser = subparsers.add_parser('live-apply', help='Decompile via MCP + GPT-generate + adapt + patch into src')
    live_apply_parser.add_argument('-b', '--binary', required=True, help='Binary ID (e.g., port_9009)')

    live_apply_parser.add_argument('-f', '--function', required=True, help='Function name to implement')
    live_apply_parser.add_argument('-s', '--src-file', help='Target src file (guessed from function name if omitted)')
    live_apply_parser.add_argument('--src-root', default='src', help='Root directory of source tree to patch')
    live_apply_parser.add_argument('--out-dir', default='tools/re_agent/full_review_output', help='Output dir for artifacts (impls saved here)')
    live_apply_parser.add_argument('--dry-run', action='store_true', help='Do not write changes; only report')
    live_apply_parser.add_argument('--no-adapt', action='store_true', help='Disable adaptation to current structs (adapt is ON by default)')

    args = parser.parse_args()

    # Print banner
    print_banner()

    # Initialize agent and MCP client
    # Propagate MCP base URL to environment so all subcomponents pick it up
    if args.mcp_base:
        os.environ['BN_MCP_BASE_URL'] = args.mcp_base

    try:
        agent = MIPSReverseEngineeringAgent(model=args.model)
        mcp = BinaryNinjaMCPClient()
        print(f"✓ Agent initialized with model: {args.model}")
        # Warn if no MCP base URL configured; decompile will not work
        if not os.environ.get('BN_MCP_BASE_URL') and not os.environ.get('SMART_DIFF_BASE_URL'):
            print("! Warning: No BN_MCP_BASE_URL set; MCP decompile calls will be offline.\n  Set it, e.g.: export BN_MCP_BASE_URL=http://127.0.0.1:8011 or pass --mcp-base")

    except Exception as e:
        print(f"Error initializing agent: {e}")
        return 1

    # Execute command
    if args.interactive or not args.command:
        interactive_mode(agent, mcp)
    elif args.command == 'analyze':
        analyze_function_command(agent, mcp, args.binary, args.function, args.output)
    elif args.command == 'compare':
        compare_functions_command(agent, mcp, args.old_binary, args.new_binary,
                                 args.function, args.output)
    elif args.command == 'apply':
        ok, msg = apply_function_implementation(
            args.function,
            args.src_file,
            impl_root=args.impl_root,
            src_root=args.src_root,
            dry_run=args.dry_run,
            adapt=(not args.no_adapt),
        )
        print(msg)
        return 0 if ok else 1
    elif args.command == 'analyze':
        ok, msg = analyze_command(
            args.function,
            args.binary,
            args.src_file,
            impl_root=args.out_dir,
        )
        print(msg)
        return 0 if ok else 1

    else:
        parser.print_help()
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())

