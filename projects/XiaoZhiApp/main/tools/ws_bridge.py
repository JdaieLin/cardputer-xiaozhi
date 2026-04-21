#!/usr/bin/env python3
import argparse
import asyncio
import base64
import json
import ssl
import sys
from typing import Any

try:
    import opuslib
    import websockets
except Exception as e:
    print(json.dumps({"event": "error", "text": f"missing dependency: {e}"}), flush=True)
    raise

SSL_CTX = ssl._create_unverified_context()


async def read_stdin_lines(queue: asyncio.Queue[str]) -> None:
    while True:
        line = await asyncio.to_thread(sys.stdin.readline)
        if line == "":
            await queue.put('{"cmd":"exit"}')
            return
        await queue.put(line.strip())


async def run_bridge(args: argparse.Namespace) -> None:
    headers = {
        "Authorization": f"Bearer {args.token}" if args.token else "Bearer ",
        "Protocol-Version": "1",
        "Device-Id": args.device_id,
        "Client-Id": args.client_id,
    }

    encoder = opuslib.Encoder(16000, 1, opuslib.APPLICATION_VOIP)
    decoder = opuslib.Decoder(16000, 1)
    frame_samples = 960
    frame_bytes = frame_samples * 2
    pcm_buf = bytearray()

    stdin_queue: asyncio.Queue[str] = asyncio.Queue()
    stdin_task = asyncio.create_task(read_stdin_lines(stdin_queue))

    session_id = ""
    ws_ssl = SSL_CTX if args.url.startswith("wss://") else None

    async with websockets.connect(
        args.url,
        ssl=ws_ssl,
        additional_headers=headers,
        ping_interval=20,
        ping_timeout=20,
        close_timeout=10,
        max_size=10 * 1024 * 1024,
        compression=None,
    ) as ws:
        hello = {
            "type": "hello",
            "version": 1,
            "transport": "websocket",
            "features": {"mcp": False},
            "audio_params": {
                "format": "opus",
                "sample_rate": 16000,
                "channels": 1,
                "frame_duration": 60,
            },
        }
        await ws.send(json.dumps(hello))

        first = await asyncio.wait_for(ws.recv(), timeout=10)
        if not isinstance(first, str):
            print(json.dumps({"event": "error", "text": "invalid hello response"}), flush=True)
            return

        hello_resp: dict[str, Any] = json.loads(first)
        session_id = hello_resp.get("session_id", "")
        print(json.dumps({"event": "connected"}), flush=True)

        async def recv_loop() -> None:
            async for msg in ws:
                if isinstance(msg, bytes):
                    pcm = decoder.decode(msg, frame_samples)
                    pcm_b64 = base64.b64encode(pcm).decode("ascii")
                    print(json.dumps({"event": "tts_audio", "pcm": pcm_b64}), flush=True)
                    continue

                data: dict[str, Any] = json.loads(msg)
                t = data.get("type")
                if t == "stt":
                    print(json.dumps({"event": "stt", "text": data.get("text", "")}), flush=True)
                elif t == "listen":
                    if data.get("state") == "stop":
                        print(json.dumps({"event": "listen_stop"}), flush=True)
                elif t == "tts":
                    state = data.get("state")
                    if state == "start":
                        print(json.dumps({"event": "tts_start"}), flush=True)
                    elif state == "stop":
                        print(json.dumps({"event": "tts_stop"}), flush=True)
                elif t == "goodbye":
                    print(json.dumps({"event": "error", "text": "server goodbye"}), flush=True)
                    return

        async def send_loop() -> None:
            nonlocal session_id, pcm_buf
            while True:
                line = await stdin_queue.get()
                if not line:
                    continue
                try:
                    cmd = json.loads(line)
                except Exception:
                    continue

                c = cmd.get("cmd")
                if c == "exit":
                    return
                if c == "listen_start":
                    await ws.send(json.dumps({
                        "session_id": session_id,
                        "type": "listen",
                        "state": "start",
                        "mode": "auto",
                    }))
                elif c == "listen_stop":
                    await ws.send(json.dumps({
                        "session_id": session_id,
                        "type": "listen",
                        "state": "stop",
                    }))
                elif c == "abort":
                    await ws.send(json.dumps({"session_id": session_id, "type": "abort"}))
                elif c == "audio":
                    pcm_b64 = cmd.get("pcm", "")
                    if not pcm_b64:
                        continue
                    pcm_buf.extend(base64.b64decode(pcm_b64))
                    while len(pcm_buf) >= frame_bytes:
                        frame = bytes(pcm_buf[:frame_bytes])
                        del pcm_buf[:frame_bytes]
                        opus = encoder.encode(frame, frame_samples)
                        await ws.send(opus)

        recv_task = asyncio.create_task(recv_loop())
        send_task = asyncio.create_task(send_loop())

        done, pending = await asyncio.wait({recv_task, send_task}, return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()

    stdin_task.cancel()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--token", default="")
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--client-id", required=True)
    args = parser.parse_args()

    try:
        asyncio.run(run_bridge(args))
    except Exception as e:
        print(json.dumps({"event": "error", "text": str(e)}), flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
