"""
MIPS Reverse Engineering Agent

An OpenAI-powered agent for reverse engineering MIPS drivers using
Binary Ninja MCP smart diff tooling with safe struct member access patterns.
"""

from .mips_re_agent import (
    MIPSReverseEngineeringAgent,
    StructMember,
    StructLayout,
)

from .binja_mcp_client import (
    BinaryNinjaMCPClient,
    SmartDiffAnalyzer,
    BinaryInfo,
    FunctionInfo,
    FunctionDiff,
)

__version__ = "0.1.0"
__all__ = [
    "MIPSReverseEngineeringAgent",
    "StructMember",
    "StructLayout",
    "BinaryNinjaMCPClient",
    "SmartDiffAnalyzer",
    "BinaryInfo",
    "FunctionInfo",
    "FunctionDiff",
]

