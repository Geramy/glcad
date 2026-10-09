#!/usr/bin/env python3
"""Check discovery and read-only tools on the running app's settings-enabled MCP server."""
import argparse
import json
import os
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:5912/mcp")
    parser.add_argument("--token-env", help="Environment variable containing the optional access token")
    args = parser.parse_args()
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
               "MCP-Protocol-Version": "2025-11-25"}
    if args.token_env:
        headers["Authorization"] = "Bearer " + os.environ[args.token_env]
    def call(number, method, params):
        body = json.dumps(dict(jsonrpc="2.0", id=number, method=method, params=params)).encode()
        request = urllib.request.Request(args.url, data=body, headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            result = json.load(response)
        assert "error" not in result, result
        return result["result"]
    call(1, "initialize", dict(protocolVersion="2025-11-25", capabilities={}, clientInfo=dict(name="smoke-test", version="1")))
    tools = call(2, "tools/list", {})["tools"]
    info = call(3, "tools/call", dict(name="document_info", arguments={}))
    assert not info["isError"]
    doc = info["structuredContent"]["document_id"]
    summary = call(4, "tools/call", dict(name="document_summary", arguments=dict(document_id=doc)))
    assert not summary["isError"]
    print(f"MCP HTTP passed: {len(tools)} tools discovered; active document inspected without edits")


if __name__ == "__main__":
    main()
