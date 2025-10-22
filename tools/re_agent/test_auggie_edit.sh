#!/bin/bash
# Test script to verify Auggie can make code edits with --print --quiet --max-turns flags

set -e

echo "Testing Auggie code editing with new flags..."

# Create a test file
TEST_FILE="tools/re_agent/test_edit_target.c"
cat > "$TEST_FILE" << 'EOF'
#include <stdio.h>

int test_function(void) {
    return 0;
}
EOF

echo "Created test file: $TEST_FILE"
cat "$TEST_FILE"

# Try to edit it with Auggie
echo ""
echo "Calling Auggie to replace the function..."
timeout 60 auggie --print --quiet --max-turns 5 "Replace the implementation of test_function in $TEST_FILE to return 42 instead of 0. Use the str-replace-editor tool."

echo ""
echo "Result:"
cat "$TEST_FILE"

# Check if it worked
if grep -q "return 42" "$TEST_FILE"; then
    echo ""
    echo "✅ SUCCESS: Auggie successfully edited the file!"
    rm "$TEST_FILE"
    exit 0
else
    echo ""
    echo "❌ FAILED: File was not edited correctly"
    rm "$TEST_FILE"
    exit 1
fi

