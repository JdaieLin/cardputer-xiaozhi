# cardputer-xiaozhi

Porting `whisplay-xiaozhi` to M5Cardputer.

## Current status

This repository now contains **Phase 1 MVP scaffold**:

- Project layout for a C++ implementation
- Build entry using SCons
- Runtime state machine (binding/idle/listening/thinking/speaking/error)
- OTA-based real activation code flow for xiaozhi.me binding
- macOS simulator audio capture/playback via SDL
- macOS simulator text rendering with Chinese + emoji support
- Real WebSocket dialogue bridge (Python sidecar with Opus + XiaoZhi protocol)

This is intentionally minimal so we can iterate quickly and replace stubs with real SDK integrations.

## Project layout

```
cardputer-xiaozhi/
	.env.template
	build.sh
	projects/
		XiaoZhiApp/
			SConstruct
			config_defaults.mk
			main/
				include/
				src/
```

## Build

From repository root:

```bash
chmod +x build.sh
./build.sh
```

`build.sh` tries SCons first. If `scons` is not installed, it falls back to a direct `g++` build for the current scaffold.

## Local simulator (macOS)

Install dependencies:

```bash
brew install sdl2 scons
pip3 install websockets opuslib
```

Build simulator:

```bash
./build.sh --sim
```

Run simulator:

```bash
./projects/XiaoZhiApp/build/xiaozhi_simulator
```

Controls:

- AppLaunch home: `Left` / `Right` select app, `Enter` or `Space` open selected app
- XiaoZhi app: `SPACE` or `ENTER` wake-to-talk (press start, release does not stop)
- `Esc`: return to AppLaunch home
- `CARDPUTER_WAKE_KEYS=Space,Return`: optional override for simulator primary action keys (comma-separated SDL key names)
- Listening stop is decided by server VAD (`listen_stop`)
- close window or `Ctrl+C`: exit

Notes:

- If logs show `ready -> stopped`, the app received a quit signal (`SDL_QUIT` from window close, or `SIGINT/SIGTERM`).
- For a valid voice round, expected states are usually:
	`ready -> listening server vad -> tts start -> tts stop`.

Run:

```bash
./projects/XiaoZhiApp/build/xiaozhi_app
```

## MVP scope

- WebSocket only (no MQTT)
- Push-to-talk only (no wakeword in phase 1)
- End-to-end audio conversation pipeline shape

## Next implementation tasks

1. Replace simulator Python WebSocket bridge with native C++ WebSocket implementation
2. Port SDL audio path to device TinyALSA path for hardware deployment
3. Replace simulator UI with LVGL device UI screens
4. Replace keyboard simulation with real Cardputer input drivers
5. Add persistent credential cache for direct reconnect optimization