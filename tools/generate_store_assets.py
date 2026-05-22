#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DISPLAY_BRIDGE = ROOT / "main" / "tools" / "display_bridge.py"
SCREENSHOT_DIR = ROOT / "store" / "screenshots"

SHOTS = [
    {
        "name": "01-binding.png",
        "status": "BINDING",
        "emoji": "🙂",
        "text": "code 482913 visit xiaozhi.me to bind",
        "code": "482913",
    },
    {
        "name": "02-idle.png",
        "status": "IDLE",
        "emoji": "😄",
        "text": "Ready",
        "code": "",
    },
    {
        "name": "03-listening.png",
        "status": "LISTENING",
        "emoji": "🤔",
        "text": "listening server vad",
        "code": "",
    },
    {
        "name": "04-speaking.png",
        "status": "SPEAKING",
        "emoji": "😊",
        "text": "Welcome to XiaoZhi. How can I help today?",
        "code": "",
    },
]


def render_shot(shot: dict[str, str]) -> None:
    SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = SCREENSHOT_DIR / shot["name"]
    env = os.environ.copy()
    env["XIAOZHI_RENDER_PNG_PATH"] = str(out_path)
    env["XIAOZHI_FBDEV"] = "/dev/null"
    proc = subprocess.run(
        [sys.executable, str(DISPLAY_BRIDGE)],
        input="\n".join(
            [
                json.dumps(
                    {
                        "cmd": "render",
                        "status": shot["status"],
                        "emoji": shot["emoji"],
                        "text": shot["text"],
                        "code": shot["code"],
                    }
                ),
                json.dumps({"cmd": "quit"}),
                "",
            ]
        ),
        text=True,
        capture_output=True,
        env=env,
        cwd=str(ROOT),
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"failed to render {shot['name']}: {proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    if not out_path.exists():
        raise RuntimeError(f"expected screenshot not generated: {out_path}")


def main() -> None:
    for shot in SHOTS:
        render_shot(shot)


if __name__ == "__main__":
    main()
