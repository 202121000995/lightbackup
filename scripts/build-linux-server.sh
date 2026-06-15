#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$ROOT_DIR/release-linux"
GO_BIN="${GO:-go}"

mkdir -p "$OUTPUT_DIR"
GOOS=linux GOARCH=amd64 "$GO_BIN" build -o "$OUTPUT_DIR/lightbackup-server" "$ROOT_DIR/cmd/lightbackup-server"

echo "已生成 $OUTPUT_DIR/lightbackup-server"
