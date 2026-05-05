#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_DIR="$ROOT_DIR/projects/XiaoZhiApp"
BUILD_DIR="$PROJECT_DIR/build"
BIN="$BUILD_DIR/xiaozhi_app"
PKG_ROOT="$BUILD_DIR/applaunch-package"
OUT_DEB="$BUILD_DIR/xiaozhi-applaunch_0.1-m5stack1_arm64.deb"

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
	"$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi"

install -m 0755 "$BIN" "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_app"
install -m 0644 "$PROJECT_DIR/main/tools/ws_bridge.py" "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/ws_bridge.py"

cat > "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_launcher" <<'EOF'
#!/usr/bin/env sh
export XIAOZHI_WS_BRIDGE=/usr/share/APPLaunch/share/xiaozhi/ws_bridge.py
export SDL_AUDIODRIVER=alsa
export SDL_VIDEODRIVER=dummy
export XIAOZHI_FBDEV=/dev/fb0
LOG_DIR="/tmp/xiaozhi_logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/xiaozhi_$(date +%Y%m%d_%H%M%S).log"
exec /usr/share/APPLaunch/bin/xiaozhi_app >>"$LOG_FILE" 2>&1
EOF
chmod 0755 "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_launcher"

cat > "$PKG_ROOT/usr/share/APPLaunch/applications/xiaozhi.desktop" <<'EOF'
[Desktop Entry]
Name=XiaoZhi
Exec=/usr/share/APPLaunch/bin/xiaozhi_launcher
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
