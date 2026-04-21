#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$ROOT_DIR/projects/XiaoZhiApp"

TARGET="app"
if [[ "${1:-}" == "--sim" ]]; then
	TARGET="sim"
	shift
fi

cd "$PROJECT_DIR"

resolve_sdl_flags() {
	if command -v sdl2-config >/dev/null 2>&1; then
		echo "$(sdl2-config --cflags --libs)"
		return 0
	fi

	for base in /opt/homebrew/opt/sdl2 /usr/local/opt/sdl2; do
		if [[ -x "$base/bin/sdl2-config" ]]; then
			echo "$($base/bin/sdl2-config --cflags --libs)"
			return 0
		fi
	done

	if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
		echo "$(pkg-config --cflags --libs sdl2)"
		return 0
	fi

	return 1
}

mkdir -p build
if [[ "$TARGET" == "sim" ]]; then
	if ! SDL_FLAGS="$(resolve_sdl_flags)"; then
		echo "SDL2 flags not found. Install SDL2 first: brew install sdl2"
		exit 1
	fi

	g++ -std=c++17 -Wall -Wextra -O2 \
		-Imain/include \
		main/src/application.cpp \
		main/src/audio_pipeline.cpp \
                main/src/audio_pipeline_sdl.cpp \
		main/src/config.cpp \
		main/src/hal_sdl.cpp \
		main/src/ota_client.cpp \
		main/src/ui_sdl.cpp \
		main/src/ws_client.cpp \
		main/src/main_sim.cpp \
		${SDL_FLAGS} \
		-o build/xiaozhi_simulator
else
	if command -v scons >/dev/null 2>&1; then
		scons -Q "build/xiaozhi_app" "$@"
		exit 0
	fi

	echo "scons not found, using fallback g++ build for scaffold"

	g++ -std=c++17 -Wall -Wextra -O2 \
		-Imain/include \
		main/src/application.cpp \
		main/src/audio_pipeline.cpp \
		main/src/config.cpp \
		main/src/hal_stub.cpp \
		main/src/ota_client.cpp \
		main/src/ui.cpp \
		main/src/ws_client.cpp \
		main/src/main.cpp \
		-o build/xiaozhi_app
fi
