# cardputer-xiaozhi

[![Build deb](https://github.com/JdaieLin/cardputer-xiaozhi/actions/workflows/build-deb.yml/badge.svg)](https://github.com/JdaieLin/cardputer-xiaozhi/actions/workflows/build-deb.yml)

XiaoZhi voice assistant ported to M5Cardputer (Raspberry Pi Zero + framebuffer display).

## Features

- Two-way voice conversation via WebSocket (Opus codec)
- Framebuffer UI with CJK + emoji rendering
- Optional audio-reactive Whisplay watercolor orb UI
- OTA activation code flow for xiaozhi.me binding
- Push-to-talk (SPACE key) with server-side VAD
- Continuous conversation mode (auto re-listen after TTS finishes)
- MCP local command, web search, and camera vision tools
- On-screen green terminal progress for tool calls
- Whisplay-style SVG emoji artwork downloaded during installation, with font fallback
- macOS simulator for local development

## Project layout

```
cardputer-xiaozhi/
  .env.template                  # Environment variable template
  build.sh                       # Multi-target build script
  SConstruct                     # SCons build file (optional)
  app-builder.json               # Package metadata
  config_defaults.mk             # Build config
  tools/
    deploy_rpi_199.sh            # Deploy to Raspberry Pi (192.168.100.199)
    package_applaunch.sh         # Create .deb package for APPLaunch
    install.sh                   # Dependency + font installer
    fetch_fonts.sh               # Download fonts for CI bundling
  .github/workflows/
    build-deb.yml                # CI: auto-build arm64 .deb
  main/
    include/                     # Headers
    src/                         # C++ sources
    tools/
      display_bridge.py          # Python PIL framebuffer renderer
      mcp_tools.py               # Safe local command + web search MCP tools
      ws_bridge.py               # Python WebSocket + Opus bridge
```

## Quick start

### Build for Raspberry Pi (device)

```bash
# On the Pi:
sudo apt-get install -y libsdl2-dev libsdl2-ttf-dev libopus0 fonts-noto-cjk python3 python3-pil python3-cairosvg curl unzip
./build.sh --device
```

### Build the .deb package

```bash
./build.sh --device --package
# Output: build/cardputerzero-xiaozhi_0.2.6-m5stack1_arm64.deb
```

### Cross-compile from macOS (aarch64)

```bash
brew install zig
./build.sh --aarch64 --package
```

### Local simulator (macOS)

```bash
brew install sdl2 opus
python3 -m pip install websockets
./build.sh --sim
./build/xiaozhi_simulator
```

## CI / Automatic builds

Every push to `main` or `ci/**` branches triggers an automatic arm64 `.deb` build
and publishes a prerelease. Tagged pushes (`v*`) attach the `.deb` to the release,
with the package version derived from the tag name, so `v0.2.6` builds
`cardputerzero-xiaozhi_0.2.6-..._arm64.deb`.

Pre-built packages are available on the [Releases](https://github.com/JdaieLin/cardputer-xiaozhi/releases) page.

## App Store submission

This repository now includes CardputerZero App Store metadata in [`app-builder.json`](./app-builder.json)
and store assets in [`store/`](./store/).

Recommended publish flow:

```bash
./build.sh --device --package
python3 /path/to/prepublish_check.py --deb build/cardputerzero-xiaozhi_0.2.6-m5stack1_arm64.deb --app-dir .
czdev login
czdev publish --deb build/cardputerzero-xiaozhi_0.2.6-m5stack1_arm64.deb
```

To regenerate the listing screenshots:

```bash
/private/tmp/cardputer-xiaozhi-venv/bin/python tools/generate_store_assets.py
```

## Deployment to Raspberry Pi

```bash
./tools/deploy_rpi_199.sh
```

This script:
1. Installs system dependencies on the Pi and verifies vendored Python dependencies
2. Uploads source and builds `--device --package` on the Pi
3. Installs the `.deb` with `dpkg -i` and restarts `APPLaunch.service`

### Manual install

```bash
scp build/cardputerzero-xiaozhi_0.2.6-m5stack1_arm64.deb pi@192.168.100.199:/tmp/
ssh pi@192.168.100.199 "sudo dpkg -i /tmp/cardputerzero-xiaozhi_0.2.6-m5stack1_arm64.deb"
ssh pi@192.168.100.199 "sudo systemctl restart APPLaunch.service"
```

## Simulator controls

| Key | Action |
|-----|--------|
| `SPACE` / `ENTER` | Wake / push-to-talk |
| `X` | End the current conversation and return to idle |
| `O` | Toggle classic / watercolor display mode |
| `Esc` | Return to app launcher |
| Close window / `Ctrl+C` | Exit |

Listening stop is decided by server-side VAD.

## Configuration

Copy `.env.template` and edit:

```bash
cp .env.template .env
```

Key variables:
- `XIAOZHI_WS_URL` — WebSocket server endpoint
- `XIAOZHI_WS_TOKEN` — Authentication token
- `XIAOZHI_DEVICE_ID` / `XIAOZHI_CLIENT_ID` — Device identity

### MCP tools

The WebSocket bridge advertises MCP support and registers these tools by default:

- `local_command` — runs commands without a shell and returns stdout, stderr, and exit code.
- `checkCommand` / `stopCommand` — inspect or stop commands that outlive the foreground timeout.
- `web_search` — searches DuckDuckGo HTML, or Google News RSS with `search_type=news`.
- `self.camera.take_photo` — captures a JPEG from the CSI camera and asks the
  XiaoZhi vision service the supplied `question`. It is advertised only when
  both the configured camera device and `rpicam-still` are available.

`local_command` is restricted to the allowlist in `XIAOZHI_LOCAL_COMMAND_ALLOWLIST`.
Arbitrary commands require `XIAOZHI_LOCAL_COMMAND_ALLOW_DANGEROUS=true`; shell syntax
additionally requires `XIAOZHI_LOCAL_COMMAND_USE_SHELL=true`. The legacy
`XIAOZHI_LOCAL_COMMAND_UNSAFE` variable remains a compatibility alias. Tool progress is
rendered as small green text in place of the normal status text while the emoji remains in
its usual position. It clears as soon as speech starts, or five seconds after the last tool
update when no spoken response follows.

When dangerous mode is enabled, `XIAOZHI_LOCAL_COMMAND_SUDO_PASSWORD` may hold the local
sudo password. If configured, the MCP tool description gives the model only the fixed
`{{SUDO_PASSWORD}}` placeholder. A call such as
`sudo {{SUDO_PASSWORD}} apt-get update` is executed as `sudo -S -p '' apt-get update`, with
the secret written directly to stdin. The secret is removed from the Python environment,
never inserted into the command line, and redacted from progress and tool results.
The APPLaunch package loads device-private tool settings from
`~/.config/xiaozhi/tools.env`; keep that file mode `0600` and do not package it.

Web search uses `XIAOZHI_WEB_TOOL_PROXY=http://192.168.100.124:7890` by default in the
packaged APPLaunch launcher. Override or clear that environment variable to use another
proxy or a direct connection.

Camera capture defaults to `/dev/video0`, 640×480 JPEG at quality 80. The MCP
initialize message supplies the session-specific vision URL and token; photos
are posted directly to that endpoint and are not saved to disk. Configure the
capture path with the `XIAOZHI_CAMERA_*` variables in `.env.template`. Raspberry
Pi systems must reserve enough contiguous memory for libcamera; the tested
CardputerZero configuration uses `cma=64M`.

Whisplay emoji SVG files are not stored in this repository or embedded in the `.deb`.
During installation, `tools/install.sh` downloads `emoji_svg.zip` from Whisplay's asset
server and extracts it beside `display_bridge.py`. Override the source with
`XIAOZHI_EMOJI_ASSET_URL`; the renderer falls back to the bundled emoji font if the
download is unavailable.

### Watercolor orb UI

Set `XIAOZHI_DISPLAY_UI_STYLE=watercolor` in `~/.config/xiaozhi/tools.env` to
enable the audio-reactive renderer ported from `whisplay-xiaozhi` v1.4.0. The orb is
drawn on the right at roughly two-thirds of the 170-pixel display height; the
current emoji becomes a small lower-left badge overlapping its edge. Classic
mode remains the default.

The repository and `.deb` include Whisplay v1.4.0's precompiled aarch64 Rust
renderer beside `display_bridge.py`, together with its GPLv3 license. The
installer verifies the pinned SHA-256 and downloads a replacement only when
the bundled file is missing or invalid. Override the fallback source with
`XIAOZHI_WATERCOLOR_RENDERER_URL` and the matching
`XIAOZHI_WATERCOLOR_RENDERER_SHA256` when needed. Emoji SVG artwork remains an
install-time download and is not stored in this repository or the `.deb`.

| Variable | Default | Purpose |
|---|---:|---|
| `XIAOZHI_DISPLAY_UI_STYLE` | `classic` | Select `classic` or `watercolor` |
| `XIAOZHI_WATERCOLOR_FPS` | `15` | Animation frame rate (1–20) |
| `XIAOZHI_WATERCOLOR_DIAMETER` | `113` | Orb diameter in pixels |
| `XIAOZHI_WATERCOLOR_RENDER_SCALE` | `0.60` | High-quality internal render scale (0.2–1.0) |
| `XIAOZHI_WATERCOLOR_SMOOTH_FBM` | `true` | Smooth pigment boundaries |
| `XIAOZHI_WATERCOLOR_TEMPORAL_3D` | `true` | Enable temporal watercolor detail |
| `XIAOZHI_WATERCOLOR_MIN_MEM_AVAILABLE_MB` | `64` | Pause watercolor before a local command below this available-memory level |
| `XIAOZHI_WATERCOLOR_CRITICAL_MEM_AVAILABLE_MB` | `32` | Emergency pause level, even if a command lifecycle event is delayed |
| `XIAOZHI_WATERCOLOR_MIN_CMA_FREE_MB` | `0` | Optional free-CMA threshold; `0` disables this additional check |
| `XIAOZHI_WATERCOLOR_RECOVERY_DELAY_SEC` | `5` | Healthy-memory delay before restoring a temporarily paused watercolor mode |
| `XIAOZHI_WATERCOLOR_THREADS` | `2` | Native renderer threads (1–4) |

## Architecture

| Layer | Device (RPi) | Simulator (macOS) |
|-------|-------------|-------------------|
| HAL | `HalEvdev` (evdev input) | `HalSdl` (SDL keyboard) |
| UI | `DisplayBridge` + `display_bridge.py` (PIL framebuffer) | `UiSdl` (SDL window) |
| WS | `WsClientBridge` + `ws_bridge.py` (Python asyncio) | same |
| Audio | `AudioPipelineSdl` (SDL audio) | same |

The Python sidecars communicate with C++ via pipes (stdin/stdout JSON protocol).

## License

MIT
