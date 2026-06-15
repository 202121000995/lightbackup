#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-202121000995/lightbackup}"
VERSION="${VERSION:-latest}"
INSTALL_DIR="${INSTALL_DIR:-/opt/lightbackup}"
BIN_DIR="${BIN_DIR:-/usr/local/bin}"
SERVICE_NAME="${SERVICE_NAME:-lightbackup}"
GITHUB_PROXY="${GITHUB_PROXY:-${GH_PROXY:-}}"

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "缺少命令：$1"
    echo "Debian/Ubuntu 可先执行：sudo apt-get update && sudo apt-get install -y curl tar coreutils"
    echo "CentOS/RHEL 可先执行：sudo yum install -y curl tar coreutils"
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
install -d "$INSTALL_DIR" "$BIN_DIR"
install -m 0755 "$pkg_dir/lightbackup-server" "$BIN_DIR/lightbackup-server"
install -m 0644 "$pkg_dir/README.md" "$INSTALL_DIR/README.md"

if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files "${SERVICE_NAME}.service" >/dev/null 2>&1; then
  systemctl restart "${SERVICE_NAME}.service"
  echo "服务已重启：systemctl status ${SERVICE_NAME}"
else
  echo "更新完成。请按你的启动方式重启 lightbackup-server。"
fi
