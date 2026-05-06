# cardputer-xiaozhi

[![Build deb](https://github.com/JdaieLin/cardputer-xiaozhi/actions/workflows/build-deb.yml/badge.svg)](https://github.com/JdaieLin/cardputer-xiaozhi/actions/workflows/build-deb.yml)

XiaoZhi voice assistant ported to M5Cardputer (Raspberry Pi Zero + framebuffer display).

## Features

- Two-way voice conversation via WebSocket (Opus codec)
- Framebuffer UI with CJK + emoji rendering
- OTA activation code flow for xiaozhi.me binding
- Push-to-talk (SPACE key) with server-side VAD
- Continuous conversation mode (auto re-listen after TTS finishes)
- macOS simulator for local development

## Project layout

```
cardputer-xiaozhi/
  .env.template                  # Environment variable template
  build.sh                       # Multi-target build script
  tools/
    deploy_rpi_199.sh            # Deploy to Raspberry Pi (192.168.100.199)
    package_applaunch.sh         # Create .deb package for APPLaunch
  .github/workflows/
    build-deb.yml                # CI: auto-build arm64 .deb
  projects/XiaoZhiApp/
    SConstruct                   # SCons build file
    app-builder.json             # Package metadata
    main/
      include/                   # Headers
      src/                       # C++ sources
      tools/
        display_bridge.py        # Python PIL framebuffer renderer
        ws_bridge.py             # Python WebSocket + Opus bridge
```

## Quick start

### Build for Raspberry Pi (device)

```bash
# On the Pi:
sudo apt-get install -y libsdl2-dev libsdl2-ttf-dev fonts-noto-cjk fonts-noto-color-emoji python3-pip
sudo pip3 install --break-system-packages websockets opuslib pillow
./build.sh --device
```

### Build the .deb package

```bash
./build.sh --device --package
# Output: projects/XiaoZhiApp/build/xiaozhi-applaunch_0.1-m5stack1_arm64.deb
```

### Cross-compile from macOS (aarch64)

```bash
brew install zig
./build.sh --aarch64 --package
```

### Local simulator (macOS)

```bash
brew install sdl2
pip3 install websockets opuslib
./build.sh --sim
./projects/XiaoZhiApp/build/xiaozhi_simulator
```

## CI / Automatic builds

Every push to `main` or `ci/**` branches triggers an automatic arm64 `.deb` build
and publishes a prerelease. Tagged pushes (`v*`) attach the `.deb` to the release.

Pre-built packages are available on the [Releases](https://github.com/JdaieLin/cardputer-xiaozhi/releases) page.

## Deployment to Raspberry Pi

```bash
./tools/deploy_rpi_199.sh
```

This script:
1. Installs system dependencies and Python packages on the Pi
2. Uploads source and builds `--device --package` on the Pi
3. Installs the `.deb` with `dpkg -i` and restarts `APPLaunch.service`

### Manual install

```bash
scp projects/XiaoZhiApp/build/xiaozhi-applaunch_0.1-m5stack1_arm64.deb pi@192.168.100.199:/tmp/
ssh pi@192.168.100.199 "sudo dpkg -i /tmp/xiaozhi-applaunch_0.1-m5stack1_arm64.deb"
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
