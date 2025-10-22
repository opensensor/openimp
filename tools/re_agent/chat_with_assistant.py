#!/usr/bin/env python3
"""
Chat with MIPS Reverse Engineering Assistant

This script provides an interactive chat interface with the OpenAI Assistant.
The assistant can call custom functions to analyze code, generate structs, etc.

Usage:
    python chat_with_assistant.py [assistant_id]
    
If assistant_id is not provided, it will be read from assistant_config.json
"""

import os
import sys
import json
import time
from pathlib import Path
from openai import OpenAI

# Add current directory to path
sys.path.insert(0, str(Path(__file__).parent))

from mips_re_agent import MIPSReverseEngineeringAgent, StructMember
from binja_mcp_client import BinaryNinjaMCPClient


class AssistantChat:
    """Interactive chat with the RE Assistant"""
    
    def __init__(self, assistant_id: str):
        """Initialize the chat"""
        self.client = OpenAI(api_key=os.getenv("OPENAI_API_KEY"))
        self.assistant_id = assistant_id
        self.thread = None
        self.agent = MIPSReverseEngineeringAgent(api_key=os.getenv("OPENAI_API_KEY"))
        self.mcp = BinaryNinjaMCPClient()
        
        # Create a thread for this conversation
        self.thread = self.client.beta.threads.create()
        print(f"✓ Started conversation thread: {self.thread.id}")
    
    def handle_function_call(self, tool_call):
        """Handle a function call from the assistant"""
        function_name = tool_call.function.name
        arguments = json.loads(tool_call.function.arguments)
        
        print(f"\n[Function Call: {function_name}]")
        
        try:
            if function_name == "analyze_decompilation":
                result = self.agent.analyze_decompilation(
                    arguments["decompiled_code"],
                    arguments["function_name"]
                )
                return json.dumps(result)
            
            elif function_name == "generate_struct_definition":
                members = [
                    StructMember(
                        name=m["name"],
                        offset=m["offset"],
                        size=m["size"],
                        type_name=m["type_name"],
                        description=m.get("description", "")
                    )
                    for m in arguments["members"]
                ]
                result = self.agent.generate_struct_definition(
                    arguments["struct_name"],
                    members
                )
                return result
            
            elif function_name == "generate_safe_access_code":
                result = self.agent.generate_safe_access_code(
                    arguments["struct_name"],
                    arguments["member_name"],
                    arguments.get("access_type", "read")
                )
                return result
            
            elif function_name == "compare_binary_versions":
                result = self.agent.compare_binary_versions(
                    arguments["old_decompilation"],
                    arguments["new_decompilation"],
                    arguments["function_name"]
                )
                return json.dumps(result)
            
            elif function_name == "list_binaries":
                binaries = self.mcp.list_binaries()
                result = {
                    "binaries": [
                        {
                            "binary_id": b.binary_id,
                            "name": b.name,
                            "architecture": b.architecture
                        }
                        for b in binaries
                    ]
                }
                return json.dumps(result)
            
            else:
                return json.dumps({"error": f"Unknown function: {function_name}"})
                
        except Exception as e:
            return json.dumps({"error": str(e)})
    
    def send_message(self, message: str):
        """Send a message and get response"""
        # Add message to thread
        self.client.beta.threads.messages.create(
            thread_id=self.thread.id,
            role="user",
            content=message
        )
        
        # Run the assistant
        run = self.client.beta.threads.runs.create(
            thread_id=self.thread.id,
            assistant_id=self.assistant_id
        )
        
        # Wait for completion
        while True:
            run = self.client.beta.threads.runs.retrieve(
                thread_id=self.thread.id,
                run_id=run.id
            )
            
            if run.status == "completed":
                break
            elif run.status == "requires_action":
                # Handle function calls
                tool_outputs = []
                for tool_call in run.required_action.submit_tool_outputs.tool_calls:
                    output = self.handle_function_call(tool_call)
                    tool_outputs.append({
                        "tool_call_id": tool_call.id,
                        "output": output
                    })
                
                # Submit tool outputs
                run = self.client.beta.threads.runs.submit_tool_outputs(
                    thread_id=self.thread.id,
                    run_id=run.id,
                    tool_outputs=tool_outputs
                )
            elif run.status in ["failed", "cancelled", "expired"]:
                print(f"Run failed with status: {run.status}")
                return None
            
            time.sleep(0.5)
        
        # Get the latest message
        messages = self.client.beta.threads.messages.list(
            thread_id=self.thread.id,
            order="desc",
            limit=1
        )
        
        if messages.data:
            return messages.data[0].content[0].text.value
        return None
    
    def chat(self):
        """Interactive chat loop"""
        print("\n" + "="*70)
        print("MIPS Reverse Engineering Assistant")
        print("="*70)
        print("\nType 'exit' or 'quit' to end the conversation")
        print("Type 'help' for usage tips\n")
        
        while True:
            try:
                user_input = input("You> ").strip()
                
                if not user_input:
                    continue
                
                if user_input.lower() in ['exit', 'quit', 'q']:
                    print("\nGoodbye!")
                    break
                
                if user_input.lower() == 'help':
                    self.show_help()
                    continue
                
                # Send message and get response
                print("\nAssistant> ", end="", flush=True)
                response = self.send_message(user_input)
                
                if response:
                    print(response)
                else:
                    print("(No response)")
                
                print()
                
            except KeyboardInterrupt:
                print("\n\nUse 'exit' to quit.")
            except Exception as e:
                print(f"\nError: {e}")
    
    def show_help(self):
        """Show help information"""
        help_text = """
Usage Tips:

The assistant can help you with:
  • Analyzing decompiled MIPS code
  • Generating struct definitions
  • Creating safe access patterns
  • Comparing binary versions
  • Answering questions about reverse engineering

Example questions:
  > Analyze this decompilation: [paste code]
  > Generate a struct for EncChannel with these offsets: 0x00, 0x3ac, 0x1094c4
  > What's the safe way to access offset 0x3ac?
  > Compare these two versions of IMP_System_Bind
  > List available binaries

The assistant will automatically call the appropriate functions to help you.
"""
        print(help_text)


def load_assistant_id():
    """Load assistant ID from config file"""
    config_file = Path(__file__).parent / "assistant_config.json"
    if config_file.exists():
        with open(config_file) as f:
            config = json.load(f)
            return config.get("assistant_id")
    return None


def main():
    """Main entry point"""
    if not os.getenv("OPENAI_API_KEY"):
        print("Error: OPENAI_API_KEY environment variable not set")
        print("Please set it with: export OPENAI_API_KEY='your-key-here'")
        return 1
    
    # Get assistant ID
    if len(sys.argv) > 1:
        assistant_id = sys.argv[1]
    else:
        assistant_id = load_assistant_id()
        if not assistant_id:
            print("Error: No assistant ID provided and no config file found")
            print("\nPlease either:")
            print("  1. Run create_assistant.py first to create an assistant")
            print("  2. Provide assistant ID: python chat_with_assistant.py <assistant_id>")
            return 1
    
    print(f"Connecting to assistant: {assistant_id}")
    
    try:
        chat = AssistantChat(assistant_id)
        chat.chat()
        return 0
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())

