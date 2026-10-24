#!/usr/bin/env python3
"""IM920s 同士でひたすら送信し続ける (Windows / Linux 両対応, Ctrl+C で停止して統計を表示)

準備:
    pip install pyserial

使い方:
    別々のPCに1台ずつ (両方のPCで実行):
        python tools/pingpong.py COM3                 # Windows
        python3 tools/pingpong.py /dev/ttyUSB0        # Linux
    1台のPCに2台:
        python tools/pingpong.py COM3 COM4

    オプション: -p 秒  送信周期 (既定 0.25。両方が同時に送ると届かないので毎回 0〜0.1 秒ずらす)
               -b bps  通信速度 (既定 19200)
               --list  使えるシリアルポートを表示

データは「ノード番号:通し番号:埋め文字」の32バイト。受信側は送信元ごとに番号の飛びを数える。
"""
import argparse
import random
import sys
import time

import serial
import serial.tools.list_ports

SIZE = 32  # 1パケットの上限


def make_payload(node, seq):
    s = f"{node}:{seq}:"
    while len(s) < SIZE:
        s += chr(ord("a") + (seq + len(s)) % 26)
    return s


def parse_payload(text):
    """'0001:123:abc...' -> ('0001', 123)。形式が違えば None"""
    parts = text.split(":", 2)
    if len(parts) != 3 or not parts[1].isdigit():
        return None
    node, seq = parts[0], int(parts[1])
    return (node, seq) if text == make_payload(node, seq) else None


def human(b):
    if b >= 1e6:
        return f"{b / 1e6:.3f} MB"
    if b >= 1e3:
        return f"{b / 1e3:.2f} KB"
    return f"{b:.0f} B"


class Module:
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=0)
        self.name = port
        self.buf = b""
        self.rx = []  # 受信した (送信元, rssi, データ)
        time.sleep(0.1)
        self.ser.reset_input_buffer()
        self.node = self.command("RDNN") or "????"
        self.id = self.command("RDID") or "????????"

    def _lines(self, timeout):
        """timeout 秒まで待って、届いた行を1行ずつ返す"""
        end = time.time() + timeout
        while True:
            while b"\r\n" in self.buf:
                line, self.buf = self.buf.split(b"\r\n", 1)
                text = line.decode(errors="replace").strip()
                if text:
                    yield text
            if time.time() >= end:
                return
            n = self.ser.in_waiting
            if n:
                self.buf += self.ser.read(n)
            else:
                time.sleep(0.002)

    def _packet(self, line):
        """受信行 '00,0001,C8:41,42' なら受信キューに入れて True"""
        if ":" not in line or "," not in line.split(":", 1)[0]:
            return False
        head, data = line.split(":", 1)
        try:
            src = head.split(",")[1]
            rssi = int(head.split(",")[2], 16)
            payload = bytes(int(h, 16) for h in data.split(","))
        except (ValueError, IndexError):
            return False
        self.rx.append((src, rssi, payload))
        return True

    def command(self, cmd, timeout=0.5):
        """コマンドを送って応答1行を返す (応答待ち中の受信データはキューへ)"""
        self.ser.write((cmd + "\r\n").encode())
        for line in self._lines(timeout):
            if not self._packet(line):
                return line
        return ""

    def broadcast(self, text):
        """TXDA で送信。NG なら少し待って2回まで再送"""
        hexdata = text.encode().hex().upper()  # 初期設定 (DCIO) ではデータを16進数で渡す
        for _ in range(3):
            r = self.command("TXDA " + hexdata)
            if r != "NG":
                return r == "OK"
            time.sleep(0.05)
        return False

    def poll(self):
        for line in self._lines(0):
            self._packet(line)
        out, self.rx = self.rx, []
        return out


class Peer:
    """送信元ごとの受信統計"""

    def __init__(self):
        self.received = self.lost = self.rssi_sum = 0
        self.last = -1


def main():
    ap = argparse.ArgumentParser(description="IM920s 同士でひたすら送信し続ける")
    ap.add_argument("ports", nargs="*", help="シリアルポート (例: COM3 / /dev/ttyUSB0)。2つ指定すると1台のPCで2台を動かす")
    ap.add_argument("-p", type=float, default=0.25, help="送信周期 (秒)")
    ap.add_argument("-b", type=int, default=19200, help="通信速度 (bps)")
    ap.add_argument("--list", action="store_true", help="使えるシリアルポートを表示")
    args = ap.parse_args()

    if args.list or not args.ports:
        print("使えるシリアルポート:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device:15s} {p.description}")
        if not args.ports:
            print("\n使い方: python tools/pingpong.py COM3   (ポートを指定してください)")
        return 0

    try:
        mods = [Module(p, args.b) for p in args.ports]
    except serial.SerialException as e:
        print(f"エラー: {e}")
        print("ポート名が正しいか、他のプログラム (Tera Term など) が使っていないか確認してください")
        return 1
    for m in mods:
        if m.id == "????????":
            print(f"エラー: {m.name} が応答しません (通信速度が違う / IM920s ではない)")
            return 1
        print(f"{m.name}  ID {m.id}  ノード {m.node}")
    print("送信中... (Ctrl+C で停止)\n")

    seq = {m.name: 0 for m in mods}
    sent = {m.name: 0 for m in mods}
    ng = {m.name: 0 for m in mods}
    peers = {}  # (受信モジュール, 送信元ノード) -> Peer
    bytes_rx = 0
    t0 = time.time()
    # 2台のときは半周期ずらす
    nxt = {m.name: t0 + i * args.p / len(mods) for i, m in enumerate(mods)}
    last_print = t0

    try:
        while True:
            now = time.time()
            for m in mods:
                if now >= nxt[m.name]:
                    ok = m.broadcast(make_payload(m.node, seq[m.name]))
                    seq[m.name] += 1
                    sent[m.name] += 1
                    ng[m.name] += not ok
                    nxt[m.name] = time.time() + args.p + (random.uniform(0, 0.1) if len(mods) == 1 else 0)
            for m in mods:
                for src, rssi, data in m.poll():
                    parsed = parse_payload(data.decode(errors="replace"))
                    if not parsed:
                        continue  # 自分のプログラム以外からのデータや壊れたデータ
                    node, n = parsed
                    pr = peers.setdefault((m.name, node), Peer())
                    if n <= pr.last:
                        if n < pr.last - 10:  # 相手が再起動した
                            pr.last = n - 1
                        else:
                            continue  # 重複
                    if pr.last >= 0:
                        pr.lost += n - pr.last - 1
                    pr.last = n
                    pr.received += 1
                    pr.rssi_sum += rssi
                    bytes_rx += len(data)
                    if len(mods) == 1:
                        print(f"  受信 ノード{node} #{n} rssi={rssi}")
            now = time.time()
            if now - last_print >= 1:
                last_print = now
                sec = now - t0
                rx = "  ".join(f"{k[1]}->{k[0]} {v.received}受信/{v.lost}欠落" for k, v in peers.items()) or "受信なし"
                tx = "  ".join(f"{m.name} 送信{sent[m.name]}" for m in mods)
                print(f"{sec:5.0f}秒  {tx}  |  {rx}  |  受信累計 {human(bytes_rx)} ({bytes_rx / sec:.0f} B/s)")
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass

    sec = time.time() - t0
    print(f"\n--- {sec:.0f} 秒 ---")
    for m in mods:
        print(f"{m.name} (ノード {m.node}): 送信 {sent[m.name]} 回 (失敗 {ng[m.name]})  {human(sent[m.name] * SIZE)}")
    for (rxname, node), pr in peers.items():
        total = pr.received + pr.lost
        rate = 100 * pr.received / total if total else 0
        avg = pr.rssi_sum // pr.received if pr.received else 0
        print(f"ノード {node} -> {rxname}: 受信 {pr.received}  欠落 {pr.lost}  到達率 {rate:.1f}%  平均RSSI {avg}")
    print(f"受信合計 {human(bytes_rx)}  ({bytes_rx / sec:.0f} B/s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
