#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REMOTE="${REMOTE:-pi@192.168.100.199}"
REMOTE_DIR="${REMOTE_DIR:-/home/pi/cardputer-xiaozhi-device}"
REMOTE_TMP="${REMOTE_TMP:-/tmp/xiaozhi-applaunch_0.1-m5stack1_arm64.deb}"

ssh "$REMOTE" "echo pi | sudo -S sh -c 'if [ -f /etc/apt/apt.conf ]; then cp /etc/apt/apt.conf /etc/apt/apt.conf.xiaozhi.bak; cat /dev/null >/etc/apt/apt.conf; fi; need_install=0; for p in libsdl2-dev libsdl2-ttf-dev fonts-noto-cjk fonts-noto-color-emoji python3-pip; do dpkg -s \$p >/dev/null 2>&1 || need_install=1; done; if [ \$need_install -eq 1 ]; then apt-get update && apt-get install -y libsdl2-dev libsdl2-ttf-dev fonts-noto-cjk fonts-noto-color-emoji python3-pip; fi; if [ -f /etc/apt/apt.conf.xiaozhi.bak ]; then mv /etc/apt/apt.conf.xiaozhi.bak /etc/apt/apt.conf; fi'"
ssh "$REMOTE" "echo pi | sudo -S pip3 install --break-system-packages websockets opuslib"
ssh "$REMOTE" "mkdir -p /tmp/xiaozhi_logs"
ssh "$REMOTE" "rm -rf '$REMOTE_DIR' && mkdir -p '$REMOTE_DIR/projects'"
scp "$ROOT_DIR/build.sh" "$REMOTE:$REMOTE_DIR/"
scp -r "$ROOT_DIR/tools" "$REMOTE:$REMOTE_DIR/"
scp -r "$ROOT_DIR/projects/XiaoZhiApp" "$REMOTE:$REMOTE_DIR/projects/"
ssh "$REMOTE" "cd '$REMOTE_DIR' && chmod +x build.sh tools/package_applaunch.sh && ./build.sh --device --package"
scp "$REMOTE:$REMOTE_DIR/projects/XiaoZhiApp/build/xiaozhi-applaunch_0.1-m5stack1_arm64.deb" "$ROOT_DIR/projects/XiaoZhiApp/build/"
ssh "$REMOTE" "cp '$REMOTE_DIR/projects/XiaoZhiApp/build/xiaozhi-applaunch_0.1-m5stack1_arm64.deb' '$REMOTE_TMP' && echo pi | sudo -S dpkg -i '$REMOTE_TMP' && echo pi | sudo -S systemctl restart APPLaunch.service"
ssh "$REMOTE" "dpkg -s xiaozhi-applaunch | sed -n '1,8p' && systemctl is-active APPLaunch.service"
