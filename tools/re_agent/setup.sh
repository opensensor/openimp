#!/bin/bash
# Setup script for MIPS Reverse Engineering Agent

set -e

echo "=========================================="
echo "MIPS RE Agent Setup"
echo "=========================================="
echo ""

# Check Python version
echo "Checking Python version..."
python3 --version || {
    echo "Error: Python 3 is required"
    exit 1
}

# Check for pip
echo "Checking for pip..."
python3 -m pip --version || {
    echo "Error: pip is required"
    exit 1
}

# Install dependencies
echo ""
echo "Installing dependencies..."
python3 -m pip install -r requirements.txt --break-system-packages

# Check for OpenAI API key
echo ""
if [ -z "$OPENAI_API_KEY" ]; then
    echo "⚠️  Warning: OPENAI_API_KEY environment variable is not set"
    echo ""
    echo "To use the agent, you need to set your OpenAI API key:"
    echo "  export OPENAI_API_KEY='your-api-key-here'"
    echo ""
    echo "You can get an API key from: https://platform.openai.com/api-keys"
else
    echo "✓ OPENAI_API_KEY is set"
fi

# Make scripts executable
echo ""
echo "Making scripts executable..."
chmod +x re_agent_cli.py
chmod +x examples/*.py

echo ""
echo "=========================================="
echo "Setup Complete!"
echo "=========================================="
echo ""
echo "Quick Start:"
echo "  1. Set your OpenAI API key (if not already set):"
echo "     export OPENAI_API_KEY='your-key-here'"
echo ""
echo "  2. Run interactive mode:"
echo "     ./re_agent_cli.py -i"
echo ""
echo "  3. Try the examples:"
echo "     cd examples"
echo "     python3 analyze_encoder.py"
echo "     python3 compare_versions.py"
echo "     python3 openimp_workflow.py"
echo ""
echo "  4. See README.md for full documentation"
echo ""

