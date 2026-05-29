#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
APP_BUILDER_JSON="$ROOT_DIR/app-builder.json"
ICON_SRC="${XIAOZHI_ICON_SRC:-$ROOT_DIR/tools/assets/xiaozhi.png}"
MAINTAINER_NAME="${XIAOZHI_MAINTAINER_NAME:-JdaieLin}"
MAINTAINER_EMAIL="${XIAOZHI_MAINTAINER_EMAIL:-hongruilin@alum.calarts.edu}"
HOMEPAGE_URL="${XIAOZHI_HOMEPAGE_URL:-https://github.com/JdaieLin/cardputer-xiaozhi}"
GTAR_BIN="${GTAR:-}"
if [[ -z "$GTAR_BIN" ]]; then
	if command -v gtar >/dev/null 2>&1; then
		GTAR_BIN="$(command -v gtar)"
	elif command -v tar >/dev/null 2>&1 && tar --version 2>/dev/null | grep -q 'GNU tar'; then
		GTAR_BIN="$(command -v tar)"
	elif [[ -x /opt/homebrew/bin/gtar ]]; then
		GTAR_BIN="/opt/homebrew/bin/gtar"
	else
		echo "GNU tar is required for APPLaunch store-compatible packages. Install it with: brew install gnu-tar" >&2
		exit 1
	fi
fi

make_ustar_gz() {
	local src_dir="$1"
	local out_tar_gz="$2"
	shift 2
	(
		cd "$src_dir"
		COPYFILE_DISABLE=1 "$GTAR_BIN" \
			--format=ustar \
			--owner=0 --group=0 --numeric-owner \
			--sort=name \
			--mtime='UTC 2026-01-01' \
			-czf "$out_tar_gz" "$@"
	)
}

json_value() {
	local expr="$1"
	python3 - "$APP_BUILDER_JSON" "$expr" <<'PY'
import json
import sys
from pathlib import Path

config_path = Path(sys.argv[1])
expr = sys.argv[2]
data = json.loads(config_path.read_text())
value = data
for key in expr.split("."):
    value = value[key]
print(value)
PY
}

PACKAGE_NAME="${XIAOZHI_PACKAGE_NAME:-$(json_value package_name)}"
VERSION="${XIAOZHI_VERSION:-$(json_value version)}"
REVISION="${XIAOZHI_REVISION:-$(json_value revision)}"
APP_NAME="$(json_value app_name)"
BIN_NAME="$(json_value bin_name)"
DESCRIPTION="$(json_value description)"

if [[ -n "${XIAOZHI_VERSION:-}" ]]; then
	echo "[package] using version override from environment: $VERSION"
fi

BIN="$BUILD_DIR/$BIN_NAME"
PKG_ROOT="$BUILD_DIR/${PACKAGE_NAME}-package"
OUT_DEB="$BUILD_DIR/${PACKAGE_NAME}_${VERSION}-${REVISION}_arm64.deb"

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

install -m 0755 "$BIN" "$PKG_ROOT/usr/share/APPLaunch/bin/$BIN_NAME"
install -m 0644 "$ROOT_DIR/main/tools/ws_bridge.py" "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/ws_bridge.py"
install -m 0644 "$ROOT_DIR/main/tools/display_bridge.py" "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/display_bridge.py"
install -m 0755 "$ROOT_DIR/tools/install.sh" "$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/install.sh"
if [[ ! -f "$ICON_SRC" ]]; then
	echo "missing icon: $ICON_SRC"
	exit 1
fi
install -m 0644 "$ICON_SRC" "$PKG_ROOT/usr/share/APPLaunch/share/images/xiaozhi.png"

# Bundle required fonts into the .deb.
FONT_DST="$PKG_ROOT/usr/share/APPLaunch/share/xiaozhi/fonts"
mkdir -p "$FONT_DST"

copy_first_font() {
	dst_name="$1"
	shift
	for src in "$@"; do
		if [ -f "$src" ]; then
			install -m 0644 "$src" "$FONT_DST/$dst_name"
			echo "[package] bundled font: $dst_name <= $src"
			return 0
		fi
	done
	echo "[package] missing required font: $dst_name" >&2
	return 1
}

copy_first_font "NotoColorEmoji.ttf" \
	"$ROOT_DIR/tools/fonts/NotoColorEmoji.ttf" \
	"/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf" \
	"/usr/share/fonts/opentype/noto/NotoColorEmoji.ttf"

copy_first_font "NotoSansSC-Regular.ttf" \
	"$ROOT_DIR/tools/fonts/NotoSansSC-Regular.ttf" \
	"$ROOT_DIR/tools/fonts/NotoSansSC-Bold.ttf" \
	"$ROOT_DIR/tools/fonts/NotoSansCJKsc-Regular.otf" \
	"/usr/share/fonts/truetype/noto/NotoSansSC-Regular.ttf" \
	"/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf" \
	"/usr/share/fonts/truetype/noto/NotoSansSC-Bold.ttf" \
	"/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf" \
	"/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf" \
	"/usr/share/fonts/opentype/noto/NotoSansSC-Bold.otf" \
	"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"


cat > "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_launcher" <<'EOF'
#!/usr/bin/env sh
export XIAOZHI_WS_BRIDGE=/usr/share/APPLaunch/share/xiaozhi/ws_bridge.py

# APPLaunch runs as root, but audio is provided by the logged-in
# user's PipeWire PulseAudio compatibility socket.
export XDG_RUNTIME_DIR=/run/user/1000
export SDL_AUDIODRIVER=pulseaudio
export PULSE_SERVER="unix:/run/user/1000/pulse/native"
export XIAOZHI_CAPTURE_BACKEND=alsa
unset AUDIODEV
unset XIAOZHI_AUDIO_CAPTURE_DEVICE
unset XIAOZHI_AUDIO_PLAYBACK_DEVICE

# Initialize ES8388 codec mixer (PGA gain, output volume, routing)
amixer -c 1 sset 'ADC MUX' AMIC 2>/dev/null || true
amixer -c 1 sset 'PGAL Select' 'DifferentialL' 2>/dev/null || true
amixer -c 1 sset 'PGAR Select' 'DifferentialR' 2>/dev/null || true
amixer -c 1 sset 'ADCL PGA' 14 2>/dev/null || true
amixer -c 1 sset 'ADCR PGA' 14 2>/dev/null || true
amixer -c 1 sset 'DACL' 200 2>/dev/null || true
amixer -c 1 sset 'DACR' 200 2>/dev/null || true
amixer -c 1 sset 'ADC2DAC Mixer' 127 2>/dev/null || true
amixer -c 1 sset 'OUTL MUX' Normal 2>/dev/null || true
amixer -c 1 sset 'OUTR MUX' Normal 2>/dev/null || true
wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 2>/dev/null || true
wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.65 2>/dev/null || true
wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 0 2>/dev/null || true
wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 1.0 2>/dev/null || true

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
detect_user_home() {
	local user_name=""
	local user_home=""
	if command -v getent >/dev/null 2>&1; then
		user_name="$(getent passwd 1000 | cut -d: -f1)"
		user_home="$(getent passwd 1000 | cut -d: -f6)"
	fi
	if [ -z "$user_name" ] && command -v id >/dev/null 2>&1; then
		user_name="$(id -nu 1000 2>/dev/null || true)"
	fi
	if [ -z "$user_home" ] && [ -n "$user_name" ]; then
		user_home="$(eval echo "~$user_name" 2>/dev/null || true)"
	fi
	if [ -n "$user_home" ] && [ -d "$user_home" ]; then
		echo "$user_home"
		return 0
	fi
	return 1
}
XIAOZHI_USER_HOME="$(detect_user_home || true)"
if [ -z "$XIAOZHI_USER_HOME" ]; then
	echo "[launcher] unable to detect uid 1000 home directory" >&2
	exit 1
fi
XIAOZHI_IDENTITY_DIR="$XIAOZHI_USER_HOME/.config/xiaozhi"
mkdir -p "$XIAOZHI_IDENTITY_DIR"
if [ ! -f "$XIAOZHI_IDENTITY_DIR/sim_identity.env" ]; then
	if [ -f /tmp/cardputer-xiaozhi-device/sim_identity.env ]; then
		cp /tmp/cardputer-xiaozhi-device/sim_identity.env "$XIAOZHI_IDENTITY_DIR/sim_identity.env"
	elif [ -f "$XIAOZHI_USER_HOME/cardputer-xiaozhi-device/sim_identity.env" ]; then
		cp "$XIAOZHI_USER_HOME/cardputer-xiaozhi-device/sim_identity.env" "$XIAOZHI_IDENTITY_DIR/sim_identity.env"
	fi
fi
cd "$XIAOZHI_IDENTITY_DIR" || exit 1
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
	/usr/share/APPLaunch/bin/__BIN_NAME__ &
	APP_PID=$!
	term_child() {
		kill -TERM "$APP_PID" 2>/dev/null || true
		wait "$APP_PID" 2>/dev/null || true
	}
	trap term_child INT TERM
	wait "$APP_PID"
} >>"$LOG_FILE" 2>&1
EOF
python3 - "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_launcher" "$BIN_NAME" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
bin_name = sys.argv[2]
path.write_text(path.read_text().replace("__BIN_NAME__", bin_name))
PY
chmod 0755 "$PKG_ROOT/usr/share/APPLaunch/bin/xiaozhi_launcher"

cat > "$PKG_ROOT/usr/share/APPLaunch/applications/xiaozhi.desktop" <<EOF
[Desktop Entry]
Name=$APP_NAME
Exec=/usr/share/APPLaunch/bin/xiaozhi_launcher
Icon=share/images/xiaozhi.png
Terminal=false
Sysplause=false
Type=Application
EOF

cat > "$PKG_ROOT/DEBIAN/control" <<EOF
Package: $PACKAGE_NAME
Version: $VERSION-$REVISION
Architecture: arm64
Maintainer: $MAINTAINER_NAME <$MAINTAINER_EMAIL>
Section: APPLaunch
Priority: optional
Homepage: $HOMEPAGE_URL
Description: $DESCRIPTION for M5Cardputer Zero
EOF

make_ustar_gz "$PKG_ROOT/DEBIAN" "$BUILD_DIR/control.tar.gz" .
make_ustar_gz "$PKG_ROOT" "$BUILD_DIR/data.tar.gz" --exclude ./DEBIAN .
printf '2.0\n' > "$BUILD_DIR/debian-binary"
(
	cd "$BUILD_DIR"
	ar -r "$OUT_DEB" debian-binary control.tar.gz data.tar.gz >/dev/null
	rm -f debian-binary control.tar.gz data.tar.gz
)

echo "$OUT_DEB"
