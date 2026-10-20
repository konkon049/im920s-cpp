#!/usr/bin/env python3
"""IM920s 2台間の通信テスト (PCにUSBシリアル経由で2台接続した状態用)

使い方:
    python3 tools/im920s_test.py                         # /dev/ttyUSB0 <-> /dev/ttyUSB1, 19200bps
    python3 tools/im920s_test.py /dev/ttyUSB0 /dev/ttyUSB1 -b 19200 -n 10

手順:
  1. 各モジュールに RDID / RDNN / RDCH / RDRS を送り、応答を確認
  2. A から TXDA で送信 → B で受信できるか確認
  3. B から TXDA で送信 → A で受信できるか確認
"""
import argparse
import sys
import time

import serial


def cmd(port, text, wait=0.3):
    """コマンドを送り、返ってきた行を返す"""
    port.reset_input_buffer()
    port.write((text + "\r\n").encode())
    time.sleep(wait)
    return port.read(port.in_waiting or 1).decode(errors="replace").strip()


def read_lines(port, timeout):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        buf += port.read(port.in_waiting or 1)
        if buf.endswith(b"\r\n"):
            break
    return [l for l in buf.decode(errors="replace").splitlines() if l.strip()]


def decode_rx(line):
    """受信行 '00,0001,C8:41,42,43' をデータ部の文字列に変換 (失敗時はNone)"""
    if ":" not in line:
        return None
    head, data = line.split(":", 1)
    try:
        payload = bytes(int(h, 16) for h in data.split(","))
        rssi = int(head.split(",")[-1], 16)
    except ValueError:
        return None
    return payload.decode(errors="replace"), rssi


def info(name, port):
    print(f"--- {name} ({port.port}) ---")
    ok = True
    for c, label in (("RDID", "固有ID"), ("RDNN", "ノード番号"), ("RDCH", "チャンネル"), ("RDRS", "RSSI")):
        r = cmd(port, c)
        print(f"  {c:5s} {label:6s}: {r or '(応答なし)'}")
        if c == "RDID" and not r:
            ok = False
    return ok


def one_way(tx_name, tx, rx_name, rx, count):
    print(f"\n=== {tx_name} -> {rx_name} 送信テスト ({count}回) ===")
    success = 0
    for i in range(count):
        msg = f"TEST{i:03d}"
        rx.reset_input_buffer()
        # 初期設定(DCIO)では TXDA のデータは16進表記で渡す
        res = cmd(tx, f"TXDA {msg.encode().hex().upper()}", wait=0.2)
        lines = read_lines(rx, 1.0)
        got = [decode_rx(l) for l in lines]
        hit = next((g for g in got if g and g[0] == msg), None)
        if hit:
            success += 1
            print(f"  [{i}] 送信:{msg} 応答:{res!r} -> 受信OK RSSI=0x{hit[1]:02X}")
        else:
            print(f"  [{i}] 送信:{msg} 応答:{res!r} -> 受信NG 受信内容:{lines}")
    print(f"  成功 {success}/{count}")
    return success


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port_a", nargs="?", default="/dev/ttyUSB0")
    ap.add_argument("port_b", nargs="?", default="/dev/ttyUSB1")
    ap.add_argument("-b", "--baud", type=int, default=19200)
    ap.add_argument("-n", "--count", type=int, default=5)
    args = ap.parse_args()

    a = serial.Serial(args.port_a, args.baud, timeout=0.1)
    b = serial.Serial(args.port_b, args.baud, timeout=0.1)
    time.sleep(0.5)

    ok_a = info("A", a)
    ok_b = info("B", b)
    if not (ok_a and ok_b):
        print("\n応答のないモジュールがあります。アダプタ基板のコネクタの差し込みとボーレートを確認してください。")
        sys.exit(1)

    one_way("A", a, "B", b, args.count)
    one_way("B", b, "A", a, args.count)
    a.close()
    b.close()


if __name__ == "__main__":
    main()
