#!/bin/bash
# Test script for the new RE-Agent + Auggie workflow

set -e  # Exit on error

echo "========================================="
echo "RE-Agent + Auggie Workflow Test"
echo "========================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Test function
FUNCTION_NAME="IMP_ISP_AddSensor"
BINARY_ID="port_9009"
OUTPUT_DIR="tools/re_agent/full_review_output"

echo -e "${YELLOW}Step 1: Clean up previous artifacts${NC}"
rm -rf "$OUTPUT_DIR/auggie_artifacts"
rm -f "$OUTPUT_DIR/implementations/$FUNCTION_NAME.c"
echo "✓ Cleaned up"
echo ""

echo -e "${YELLOW}Step 2: Run RE-Agent analysis${NC}"
echo "Command: python tools/re_agent/re_agent_cli.py analyze $BINARY_ID $FUNCTION_NAME"
echo ""

# Note: This will fail if Binary Ninja MCP is not running
# We'll just show what would happen
echo -e "${GREEN}Expected output (without --apply):${NC}"
echo "  ✓ Analysis complete for $FUNCTION_NAME"
echo "  ✓ Implementation saved to $OUTPUT_DIR/implementations/$FUNCTION_NAME.c"
echo "  ✓ Generated N Auggie artifact(s) in $OUTPUT_DIR/auggie_artifacts"
echo ""

echo -e "${YELLOW}Step 2b: Auto-apply mode (NEW!)${NC}"
echo "Command: python tools/re_agent/re_agent_cli.py"
echo "  RE-Agent> analyze $BINARY_ID $FUNCTION_NAME --apply"
echo ""
echo -e "${GREEN}Expected output (with --apply):${NC}"
echo "  ✓ Analysis complete for $FUNCTION_NAME"
echo "  🔧 Auto-applying changes via Auggie..."
echo "    → Applying function implementation..."
echo "      ✓ Function implementation applied successfully"
echo "    → Applying struct update for ISPDevice..."
echo "      ✓ Struct update applied successfully"
echo "  ✅ Auto-apply complete!"
echo ""

echo -e "${YELLOW}Step 3: Check for Auggie CLI${NC}"
if command -v auggie &> /dev/null; then
    echo -e "${GREEN}✓ Auggie CLI is installed${NC}"
    auggie --version || true
else
    echo -e "${RED}✗ Auggie CLI not found${NC}"
    echo "  Install with: npm install -g @augmentcode/auggie"
    echo "  (Requires Node.js 22+)"
fi
echo ""

echo -e "${YELLOW}Step 4: What artifacts would be generated${NC}"
echo "Expected files:"
echo "  - $OUTPUT_DIR/auggie_artifacts/$FUNCTION_NAME.json"
echo "  - $OUTPUT_DIR/auggie_artifacts/ISPDevice_update.json (if struct updates needed)"
echo ""

echo -e "${YELLOW}Step 5: How to apply changes${NC}"
echo "After running the analysis, you would:"
echo ""
echo "  # Review the artifacts"
echo "  cat $OUTPUT_DIR/auggie_artifacts/$FUNCTION_NAME.json"
echo ""
echo "  # Dry-run to see what Auggie will do"
echo "  python tools/re_agent/auggie_apply.py --function $FUNCTION_NAME --dry-run"
echo ""
echo "  # Apply the changes"
echo "  python tools/re_agent/auggie_apply.py --function $FUNCTION_NAME"
echo ""

echo "========================================="
echo "Workflow Overview"
echo "========================================="
echo ""
echo "1. RE-Agent analyzes and generates artifacts (JSON files)"
echo "2. You review the artifacts"
echo "3. Auggie applies the changes using proper code editing tools"
echo ""
echo "Benefits:"
echo "  ✓ No regex parsing of C code"
echo "  ✓ Reviewable artifacts before applying"
echo "  ✓ Safe, precise edits with Auggie"
echo "  ✓ Clear separation of concerns"
echo ""

echo -e "${GREEN}Test complete!${NC}"
echo ""
echo "To run the actual workflow:"
echo ""
echo "  # Auto-apply mode (recommended):"
echo "  python tools/re_agent/re_agent_cli.py"
echo "  RE-Agent> analyze $BINARY_ID $FUNCTION_NAME --apply"
echo ""
echo "  # Manual mode (review before apply):"
echo "  python tools/re_agent/re_agent_cli.py"
echo "  RE-Agent> analyze $BINARY_ID $FUNCTION_NAME"
echo "  python tools/re_agent/auggie_apply.py --function $FUNCTION_NAME"

