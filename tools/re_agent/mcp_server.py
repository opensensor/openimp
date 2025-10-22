#!/usr/bin/env python3
"""
MCP Server for MIPS Reverse Engineering Agent

This MCP server exposes the RE agent functionality so it can be called
from Augment, Cline, or any other MCP-compatible tool.

Usage:
    python mcp_server.py

Then configure in your MCP client (e.g., Augment, Cline) to connect to this server.
"""

import json
import sys
from typing import Any, Dict, List, Optional

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember, StructLayout
from binja_mcp_client import BinaryNinjaMCPClient


class REAgentMCPServer:
    """MCP Server for the Reverse Engineering Agent"""
    
    def __init__(self):
        """Initialize the MCP server"""
        self.agent = None
        self.mcp_client = BinaryNinjaMCPClient()
        
        # Initialize agent lazily (when first needed)
        self._init_agent()
    
    def _init_agent(self):
        """Initialize the agent with OpenAI API key"""
        try:
            self.agent = MIPSReverseEngineeringAgent()
        except Exception as e:
            print(f"Warning: Could not initialize agent: {e}", file=sys.stderr)
            print("Make sure OPENAI_API_KEY is set", file=sys.stderr)
    
    def handle_request(self, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Handle an MCP request.
        
        Args:
            method: The method to call
            params: Parameters for the method
            
        Returns:
            Result dictionary
        """
        if method == "analyze_decompilation":
            return self._analyze_decompilation(params)
        elif method == "generate_struct":
            return self._generate_struct(params)
        elif method == "generate_safe_access":
            return self._generate_safe_access(params)
        elif method == "compare_versions":
            return self._compare_versions(params)
        elif method == "ask_agent":
            return self._ask_agent(params)
        elif method == "list_binaries":
            return self._list_binaries(params)
        else:
            return {"error": f"Unknown method: {method}"}
    
    def _analyze_decompilation(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Analyze decompiled code.
        
        Params:
            decompiled_code: The decompiled C code
            function_name: Name of the function
            
        Returns:
            Analysis results
        """
        if not self.agent:
            return {"error": "Agent not initialized. Set OPENAI_API_KEY."}
        
        code = params.get("decompiled_code", "")
        func_name = params.get("function_name", "unknown")
        
        result = self.agent.analyze_decompilation(code, func_name)
        return {"success": True, "result": result}
    
    def _generate_struct(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Generate a struct definition.
        
        Params:
            struct_name: Name of the struct
            members: List of member dictionaries with keys: name, offset, size, type_name, description
            
        Returns:
            Struct definition as string
        """
        if not self.agent:
            return {"error": "Agent not initialized. Set OPENAI_API_KEY."}
        
        struct_name = params.get("struct_name", "UnknownStruct")
        members_data = params.get("members", [])
        
        # Convert dictionaries to StructMember objects
        members = [
            StructMember(
                name=m.get("name", "unknown"),
                offset=m.get("offset", 0),
                size=m.get("size", 4),
                type_name=m.get("type_name", "uint32_t"),
                description=m.get("description", "")
            )
            for m in members_data
        ]
        
        struct_def = self.agent.generate_struct_definition(struct_name, members)
        return {"success": True, "struct_definition": struct_def}
    
    def _generate_safe_access(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Generate safe access code.
        
        Params:
            struct_name: Name of the struct
            member_name: Name of the member
            access_type: "read" or "write"
            
        Returns:
            Safe access code
        """
        if not self.agent:
            return {"error": "Agent not initialized. Set OPENAI_API_KEY."}
        
        struct_name = params.get("struct_name", "")
        member_name = params.get("member_name", "")
        access_type = params.get("access_type", "read")
        
        code = self.agent.generate_safe_access_code(struct_name, member_name, access_type)
        return {"success": True, "code": code}
    
    def _compare_versions(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Compare two versions of a function.
        
        Params:
            old_code: Old version decompiled code
            new_code: New version decompiled code
            function_name: Name of the function
            
        Returns:
            Comparison results
        """
        if not self.agent:
            return {"error": "Agent not initialized. Set OPENAI_API_KEY."}
        
        old_code = params.get("old_code", "")
        new_code = params.get("new_code", "")
        func_name = params.get("function_name", "unknown")
        
        result = self.agent.compare_binary_versions(old_code, new_code, func_name)
        return {"success": True, "result": result}
    
    def _ask_agent(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Ask the agent a question.
        
        Params:
            question: The question to ask
            
        Returns:
            Agent's response
        """
        if not self.agent:
            return {"error": "Agent not initialized. Set OPENAI_API_KEY."}
        
        question = params.get("question", "")
        response = self.agent.ask(question)
        return {"success": True, "response": response}
    
    def _list_binaries(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        List available binaries.
        
        Returns:
            List of binary information
        """
        binaries = self.mcp_client.list_binaries()
        return {
            "success": True,
            "binaries": [
                {
                    "binary_id": b.binary_id,
                    "name": b.name,
                    "architecture": b.architecture,
                    "base_address": b.base_address
                }
                for b in binaries
            ]
        }
    
    def run(self):
        """Run the MCP server (stdio mode)"""
        print("MIPS RE Agent MCP Server started", file=sys.stderr)
        print("Waiting for requests on stdin...", file=sys.stderr)
        
        for line in sys.stdin:
            try:
                request = json.loads(line.strip())
                method = request.get("method", "")
                params = request.get("params", {})
                
                result = self.handle_request(method, params)
                
                # Send response
                response = {
                    "id": request.get("id"),
                    "result": result
                }
                print(json.dumps(response), flush=True)
                
            except json.JSONDecodeError as e:
                error_response = {
                    "error": f"Invalid JSON: {e}"
                }
                print(json.dumps(error_response), flush=True)
            except Exception as e:
                error_response = {
                    "error": f"Server error: {e}"
                }
                print(json.dumps(error_response), flush=True)


def main():
    """Main entry point"""
    server = REAgentMCPServer()
    server.run()


if __name__ == "__main__":
    main()

