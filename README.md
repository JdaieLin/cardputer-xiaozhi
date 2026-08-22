# cardputer-xiaozhi

[![Build deb](https://github.com/JdaieLin/cardputer-xiaozhi/actions/workflows/build-deb.yml/badge.svg)](https://github.com/JdaieLin/cardputer-xiaozhi/actions/workflows/build-deb.yml)

XiaoZhi voice assistant ported to M5Cardputer (Raspberry Pi Zero + framebuffer display).

## Features

- Two-way voice conversation via WebSocket (Opus codec)
- Framebuffer UI with CJK + emoji rendering
- OTA activation code flow for xiaozhi.me binding
- Push-to-talk (SPACE key) with server-side VAD
- Continuous conversation mode (auto re-listen after TTS finishes)
- MCP local command and web search tools
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
# Output: build/cardputerzero-xiaozhi_0.2.4-m5stack1_arm64.deb
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
with the package version derived from the tag name, so `v0.2.4` builds
`cardputerzero-xiaozhi_0.2.4-..._arm64.deb`.

Pre-built packages are available on the [Releases](https://github.com/JdaieLin/cardputer-xiaozhi/releases) page.

## App Store submission

This repository now includes CardputerZero App Store metadata in [`app-builder.json`](./app-builder.json)
and store assets in [`store/`](./store/).

Recommended publish flow:

```bash
./build.sh --device --package
python3 /path/to/prepublish_check.py --deb build/cardputerzero-xiaozhi_0.2.4-m5stack1_arm64.deb --app-dir .
czdev login
czdev publish --deb build/cardputerzero-xiaozhi_0.2.4-m5stack1_arm64.deb
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
scp build/cardputerzero-xiaozhi_0.2.4-m5stack1_arm64.deb pi@192.168.100.199:/tmp/
ssh pi@192.168.100.199 "sudo dpkg -i /tmp/cardputerzero-xiaozhi_0.2.4-m5stack1_arm64.deb"
ssh pi@192.168.100.199 "sudo systemctl restart APPLaunch.service"
```

## Simulator controls

| Key | Action |
|-----|--------|
| `SPACE` / `ENTER` | Wake / push-to-talk |
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

Whisplay emoji SVG files are not stored in this repository or embedded in the `.deb`.
During installation, `tools/install.sh` downloads `emoji_svg.zip` from Whisplay's asset
server and extracts it beside `display_bridge.py`. Override the source with
`XIAOZHI_EMOJI_ASSET_URL`; the renderer falls back to the bundled emoji font if the
download is unavailable.

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
