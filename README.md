# im920s-cpp

Interplan の 920MHz 無線モジュール **IM920s** を、Linux PC から USB シリアル経由で使うための C++ ヘッダオンリーライブラリです。

- ヘッダファイル1つ (`include/im920s.hpp`)、C++17、外部ライブラリ不要
- 全員宛て送信 (`TXDA`)、宛先指定送信 (`TXDU`)、受信、設定の読み出し
- モジュールの設定を書き換えるコマンドは扱いません (読み出しのみ)
- 実機2台で 152 項目のテスト済み

動作確認: IM920s Ver.01.21 + IM920-USB × 2台、Ubuntu

## フォルダ構成

```
include/im920s.hpp       ライブラリ本体
tests/test_im920s.cpp    テスト (単体テスト + 実機テスト)
examples/example.cpp     ライブラリの使用例
tools/                   ライブラリを使わない単体のテストプログラム
  im920s_test.py           Python 版の通信テスト
  im920s_test.cpp          C++ 版の通信テスト
  im920s_min.cpp           最低限の通信テスト (約50行)
docs/
  setup.md                 ハードウェアの準備とトラブルシューティング
  commands.md              IM920s のコマンドと実機で確認した挙動
  test-results.md          テスト結果
Makefile
```

## クイックスタート

```bash
make                     # build/ に全部ビルド
make test                # ハードウェア不要の単体テスト
make hwtest              # 2台つないだ実機テスト (/dev/ttyUSB0 と /dev/ttyUSB1)
make hwtest N=300        # 連続送信テストの回数を指定
```

モジュールがつながっているかだけ確認したいとき:

```bash
python3 -m serial.tools.miniterm -e --eol CRLF /dev/ttyUSB0 19200
# RDID と入力して Enter → 8桁の ID が返れば OK (終了は Ctrl+])
```

応答がないときは [docs/setup.md](docs/setup.md) を見てください。

## 使い方

```cpp
#include "im920s.hpp"
using namespace std::chrono_literals;

im920s::Module m("/dev/ttyUSB0");      // 19200bps で開く

m.broadcast("HELLO");                  // 全員宛て (TXDA)
m.unicast(0x0001, "HI");               // ノード0001宛て。相手の受信確認つき (TXDU)

if (auto p = m.receive(1000ms))        // 受信 (1秒待つ)
  std::cout << p->node << ": " << p->text() << "\n";

std::cout << m.id() << " " << m.channel().value_or(-1) << "\n";  // 読み出し
```

| 関数 | 内容 |
|---|---|
| `broadcast(data)` | 全員宛てに送信。`Status::Ok` / `Ng` / `Timeout` を返す |
| `unicast(node, data)` | 指定ノード宛てに送信。相手から受信確認が返れば `Ok` |
| `receive(timeout)` | 受信データ (`Packet`: `node`, `rssi`, `data`, `text()`) を1つ返す。なければ `nullopt` |
| `id()` `node()` `channel()` `rssi()` `version()` `group()` | 読み出し |
| `command(cmd)` | 任意のコマンドを送って応答1行を返す |
| `clear()` | 受信キューを空にする |

- 送信データは 1〜32 バイト。範囲外は `std::invalid_argument`
- ポートを開けない・他のプログラムが使用中などのときは `im920s::Error` 例外
- 待ち時間や再送回数は `im920s::Module::Options` で変更できる

## ライブラリが吸収している IM920s の挙動

| モジュールの挙動 | ライブラリの対応 |
|---|---|
| 初期設定 (DCIO) では送信データを16進数で渡す必要がある | 自動で16進数に変換 |
| 33バイト以上を送ると `OK` を返しつつ黙って32バイトに切り詰める | 送信前に例外 |
| 送信間隔が200ms未満だと約1割が `NG` になる | 自動で間隔をあけ、`NG` なら再送 |
| 存在しないノードへの `TXDU` は約6秒後に `NG` | 応答待ちを8秒に設定。タイムアウト後に遅れて来た応答は捨てる |
| コマンドの応答待ち中にも受信データが届く | 捨てずに受信キューへ積む |
| 2台が同時に `TXDA` で送るとお互い受信できない | 仕様。確実に届けたいデータは `unicast` を使う |

詳しくは [docs/commands.md](docs/commands.md)、テスト結果は [docs/test-results.md](docs/test-results.md)。

## ライセンス

MIT License
