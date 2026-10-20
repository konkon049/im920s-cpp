# im920s

Interplan の 920MHz 無線モジュール **IM920s** を、Linux PC から USB シリアル経由で使うための C++ ヘッダオンリーライブラリです。

- ヘッダファイル1つ (`im920s.hpp`)、C++17、外部ライブラリ不要
- 全員宛て送信 (`TXDA`)、宛先指定送信 (`TXDU`)、受信、設定の読み出し
- モジュールの設定を書き換えるコマンドは扱いません (読み出しのみ)

動作確認: IM920s Ver.01.21 + IM920-USB (USB インターフェイスボード) × 2台、Ubuntu

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

`broadcast` / `unicast` は `im920s::Status::Ok` / `Ng` / `Timeout` を返します。
ポートを開けない、他のプログラムが使用中などのときは `im920s::Error` 例外を投げます。

## ライブラリが吸収している IM920s の挙動

実機で確認した挙動に合わせて作っています。

| モジュールの挙動 | ライブラリの対応 |
|---|---|
| 初期設定 (DCIO) では `TXDA` のデータを16進数で渡す必要がある | 送信データを自動で16進数に変換 |
| 33バイト以上を送ると `OK` を返しつつ黙って32バイトに切り詰める | 1〜32バイト以外は送信前に `std::invalid_argument` |
| 16進数の書式が崩れていても `OK` を返し、壊れたデータを送る | 変換をライブラリが行うので発生しない |
| 送信間隔が200ms未満だと約1割が `NG` になる | 自動で間隔をあけ、`NG` なら再送 |
| 存在しないノードへの `TXDU` は約6秒後に `NG`、その間は他のコマンドを受け付けない | 応答待ちを8秒に設定。タイムアウト後に遅れて来た応答は捨てる |
| コマンドの応答待ち中にも受信データが届く | 捨てずに受信キューへ積む |
| 2台が同時に `TXDA` で送るとお互いに受信できない | 仕様。確実に届けたいデータは `unicast` (モジュールが自動再送) を使う |

## ビルドとテスト

```bash
g++ -std=c++17 -O2 -Wall -pthread -o test_im920s test_im920s.cpp

./test_im920s                                # ハードウェア不要の単体テスト
./test_im920s /dev/ttyUSB0 /dev/ttyUSB1 300  # 2台つないだ実機テスト (連続送信300回)
TEST_FILTER=unicast ./test_im920s /dev/ttyUSB0 /dev/ttyUSB1   # 名前で絞り込み
```

実機テスト結果 (152項目すべて成功) の例:

| テスト | 結果 |
|---|---|
| 連続送信 300回 (A→B, B→A) | どちらも 300/300、約4パケット/秒 |
| 長さ 1〜32 バイト、全バイト値 0x00〜0xFF | 全て一致 |
| 2台同時に unicast | 20/20 届く |
| 2台同時に broadcast | ほぼ届かない (仕様)、壊れたデータは来ない |

## 使用例

```bash
g++ -std=c++17 -o example example.cpp
./example /dev/ttyUSB0
```

## 注意

- `/dev/ttyUSB*` を使うには `dialout` グループに入っている必要があります。
- miniterm などで同じポートを開いたままだと、`Module` の作成時に「使用中です」の例外になります。

## ライセンス

MIT License
