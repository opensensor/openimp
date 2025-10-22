#!/usr/bin/env python3
"""
MIPS Driver Reverse Engineering Agent

An OpenAI agent specialized in reverse engineering MIPS drivers using Binary Ninja MCP
smart diff tooling with safe struct member access patterns.

This agent helps with:
- Decompiling MIPS binaries using Binary Ninja MCP
- Analyzing struct layouts and member offsets
- Generating safe struct access code
- Comparing binary versions to identify changes
- Creating C implementations from decompiled code
"""

import os
import json
import re
from typing import Dict, List, Optional, Any
from dataclasses import dataclass
from openai import OpenAI
from openai.types.chat import ChatCompletionMessageParam


@dataclass
class StructMember:
    """Represents a struct member with offset and type information"""
    name: str
    offset: int
    size: int
    type_name: str
    description: str = ""


@dataclass
class StructLayout:
    """Represents a complete struct layout"""
    name: str
    total_size: int
    members: List[StructMember]
    platform: str = "T31"


class MIPSReverseEngineeringAgent:
    """
    OpenAI agent for reverse engineering MIPS drivers with Binary Ninja MCP integration.
    
    This agent follows safe struct member access patterns:
    1. Typed access: struct->member
    2. memcpy(): memcpy(&value, ptr + offset, sizeof(value))
    3. Never: Direct pointer arithmetic like *(uint32_t*)(ptr + offset)
    """
    
    def __init__(self, api_key: Optional[str] = None, model: str = "gpt-4o"):
        """Initialize the agent with OpenAI API"""
        self.client = OpenAI(api_key=api_key or os.getenv("OPENAI_API_KEY"))
        self.model = model
        self.conversation_history: List[ChatCompletionMessageParam] = []
        self.struct_database: Dict[str, StructLayout] = {}
        
        # Initialize with system prompt
        self.system_prompt = self._create_system_prompt()
        self.conversation_history.append({
            "role": "system",
            "content": self.system_prompt
        })
    
    def _create_system_prompt(self) -> str:
        """Create the system prompt for the agent"""
        return """You are an expert MIPS reverse engineering agent specializing in Ingenic SoC drivers.

Your expertise includes:
- Analyzing Binary Ninja MCP decompilations of MIPS binaries
- Understanding Ingenic IMP (Ingenic Media Platform) driver architecture
- Identifying struct layouts and member offsets from decompiled code
- Generating safe C code that follows proper struct access patterns
- Comparing binary versions to identify API changes
- Reviewing and fixing existing implementations

CRITICAL RULES FOR STRUCT ACCESS:
1. ALWAYS use typed access (struct->member) when the struct definition is known
2. Use memcpy() for safe access when working with offsets: memcpy(&value, ptr + offset, sizeof(value))
3. NEVER use direct pointer arithmetic like *(uint32_t*)(ptr + offset) - this is unsafe
4. Document all struct offsets discovered from decompilation with comments
5. Preserve exact byte offsets - changing them breaks binary compatibility

FUNCTION HANDLING STRATEGY:

ONLY FUNCTIONS THAT EXIST IN OEM BINARY (e.g., libimp.so) should be analyzed:
   - These MUST be reverse-engineered from Binary Ninja decompilation
   - Extract struct offsets from pointer arithmetic in decompiled code
   - Generate struct definitions with proper padding
   - Create safe implementation that matches OEM behavior exactly
   - Example: IMP_Encoder_CreateGroup, IMP_System_Init

FUNCTIONS THAT DON'T EXIST IN OEM BINARY are SKIPPED:
   - These are likely dead code or incorrect abstractions
   - They probably shouldn't exist in the codebase
   - Callers should call OEM functions directly instead
   - The agent will skip these automatically

You will ONLY receive functions that exist in the OEM binary.
Your job is to reverse-engineer them from Binary Ninja decompilations.

WORKFLOW (for OEM functions only):
1. Analyze Binary Ninja decompilation
2. Extract struct offsets from pointer arithmetic
3. Create struct definitions with proper member types
4. Generate safe implementation using typed access
5. Document all offsets with comments

PLATFORMS:
- T31 (primary): Ingenic T31 SoC
- T23, T40, T41, C100: Other Ingenic platforms with slight variations

Always ask for clarification if struct layouts are ambiguous."""

    def add_struct_to_database(self, struct: StructLayout):
        """Add a struct layout to the database"""
        self.struct_database[struct.name] = struct
    
    def get_struct_from_database(self, name: str) -> Optional[StructLayout]:
        """Retrieve a struct layout from the database"""
        return self.struct_database.get(name)
    
    def analyze_decompilation(self, decompiled_code: str, function_name: str) -> Dict[str, Any]:
        """
        Analyze decompiled code to extract struct information and generate safe implementations.
        
        Args:
            decompiled_code: The decompiled C code from Binary Ninja
            function_name: Name of the function being analyzed
            
        Returns:
            Dictionary containing analysis results
        """
        prompt = f"""Analyze this Binary Ninja decompilation of the function '{function_name}':

```c
{decompiled_code}
```

Please provide:
1. Identified struct offsets (e.g., ptr + 0x10, ptr + 0x20)
2. Inferred member types based on usage
3. Suggested struct definition with proper types
4. Safe C implementation using typed struct access
5. Any validation checks or error handling patterns

CRITICAL: Format your response as JSON with these keys:
- "offsets": dict mapping offset strings to descriptions
- "struct_definition": string containing the complete C struct definition
- "safe_implementation": string containing ONLY valid C code (the complete function implementation)
- "notes": dict or string with additional notes

IMPORTANT: The "safe_implementation" field MUST contain valid C code as a plain string, NOT a dict or object.
Example format:
{{
  "offsets": {{"0x20": "file descriptor", "0x24": "status"}},
  "struct_definition": "typedef struct {{ int fd; uint32_t status; }} MyStruct;",
  "safe_implementation": "int my_function(MyStruct *s) {{\\n    if (s == NULL) return -1;\\n    return s->fd;\\n}}",
  "notes": "Function validates pointer before access"
}}"""

        response = self._chat(prompt)
        
        # Try to parse JSON from response
        try:
            # Extract JSON from markdown code blocks if present
            json_match = re.search(r'```json\s*(\{.*?\})\s*```', response, re.DOTALL)
            if json_match:
                return json.loads(json_match.group(1))
            else:
                # Try to parse the whole response
                return json.loads(response)
        except json.JSONDecodeError:
            # Return structured response even if not JSON
            return {
                "raw_analysis": response,
                "offsets": [],
                "struct_definition": "",
                "safe_implementation": "",
                "notes": response
            }
    
    def generate_struct_definition(self, struct_name: str, members: List[StructMember]) -> str:
        """
        Generate a C struct definition from member information.
        
        Args:
            struct_name: Name of the struct
            members: List of struct members with offsets
            
        Returns:
            C struct definition as a string
        """
        # Sort members by offset
        sorted_members = sorted(members, key=lambda m: m.offset)
        
        lines = [f"typedef struct {struct_name} {{"]
        
        current_offset = 0
        for member in sorted_members:
            # Add padding if needed
            if member.offset > current_offset:
                padding_size = member.offset - current_offset
                lines.append(f"    uint8_t padding_{current_offset:04x}[{padding_size}]; /* 0x{current_offset:04x} */")
                current_offset = member.offset
            
            # Add member with offset comment
            comment = f"/* 0x{member.offset:04x}"
            if member.description:
                comment += f": {member.description}"
            comment += " */"
            
            lines.append(f"    {member.type_name} {member.name}; {comment}")
            current_offset = member.offset + member.size
        
        lines.append(f"}} {struct_name};")
        
        return "\n".join(lines)
    
    def generate_safe_access_code(self, struct_name: str, member_name: str, 
                                   access_type: str = "read") -> str:
        """
        Generate safe struct member access code.
        
        Args:
            struct_name: Name of the struct
            member_name: Name of the member to access
            access_type: "read" or "write"
            
        Returns:
            Safe C code for accessing the member
        """
        if access_type == "read":
            return f"""// Safe read access
{struct_name} *s = ({struct_name} *)ptr;
value = s->{member_name};"""
        else:
            return f"""// Safe write access
{struct_name} *s = ({struct_name} *)ptr;
s->{member_name} = value;"""
    
    def compare_binary_versions(self, old_decompilation: str, new_decompilation: str,
                                function_name: str) -> Dict[str, Any]:
        """
        Compare two versions of a decompiled function to identify changes.
        
        Args:
            old_decompilation: Decompiled code from old binary
            new_decompilation: Decompiled code from new binary
            function_name: Name of the function
            
        Returns:
            Dictionary containing comparison results
        """
        prompt = f"""Compare these two versions of the function '{function_name}':

OLD VERSION:
```c
{old_decompilation}
```

NEW VERSION:
```c
{new_decompilation}
```

Identify:
1. Changed struct offsets (critical for compatibility)
2. New or removed members
3. Changed logic or behavior
4. API signature changes
5. Recommendations for updating our implementation

Format as JSON with keys: offset_changes, member_changes, logic_changes, api_changes, recommendations"""

        response = self._chat(prompt)
        
        try:
            json_match = re.search(r'```json\s*(\{.*?\})\s*```', response, re.DOTALL)
            if json_match:
                return json.loads(json_match.group(1))
            else:
                return json.loads(response)
        except json.JSONDecodeError:
            return {"raw_comparison": response}
    
    def _chat(self, user_message: str) -> str:
        """Send a message and get a response"""
        self.conversation_history.append({
            "role": "user",
            "content": user_message
        })
        
        response = self.client.chat.completions.create(
            model=self.model,
            messages=self.conversation_history,
            temperature=0.1,  # Low temperature for consistent technical analysis
        )
        
        assistant_message = response.choices[0].message.content
        self.conversation_history.append({
            "role": "assistant",
            "content": assistant_message
        })
        
        return assistant_message
    
    def ask(self, question: str) -> str:
        """Ask the agent a question"""
        return self._chat(question)
    
    def reset_conversation(self):
        """Reset the conversation history"""
        self.conversation_history = [{
            "role": "system",
            "content": self.system_prompt
        }]

