#!/usr/bin/env python3
"""Run an on-device camera capture through the complete MCP vision upload path."""

import asyncio
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1] / "main" / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from mcp_tools import McpTools  # noqa: E402


class VisionHandler(BaseHTTPRequestHandler):
    received = b""
    headers_seen = {}

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        type(self).received = self.rfile.read(length)
        type(self).headers_seen = dict(self.headers.items())
        payload = json.dumps({"answer": "camera smoke ok"}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, _format, *_args):
        return


async def run_smoke(port: int) -> None:
    tools = McpTools(device_id="smoke-device", client_id="smoke-client")
    assert "self.camera.take_photo" in tools.tools, "camera tool was not advertised"
    await tools.handle({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {
                "vision": {"url": f"http://127.0.0.1:{port}/explain", "token": "smoke-token"}
            },
        },
    })
    response = await tools.handle({
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {
            "name": "self.camera.take_photo",
            "arguments": {"question": "What is in front of the camera?"},
        },
    })
    content = response["result"]["content"][0]
    assert json.loads(content["text"])["answer"] == "camera smoke ok"
    assert b'filename="camera.jpg"' in VisionHandler.received
    assert b"Content-Type: image/jpeg" in VisionHandler.received
    assert b"\xff\xd8" in VisionHandler.received
    assert VisionHandler.headers_seen.get("Authorization") == "Bearer smoke-token"
    assert VisionHandler.headers_seen.get("Device-Id") == "smoke-device"
    assert VisionHandler.headers_seen.get("Client-Id") == "smoke-client"


def main() -> None:
    server = ThreadingHTTPServer(("127.0.0.1", 0), VisionHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        asyncio.run(run_smoke(server.server_port))
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    print("camera MCP smoke test passed")


if __name__ == "__main__":
    main()
