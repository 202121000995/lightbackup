#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-202121000995/lightbackup}"
VERSION="${VERSION:-latest}"
INSTALL_DIR="${INSTALL_DIR:-/opt/lightbackup}"
BIN_DIR="${BIN_DIR:-/usr/local/bin}"
SERVICE_NAME="${SERVICE_NAME:-lightbackup}"
ADDR="${ADDR:-:8080}"
RUN_USER="${RUN_USER:-lightbackup}"
GITHUB_PROXY="${GITHUB_PROXY:-${GH_PROXY:-}}"

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "缺少命令：$1"
    echo "Debian/Ubuntu 可先执行：sudo apt-get update && sudo apt-get install -y curl tar coreutils"
    echo "CentOS/RHEL 可先执行：sudo yum install -y curl tar coreutils"
    exit 1
  fi
}

install_rclone_if_missing() {
  if command -v rclone >/dev/null 2>&1 || [ -x "$INSTALL_DIR/rclone" ]; then
    return
  fi
  if [ "${INSTALL_RCLONE:-1}" = "0" ]; then
    echo "未检测到 rclone。请先安装 rclone，或把 rclone 放到 $INSTALL_DIR/rclone"
    exit 1
  fi
  echo "未检测到 rclone，尝试使用系统包管理器安装。"
  if command -v apt-get >/dev/null 2>&1; then
    apt-get update
    apt-get install -y rclone
  elif command -v dnf >/dev/null 2>&1; then
    dnf install -y rclone
  elif command -v yum >/dev/null 2>&1; then
    yum install -y rclone
  elif command -v apk >/dev/null 2>&1; then
    apk add --no-cache rclone
  elif command -v pacman >/dev/null 2>&1; then
    pacman -Sy --noconfirm rclone
  fi
  if ! command -v rclone >/dev/null 2>&1 && [ ! -x "$INSTALL_DIR/rclone" ]; then
    echo "rclone 自动安装失败。请手动安装后重试，例如："
    echo "curl https://rclone.org/install.sh | sudo bash"
    echo "或把 rclone 二进制放到：$INSTALL_DIR/rclone"
    exit 1
  fi
}

proxy_url() {
  local url="$1"
  if [ -n "$GITHUB_PROXY" ]; then
    printf '%s%s\n' "${GITHUB_PROXY%/}/" "$url"
  else
    printf '%s\n' "$url"
  fi
}

release_asset_url() {
  local asset="$1"
  if [ "$VERSION" = "latest" ]; then
    printf 'https://github.com/%s/releases/latest/download/%s\n' "$REPO" "$asset"
  else
    printf 'https://github.com/%s/releases/download/%s/%s\n' "$REPO" "$VERSION" "$asset"
  fi
}

need_cmd curl
need_cmd tar
need_cmd install

if [ "$(id -u)" -ne 0 ]; then
  echo "请使用 root 运行，例如：curl -fsSL ... | sudo bash"
  exit 1
fi

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

archive="$tmp_dir/LightBackup-Linux-amd64.tar.gz"
url="$(proxy_url "$(release_asset_url LightBackup-Linux-amd64.tar.gz)")"

echo "下载 LightBackup Linux 包：$url"
curl -fL "$url" -o "$archive"
tar -xzf "$archive" -C "$tmp_dir"

pkg_dir="$tmp_dir/LightBackup-Linux-amd64"
install -d "$INSTALL_DIR" "$INSTALL_DIR/configs" "$INSTALL_DIR/logs" "$BIN_DIR"
install -m 0755 "$pkg_dir/lightbackup-server" "$BIN_DIR/lightbackup-server"
install -m 0644 "$pkg_dir/README.md" "$INSTALL_DIR/README.md"
install_rclone_if_missing

if [ ! -f "$INSTALL_DIR/tasks.json" ]; then
  install -m 0644 "$pkg_dir/tasks.json" "$INSTALL_DIR/tasks.json"
fi

for config in "$pkg_dir"/configs/*.json; do
  target="$INSTALL_DIR/configs/$(basename "$config")"
  if [ ! -f "$target" ]; then
    install -m 0600 "$config" "$target"
  fi
done

if command -v useradd >/dev/null 2>&1 && ! id "$RUN_USER" >/dev/null 2>&1; then
  useradd --system --home-dir "$INSTALL_DIR" --shell /usr/sbin/nologin "$RUN_USER" || true
fi

if id "$RUN_USER" >/dev/null 2>&1; then
  chown -R "$RUN_USER:$RUN_USER" "$INSTALL_DIR"
fi

if command -v systemctl >/dev/null 2>&1; then
  cat >"/etc/systemd/system/${SERVICE_NAME}.service" <<SERVICE
[Unit]
Description=LightBackup Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=${INSTALL_DIR}
ExecStart=${BIN_DIR}/lightbackup-server -addr ${ADDR} -data-dir ${INSTALL_DIR}
Restart=on-failure
RestartSec=5
User=${RUN_USER}
Group=${RUN_USER}

[Install]
WantedBy=multi-user.target
SERVICE
  systemctl daemon-reload
  systemctl enable --now "${SERVICE_NAME}.service"
  echo "服务已启动：systemctl status ${SERVICE_NAME}"
else
  echo "未检测到 systemd，可手动启动："
  echo "${BIN_DIR}/lightbackup-server -addr ${ADDR} -data-dir ${INSTALL_DIR}"
fi

echo "安装完成。浏览器打开：http://服务器IP${ADDR}"
