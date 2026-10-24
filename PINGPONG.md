# pingpong: IM920s 同士でひたすら送り合う

`Ctrl+C` で止めると、送信数・受信数・欠落・到達率・RSSI・実効速度を表示します。

## すぐ使う (Ubuntu 22.04 / 24.04, x86_64)

```bash
git clone -b pingpong-temp https://github.com/konkon049/im920s-cpp
cd im920s-cpp
./run.sh
```

`bin/pingpong` はビルド済み (静的リンク) なので、g++ がなくても動きます。

初めての PC で `/dev/ttyUSB0` が見えない・権限がないときは、一度だけ:

```bash
bash setup_ubuntu.sh      # brltty の削除、dialout グループへの追加、ビルド
```

(dialout に追加したらログインし直す)

## 使い方

| コマンド | 内容 |
|---|---|
| `./run.sh` | 別々の PC に1台ずつ。両方の PC で実行する。IM920s を自動で探す |
| `./run.sh /dev/ttyUSB1` | ポートを指定 |
| `./run.sh pair` | 1台の PC に2台つないで高速モード |
| `./run.sh turbo` | 同上 + 115200bps に一時的に切り替えて最速。終了時に 19200 に戻す (フラッシュには書かない) |

## 実測 (32バイト/パケット, 2台を近くに置いた状態)

| モード | 速度 | 到達率 |
|---|---|---|
| 別々のPC (`./run.sh`) | 片方向 約 120 B/s | 97〜98% (同時に送ると届かない) |
| `./run.sh pair` | 合計 約 250 B/s | 100% |
| `./run.sh turbo` | 合計 約 420 B/s | 99〜100% |

## ファイル

- `examples/pingpong.cpp` 本体 (`make` で `build/pingpong`、`make bin/pingpong` で静的リンク版)
- `run.sh` 実行用 (ポート自動検出・権限チェック)
- `setup_ubuntu.sh` 初回セットアップ
- `tools/pingpong.py` Python 版 (Windows でも動く: `pip install pyserial` → `python tools/pingpong.py COM3`)
