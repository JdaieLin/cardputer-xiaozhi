#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BIN="$BUILD_DIR/xiaozhi_app"
PKG_ROOT="$BUILD_DIR/applaunch-package"
OUT_DEB="$BUILD_DIR/xiaozhi-applaunch_0.1-m5stack1_arm64.deb"
ICON_SRC="${XIAOZHI_ICON_SRC:-$ROOT_DIR/tools/assets/xiaozhi.png}"

if [[ ! -x "$BIN" ]]; then
	echo "missing binary: $BIN"
	echo "run ./build.sh --aarch64 first"
	exit 1
fi

rm -rf "$PKG_ROOT" "$OUT_DEB"
mkdir -p \
	"$PKG_ROOT/DEBIAN" \
	"$PKG_ROOT/usr/share/APPLaunch/applications" \
	"$PKG_ROOT/usr/share/APPLaunch/bin" \
	"$PKG_ROOT/usr/share/APPLaunch/share/images" \
	"$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi"

install -m 0755 "$BIN" "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_app"
install -m 0644 "$ROOT_DIR/main/tools/ws_bridge.py" "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/ws_bridge.py"
install -m 0644 "$ROOT_DIR/main/tools/display_bridge.py" "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/display_bridge.py"
install -m 0755 "$ROOT_DIR/tools/install.sh" "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/install.sh"
if [[ ! -f "$ICON_SRC" ]]; then
	echo "missing icon: $ICON_SRC"
	exit 1
fi
install -m 0644 "$ICON_SRC" "$PKG_ROOT/usr/share/APPLaunch/share/images/xiaozhi.png"

# Bundle fonts into the .deb if they were fetched by CI
if [ -d "$ROOT_DIR/tools/fonts" ] && [ -n "$(ls -A "$ROOT_DIR/tools/fonts" 2>/dev/null)" ]; then
    mkdir -p "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/fonts"
    cp "$ROOT_DIR/tools/fonts/"* "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/fonts/"
    echo "[package] bundled fonts: $(ls "$ROOT_DIR/tools/fonts/")"
fi


cat > "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_launcher" <<'EOF'
#!/usr/bin/env sh
export XIAOZHI_WS_BRIDGE=/usr/share/APPLaunch/share/xiaozhi/ws_bridge.py
export SDL_AUDIODRIVER=alsa
detect_fbdev() {
	if [ -n "${APPLAUNCH_LINUX_FBDEV_DEVICE:-}" ]; then
		echo "$APPLAUNCH_LINUX_FBDEV_DEVICE"
		return 0
	fi
	if [ -r /proc/fb ]; then
		fb_idx="$(awk '/fb_st7789v/ {print $1; exit}' /proc/fb 2>/dev/null || true)"
		if [ -n "$fb_idx" ]; then
			echo "/dev/fb$fb_idx"
			return 0
		fi
	fi
	echo "/dev/fb0"
}
export XIAOZHI_FBDEV="$(detect_fbdev)"
export XIAOZHI_KEYBOARD_DEVICE="${APPLAUNCH_LINUX_KEYBOARD_DEVICE:-/dev/input/by-path/platform-3f804000.i2c-event}"
LOCK_DIR="/tmp/xiaozhi_singleton.lock"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
	exit 0
fi
cleanup() {
	rmdir "$LOCK_DIR" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
LOG_DIR="/tmp/xiaozhi_logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/xiaozhi_$(date +%Y%m%d_%H%M%S).log"
if ! touch "$LOG_FILE" 2>/dev/null; then
	LOG_DIR="/tmp"
	LOG_FILE="$LOG_DIR/xiaozhi_$(date +%Y%m%d_%H%M%S).log"
fi
{
	echo "[launcher] fbdev=$XIAOZHI_FBDEV keyboard=$XIAOZHI_KEYBOARD_DEVICE"
	/usr/share/APPLaunch/bin/xiaozhi_app &
	APP_PID=$!
	term_child() {
		kill -TERM "$APP_PID" 2>/dev/null || true
		wait "$APP_PID" 2>/dev/null || true
	}
	trap term_child INT TERM
	wait "$APP_PID"
} >>"$LOG_FILE" 2>&1
EOF
chmod 0755 "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_launcher"

cat > "$PKG_ROOT/usr/share/APPLaunch/applications/xiaozhi.desktop" <<'EOF'
[Desktop Entry]
Name=XiaoZhi
Exec=/usr/share/APPLaunch/bin/xiaozhi_launcher
Icon=share/images/xiaozhi.png
Terminal=false
Sysplause=false
Type=Application
EOF

cat > "$PKG_ROOT/DEBIAN/control" <<'EOF'
Package: xiaozhi-applaunch
Version: 0.1-m5stack1
Architecture: arm64
Maintainer: M5Stack <m5stack@m5stack.com>
Section: APPLaunch
Priority: optional
Homepage: https://github.com/dianjixz/M5CardputerZero-UserDemo
Description: XiaoZhi voice assistant APPLaunch entry for M5Cardputer Zero
EOF

(
	cd "$PKG_ROOT/DEBIAN"
	COPYFILE_DISABLE=1 tar -czf "$BUILD_DIR/control.tar.gz" .
)
(
	cd "$PKG_ROOT"
	COPYFILE_DISABLE=1 tar --exclude ./DEBIAN -czf "$BUILD_DIR/data.tar.gz" .
)
printf '2.0\n' > "$BUILD_DIR/debian-binary"
(
	cd "$BUILD_DIR"
	ar -r "$OUT_DEB" debian-binary control.tar.gz data.tar.gz >/dev/null
	rm -f debian-binary control.tar.gz data.tar.gz
)

echo "$OUT_DEB"
