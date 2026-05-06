#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REMOTE="${REMOTE:-pi@192.168.100.199}"
REMOTE_DIR="${REMOTE_DIR:-/home/pi/cardputer-xiaozhi-device}"
REMOTE_TMP="${REMOTE_TMP:-/tmp/xiaozhi-applaunch_0.1-m5stack1_arm64.deb}"

# ── Upload & install dependencies ────────────────────────────────
ssh "$REMOTE" "mkdir -p /tmp/xiaozhi_logs"
ssh "$REMOTE" "rm -rf '$REMOTE_DIR' && mkdir -p '$REMOTE_DIR/projects'"
scp "$ROOT_DIR/build.sh" "$REMOTE:$REMOTE_DIR/"
scp -r "$ROOT_DIR/tools" "$REMOTE:$REMOTE_DIR/"
scp -r "$ROOT_DIR/projects/XiaoZhiApp" "$REMOTE:$REMOTE_DIR/projects/"

# Run install.sh on remote to set up system deps, Python pkgs, fonts
ssh "$REMOTE" "echo pi | sudo -S '$REMOTE_DIR/tools/install.sh'"

# ── Build .deb on device ─────────────────────────────────────────
ssh "$REMOTE" "cd '$REMOTE_DIR' && chmod +x build.sh tools/package_applaunch.sh && ./build.sh --device --package"

# ── Fetch .deb back and install ──────────────────────────────────
scp "$REMOTE:$REMOTE_DIR/projects/XiaoZhiApp/build/xiaozhi-applaunch_0.1-m5stack1_arm64.deb" "$ROOT_DIR/projects/XiaoZhiApp/build/"
ssh "$REMOTE" "cp '$REMOTE_DIR/projects/XiaoZhiApp/build/xiaozhi-applaunch_0.1-m5stack1_arm64.deb' '$REMOTE_TMP' && echo pi | sudo -S dpkg -i '$REMOTE_TMP' && echo pi | sudo -S systemctl restart APPLaunch.service"
ssh "$REMOTE" "dpkg -s xiaozhi-applaunch | sed -n '1,8p' && systemctl is-active APPLaunch.service"
