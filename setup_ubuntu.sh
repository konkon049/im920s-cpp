#!/bin/bash
# 別の Ubuntu PC (22.04 / 24.04) で pingpong を使えるようにする
# 使い方: このフォルダで  bash setup_ubuntu.sh
set -e
cd "$(dirname "$0")"

echo "== 1. ビルドに必要なものを入れる (g++, make)"
sudo apt update
sudo apt install -y g++ make

echo "== 2. brltty を外す (CP2102N を点字機器と間違えて /dev/ttyUSB0 を奪うため)"
if dpkg -s brltty >/dev/null 2>&1; then
  sudo apt remove -y brltty
else
  echo "   brltty は入っていません"
fi

echo "== 3. シリアルポートの使用権限 (dialout グループ)"
NEED_RELOGIN=0
if id -nG "$USER" | grep -qw dialout; then
  echo "   設定済み"
else
  sudo usermod -aG dialout "$USER"
  NEED_RELOGIN=1
fi

echo "== 4. このPCでビルド (失敗しても同梱の bin/pingpong で動きます)"
make build/pingpong || echo "   ビルドに失敗しました。bin/pingpong を使います"

echo
echo "== 完了"
ls /dev/ttyUSB* 2>/dev/null && echo "   ↑ モジュールが見えています" || echo "   (まだ /dev/ttyUSB* が見えません。IM920s を USB に挿してください)"
if [ "$NEED_RELOGIN" = 1 ]; then
  echo
  echo "!! dialout グループに追加しました。一度ログアウトしてログインし直してから実行してください"
fi
echo
echo "実行 (両方のPCで同時に):"
echo "   cd $(pwd) && ./run.sh"
