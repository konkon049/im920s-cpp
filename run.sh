#!/bin/bash
# IM920s の送り合いを始める (Ctrl+C で停止して統計を表示)
#
#   ./run.sh                 つながっている IM920s を自動で探して 1台モードで実行 (別々のPC同士)
#   ./run.sh /dev/ttyUSB1    ポートを指定
#   ./run.sh pair            1台のPCに2台つないでいるとき (2台を自動で探して高速モード)
#   ./run.sh turbo           同上 + 115200bps に一時的に切り替えて最速 (終了時に 19200 に戻す)
#
# 他のオプションはそのまま pingpong に渡る (例: ./run.sh /dev/ttyUSB0 -s 16)
cd "$(dirname "$0")"

# 実行ファイル: このPCでビルドしたもの > 同梱のビルド済み (静的リンクなので Ubuntu 22.04 / 24.04 で動く)
BIN=""
for b in build/pingpong bin/pingpong; do
  [ -x "$b" ] || continue
  "$b" >/dev/null 2>&1
  [ $? -eq 2 ] && { BIN=$b; break; }  # 引数なしだと使い方を出して終了コード 2 (= 正常に動く)
done
if [ -z "$BIN" ]; then
  echo "実行ファイルが動かないのでビルドします (g++ と make が必要: sudo apt install -y g++ make)"
  make build/pingpong || exit 1
  BIN=build/pingpong
fi

ports=()
for p in /dev/ttyUSB* /dev/ttyACM*; do [ -e "$p" ] && ports+=("$p"); done

if [ ${#ports[@]} -eq 0 ]; then
  echo "IM920s が見つかりません (/dev/ttyUSB* がない)"
  echo " - USB に挿してから数秒待ってもう一度実行してください"
  if dpkg -s brltty >/dev/null 2>&1; then
    echo " - brltty が入っています。挿した直後にポートを奪うことがあります:  sudo apt remove -y brltty"
  fi
  exit 1
fi

check_perm() {
  if [ ! -r "$1" ] || [ ! -w "$1" ]; then
    echo "$1 を使う権限がありません。次を実行して、ログインし直してください:"
    echo "   sudo usermod -aG dialout \$USER"
    exit 1
  fi
}

mode=single
case "$1" in
  pair) mode=pair; shift ;;
  turbo) mode=turbo; shift ;;
  /dev/*) ports=("$1"); shift ;;
esac

if [ "$mode" = single ]; then
  if [ ${#ports[@]} -gt 1 ]; then
    echo "ポートが複数あります: ${ports[*]}  → ${ports[0]} を使います (別のを使うなら ./run.sh ${ports[1]})"
    echo "1台のPCに2台つないでいるなら ./run.sh pair"
  fi
  check_perm "${ports[0]}"
  echo "1台モード: ${ports[0]}  (相手のPCでも ./run.sh を実行してください)"
  exec "$BIN" "$@" "${ports[0]}"
fi

if [ ${#ports[@]} -lt 2 ]; then
  echo "2台モードには IM920s が2台必要です (見つかったのは: ${ports[*]})"
  exit 1
fi
check_perm "${ports[0]}"
check_perm "${ports[1]}"
if [ "$mode" = turbo ]; then
  exec "$BIN" -t "$@" "${ports[0]}" "${ports[1]}"
else
  exec "$BIN" -f "$@" "${ports[0]}" "${ports[1]}"
fi
