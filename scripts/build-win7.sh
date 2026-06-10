#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ZIG="tools/zig-macos-aarch64-0.11.0/zig"
QT="tools/Qt/5.12.12/mingw73_32"
RCLONE="tools/rclone-v1.62.2-windows-386/rclone.exe"
STAGE="release-qt512"
PACKAGE="release-ready/LightBackup"

mkdir -p "$STAGE" "$PACKAGE/configs" "$PACKAGE/fonts" "$PACKAGE/platforms" "$PACKAGE/logs"

env ZIG_LOCAL_CACHE_DIR=.zig-cache ZIG_GLOBAL_CACHE_DIR=zig-global-cache \
  "$ZIG" c++ \
  -target x86-windows-gnu \
  -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE \
  -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB \
  -I"$QT/include" \
  -I"$QT/include/QtWidgets" \
  -I"$QT/include/QtGui" \
  -I"$QT/include/QtCore" \
  qt/main.cpp \
  -L"$QT/lib" \
  -lQt5Widgets -lQt5Gui -lQt5Core -lqtmain \
  -Wl,--subsystem,windows \
  -lole32 -luuid -lwinmm -lws2_32 -ladvapi32 -lshell32 -luser32 -lgdi32 -limm32 -lversion \
  -o "$STAGE/LightBackupQt.exe"

python3 tools/patch_win7_import.py "$STAGE/LightBackupQt.exe"
python3 tools/embed_icon.py "$STAGE/LightBackupQt.exe" assets/lightbackup.ico

cp "$STAGE/LightBackupQt.exe" "$PACKAGE/LightBackup.exe"
cp "$RCLONE" "$PACKAGE/rclone.exe"
cp "$QT/bin/Qt5Core.dll" "$QT/bin/Qt5Gui.dll" "$QT/bin/Qt5Widgets.dll" "$PACKAGE/"
cp "$QT/bin/libgcc_s_dw2-1.dll" "$QT/bin/libstdc++-6.dll" "$QT/bin/libwinpthread-1.dll" "$PACKAGE/"
cp assets/lightbackup.ico "$PACKAGE/LightBackup.ico"
cp assets/icon/icon_256.png "$PACKAGE/LightBackup.png"
cp qt/style.qss "$PACKAGE/style.qss"
cp packaging/README.txt "$PACKAGE/README.txt"
cp packaging/tasks.json "$PACKAGE/tasks.json"
cp "$QT/plugins/platforms/qwindows.dll" "$PACKAGE/platforms/"
cp assets/fonts/NotoSansCJKsc-Regular.otf "$PACKAGE/fonts/"
cp configs/*.json "$PACKAGE/configs/"

find "$PACKAGE" -type f -print0 | xargs -0 openssl dgst -sha256 > release-ready/SHA256SUMS.txt
rm -f release-ready/LightBackup-Win7-x86.zip
(
  cd release-ready
  COPYFILE_DISABLE=1 zip -r -X -q LightBackup-Win7-x86.zip LightBackup
)

unzip -t release-ready/LightBackup-Win7-x86.zip >/dev/null
echo "Built release-ready/LightBackup-Win7-x86.zip"
