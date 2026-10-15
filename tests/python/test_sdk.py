"""Exercise Manwise with the official MCP SDK, in both supported protocol modes."""
import asyncio
from pathlib import Path
import sys

from mcp import Client
from mcp.client.stdio import StdioServerParameters


async def main():
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else "build/manwise").resolve()
    for mode in ("auto", "legacy"):
        connection = StdioServerParameters(command=str(binary), args=["serve"])
        async with Client(connection, mode=mode, read_timeout_seconds=10) as client:
            tools = await client.list_tools()
            assert {t.name for t in tools.tools} == {"read_manual", "find_in_manual", "search_manuals"}
            page = await client.call_tool("read_manual", {"topic": "man", "section": "1", "limit": 20})
            assert not page.is_error, page
            assert page.structured_content["source"]
            assert "NAME" in page.structured_content["text"]
            found = await client.call_tool("find_in_manual", {"topic": "man", "section": "1", "query": "MANPATH"})
            assert not found.is_error and found.structured_content["matches"], found
            assert found.structured_content["cache_hit"]
            searched = await client.call_tool("search_manuals", {"query": "printf", "limit": 3})
            assert not searched.is_error, searched
            invalid = await client.call_tool("read_manual", {"topic": "../file"})
            assert invalid.is_error and invalid.structured_content["code"] == "invalid_arguments"
            print(f"Official MCP client passed: {mode}, protocol {client.protocol_version}")


if __name__ == "__main__":
    asyncio.run(main())
