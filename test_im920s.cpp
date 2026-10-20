// im920s.hpp のテスト
//
// ビルド: g++ -std=c++17 -O2 -Wall -Wextra -o test_im920s test_im920s.cpp
// 実行:
//   ./test_im920s                                   # ハードウェア不要の単体テストのみ
//   ./test_im920s /dev/ttyUSB0 /dev/ttyUSB1         # 実機テストも実行
//   ./test_im920s /dev/ttyUSB0 /dev/ttyUSB1 500     # 連続送信テストの回数を指定 (既定 100)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "im920s.hpp"

using namespace std::chrono_literals;
using im920s::Module;
using im920s::Packet;
using im920s::Status;

// ---- 最小限のテスト枠組み ----
static int g_pass = 0, g_fail = 0;
static std::vector<std::string> g_failed;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++g_pass;                                                              \
    } else {                                                                 \
      ++g_fail;                                                              \
      std::printf("    NG: %s (%s:%d)\n", #cond, __FILE__, __LINE__);        \
      g_failed.push_back(std::string(current) + ": " + #cond);               \
    }                                                                        \
  } while (0)

static const char* current = "";

static void run(const char* name, const std::function<void()>& f) {
  // 環境変数 TEST_FILTER を指定すると、名前にその文字列を含むテストだけ実行する
  const char* filter = std::getenv("TEST_FILTER");
  if (filter && !std::strstr(name, filter)) return;
  current = name;
  int before = g_fail;
  std::printf("[ RUN  ] %s\n", name);
  auto t0 = im920s::Clock::now();
  try {
    f();
  } catch (const std::exception& e) {
    ++g_fail;
    std::printf("    例外: %s\n", e.what());
    g_failed.push_back(std::string(name) + ": 例外 " + e.what());
  }
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(im920s::Clock::now() - t0).count();
  std::printf("[%s] %s (%lldms)\n", g_fail == before ? "  OK  " : " FAIL ", name, (long long)ms);
}

template <class F>
static bool throws(F f) {
  try {
    f();
  } catch (...) {
    return true;
  }
  return false;
}

static std::vector<uint8_t> bytes(std::initializer_list<int> l) {
  std::vector<uint8_t> v;
  for (int x : l) v.push_back(static_cast<uint8_t>(x));
  return v;
}

// ---- 単体テスト (ハードウェア不要) ----
static void unit_tests() {
  run("to_hex", [] {
    auto v = bytes({0x00, 0x0F, 0xA5, 0xFF});
    CHECK(im920s::to_hex(v.data(), v.size()) == "000FA5FF");
    CHECK(im920s::to_hex(nullptr, 0) == "");
    std::vector<uint8_t> all(256);
    for (int i = 0; i < 256; ++i) all[i] = i;
    std::string h = im920s::to_hex(all.data(), all.size());
    CHECK(h.size() == 512);
    CHECK(h.substr(0, 6) == "000102");
    CHECK(h.substr(506) == "FDFEFF");
  });

  run("parse_hex", [] {
    CHECK(im920s::parse_hex("00", 2) == 0u);
    CHECK(im920s::parse_hex("FF", 2) == 255u);
    CHECK(im920s::parse_hex("ff", 2) == 255u);
    CHECK(im920s::parse_hex("23B8", 4) == 0x23B8u);
    CHECK(im920s::parse_hex("000023B7", 8) == 0x23B7u);
    CHECK(!im920s::parse_hex("F", 2));
    CHECK(!im920s::parse_hex("FFF", 2));
    CHECK(!im920s::parse_hex("G0", 2));
    CHECK(!im920s::parse_hex("", 2));
    CHECK(!im920s::parse_hex("NG", 2));
  });

  run("parse_packet: 正常な受信行", [] {
    auto p = im920s::parse_packet("00,0002,EE:48,45,4C,4C,4F");
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->node == 2);
    CHECK(p->rssi == 0xEE);
    CHECK(p->text() == "HELLO");

    auto q = im920s::parse_packet("00,FFFF,00:00");
    CHECK(q && q->node == 0xFFFF && q->rssi == 0 && q->data == bytes({0}));

    auto r = im920s::parse_packet("00,0001,C8:00,FF,0D,0A");
    CHECK(r && r->data == bytes({0x00, 0xFF, 0x0D, 0x0A}));
  });

  run("parse_packet: 32バイト", [] {
    std::string line = "00,0001,80:";
    for (int i = 0; i < 32; ++i) line += (i ? ",41" : "41");
    auto p = im920s::parse_packet(line);
    CHECK(p && p->data.size() == 32);
    line += ",41";  // 33バイトは仕様外
    CHECK(!im920s::parse_packet(line));
  });

  run("parse_packet: 受信行ではないもの", [] {
    const char* bad[] = {
        "",
        "OK",
        "NG",
        "000023B8",                    // RDID の応答
        "IM920s Ver.01.21",            // RDVR の応答
        "ID:000023B8",                 // RPRM の応答 (コロンを含む)
        "STNN:0002",
        "00,0002,EE:",                 // データなし
        "00,0002,EE:4",                // 桁不足
        "00,0002,EE:41,",              // 末尾カンマ
        "00,0002,EE:41;42",            // 区切り違い
        "00,0002,EE:4142",             // 区切りなし
        "00,0002,EE:GG",               // 16進でない
        "00,002,EE:41",                // ノード桁不足
        "00,0002,E:41",                // RSSI 桁不足
        "0,0002,EE:41",
        "00;0002;EE:41",
        "00,0002,EE,41",               // コロンなし
        "XX,0002,EE:41",
    };
    for (const char* s : bad) {
      bool ok = !im920s::parse_packet(s);
      if (!ok) std::printf("    誤って受信行と判定: \"%s\"\n", s);
      CHECK(ok);
    }
  });

  run("parse_packet: 全バイト値の往復", [] {
    for (int start = 0; start < 256; start += 32) {
      std::vector<uint8_t> v;
      for (int i = start; i < start + 32; ++i) v.push_back(i);
      std::string line = "00,0001,EE:";
      for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) line += ",";
        line += im920s::to_hex(&v[i], 1);
      }
      auto p = im920s::parse_packet(line);
      CHECK(p && p->data == v);
    }
  });

  run("Module: 存在しないポート", [] {
    CHECK(throws([] { Module m("/dev/ttyUSB_does_not_exist"); }));
  });

  run("Module: シリアルポートでないファイル", [] {
    CHECK(throws([] { Module m("/dev/null"); }));
  });
}

// ---- 実機テスト ----
static void hardware_tests(const std::string& pa, const std::string& pb, int stress) {
  run("ポートの二重オープンを拒否", [&] {
    Module a(pa);
    CHECK(throws([&] { Module again(pa); }));
  });

  run("タイムアウト後に遅れて来た応答を次のコマンドの応答と取り違えない", [&] {
    Module::Options o;
    o.unicast_timeout = 1000ms;  // 相手不在の NG (約6秒後に来る) より短くする
    Module m(pa, o);
    std::string id = m.id();
    CHECK(m.unicast(0x0F0F, "X") == Status::Timeout);
    CHECK(m.id() == id);                          // 遅れて来た NG を ID と取り違えない
    CHECK(m.broadcast("AFTER") == Status::Ok);    // 送信も正常
    CHECK(m.id() == id);
  });

  Module a(pa), b(pb);
  std::this_thread::sleep_for(300ms);
  a.clear();
  b.clear();

  uint16_t na = 0, nb = 0;

  run("情報の読み出し", [&] {
    std::string ida = a.id(), idb = b.id();
    std::printf("    A: ID=%s  B: ID=%s\n", ida.c_str(), idb.c_str());
    CHECK(im920s::parse_hex(ida, 8).has_value());
    CHECK(im920s::parse_hex(idb, 8).has_value());
    CHECK(ida != idb);

    auto va = a.version();
    std::printf("    A: %s\n", va.c_str());
    CHECK(va.rfind("IM920s", 0) == 0);

    auto a_node = a.node(), b_node = b.node();
    CHECK(a_node && b_node);
    if (a_node) na = *a_node;
    if (b_node) nb = *b_node;
    std::printf("    ノード A=%04X B=%04X\n", na, nb);
    CHECK(na != nb);

    auto ca = a.channel(), cb = b.channel();
    CHECK(ca && cb && *ca == *cb);
    std::printf("    チャンネル A=%d B=%d\n", ca.value_or(-1), cb.value_or(-1));

    auto ra = a.rssi();
    CHECK(ra.has_value());
    auto ga = a.group(), gb = b.group();
    CHECK(ga && gb);
  });

  run("同じコマンドを100回連続 (応答の取りこぼし確認)", [&] {
    std::string id = a.id();
    int ok = 0;
    for (int i = 0; i < 100; ++i) ok += (a.id() == id);
    std::printf("    一致 %d/100\n", ok);
    CHECK(ok == 100);
  });

  run("未知のコマンドは NG", [&] {
    CHECK(a.command("XXXX") == "NG");
    CHECK(a.id().size() == 8);  // その後も正常に使える
  });

  run("送信データの長さチェック (送信せずに例外)", [&] {
    CHECK(throws([&] { a.broadcast(std::vector<uint8_t>{}); }));
    CHECK(throws([&] { a.broadcast(std::string(33, 'A')); }));
    CHECK(throws([&] { a.unicast(nb, std::string(33, 'A')); }));
    CHECK(throws([&] { a.unicast(nb, std::string()); }));
    CHECK(!b.receive(500ms));  // 何も送られていない
  });

  run("broadcast A->B 文字列", [&] {
    CHECK(a.broadcast("HELLO") == Status::Ok);
    auto p = b.receive(1000ms);
    CHECK(p && p->text() == "HELLO" && p->node == na);
  });

  run("broadcast B->A 文字列", [&] {
    CHECK(b.broadcast("WORLD") == Status::Ok);
    auto p = a.receive(1000ms);
    CHECK(p && p->text() == "WORLD" && p->node == nb);
  });

  run("broadcast 長さ 1〜32 バイト全部", [&] {
    int ok = 0;
    for (int len = 1; len <= 32; ++len) {
      std::vector<uint8_t> v;
      for (int i = 0; i < len; ++i) v.push_back('a' + (i + len) % 26);
      CHECK(a.broadcast(v) == Status::Ok);
      auto p = b.receive(1000ms);
      bool good = p && p->data == v;
      if (!good) std::printf("    長さ %d で不一致\n", len);
      ok += good;
    }
    std::printf("    一致 %d/32\n", ok);
    CHECK(ok == 32);
  });

  run("broadcast 全バイト値 0x00〜0xFF (CR/LF/カンマ/コロン含む)", [&] {
    int ok = 0;
    for (int start = 0; start < 256; start += 32) {
      std::vector<uint8_t> v;
      for (int i = start; i < start + 32; ++i) v.push_back(i);
      CHECK(a.broadcast(v) == Status::Ok);
      auto p = b.receive(1000ms);
      ok += (p && p->data == v);
    }
    std::printf("    一致 %d/8 パケット\n", ok);
    CHECK(ok == 8);
  });

  run("unicast A->B (相手から受信確認あり)", [&] {
    CHECK(a.unicast(nb, "UNI") == Status::Ok);
    auto p = b.receive(1000ms);
    CHECK(p && p->text() == "UNI" && p->node == na);
  });

  run("unicast B->A", [&] {
    CHECK(b.unicast(na, std::vector<uint8_t>{0x00, 0x0D, 0x0A, 0xFF}) == Status::Ok);
    auto p = a.receive(1000ms);
    CHECK(p && p->data == bytes({0x00, 0x0D, 0x0A, 0xFF}));
  });

  run("unicast 存在しないノードは NG (約6秒)", [&] {
    uint16_t ghost = 0x0F0F;
    auto t0 = im920s::Clock::now();
    Status st = a.unicast(ghost, "X");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(im920s::Clock::now() - t0).count();
    std::printf("    結果 %s, %lldms\n", im920s::to_string(st), (long long)ms);
    CHECK(st == Status::Ng);
    CHECK(a.id().size() == 8);  // その後も正常に使える
    b.clear();
  });

  run("receive タイムアウト", [&] {
    auto t0 = im920s::Clock::now();
    auto p = b.receive(300ms);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(im920s::Clock::now() - t0).count();
    CHECK(!p);
    CHECK(ms >= 290 && ms < 600);
  });

  run("コマンド実行中に届いた受信データを失わない", [&] {
    // B が送信 → A はその間にコマンドを実行 → 応答は正しく、受信データはキューに残る
    CHECK(b.broadcast("INTR") == Status::Ok);
    std::this_thread::sleep_for(100ms);
    std::string id = a.id();
    CHECK(im920s::parse_hex(id, 8).has_value());
    auto p = a.receive(500ms);
    CHECK(p && p->text() == "INTR");
  });

  run("受信データが複数溜まっても順番どおり取り出せる", [&] {
    for (int i = 0; i < 5; ++i) CHECK(a.broadcast("Q" + std::to_string(i)) == Status::Ok);
    std::this_thread::sleep_for(300ms);
    int ok = 0;
    for (int i = 0; i < 5; ++i) {
      auto p = b.receive(1000ms);
      ok += (p && p->text() == "Q" + std::to_string(i));
    }
    CHECK(ok == 5);
  });

  run("clear で受信キューが空になる", [&] {
    CHECK(a.broadcast("JUNK") == Status::Ok);
    std::this_thread::sleep_for(300ms);
    b.clear();
    CHECK(!b.receive(300ms));
  });

  auto stress_test = [&](const char* name, Module& tx, Module& rx, uint16_t tx_node) {
    run(name, [&] {
      int sent = 0, received = 0, corrupted = 0;
      uint64_t rssi_sum = 0;
      auto t0 = im920s::Clock::now();
      for (int i = 0; i < stress; ++i) {
        char msg[33];
        std::snprintf(msg, sizeof(msg), "STRESS-%06d-%s", i, "abcdefghijklmn");
        if (tx.broadcast(std::string(msg)) == Status::Ok) ++sent;
        if (auto p = rx.receive(1000ms)) {
          if (p->text() == msg && p->node == tx_node) {
            ++received;
            rssi_sum += p->rssi;
          } else {
            ++corrupted;
          }
        }
      }
      double sec = std::chrono::duration<double>(im920s::Clock::now() - t0).count();
      std::printf("    送信OK %d/%d, 受信一致 %d, 不一致 %d, 平均RSSI 0x%02X, %.1f パケット/秒\n", sent, stress,
                  received, corrupted, received ? (unsigned)(rssi_sum / received) : 0u, stress / sec);
      CHECK(sent == stress);
      CHECK(corrupted == 0);
      CHECK(received >= stress * 99 / 100);  // 無線なので 1% までの取りこぼしは許容
    });
  };
  stress_test("連続送信 A->B", a, b, na);
  stress_test("連続送信 B->A", b, a, nb);

  // 2台が同時に送信したとき。count_set には受信した番号を記録する
  auto simultaneous = [&](bool use_unicast, int rounds, int& ok_a, int& ok_b, std::set<int>& got_a,
                          std::set<int>& got_b, int& corrupted) {
    auto collect = [&](Module& m, const char* prefix, std::set<int>& got) {
      while (auto p = m.receive(0ms)) {
        std::string t = p->text();
        if (t.rfind(prefix, 0) == 0) got.insert(std::atoi(t.c_str() + std::strlen(prefix)));
        else ++corrupted;
      }
    };
    for (int i = 0; i < rounds; ++i) {
      std::string ma = "FROM-A" + std::to_string(i), mb = "FROM-B" + std::to_string(i);
      std::thread t([&] { ok_b += ((use_unicast ? b.unicast(na, mb) : b.broadcast(mb)) == Status::Ok); });
      ok_a += ((use_unicast ? a.unicast(nb, ma) : a.broadcast(ma)) == Status::Ok);
      t.join();
      std::this_thread::sleep_for(200ms);
      collect(b, "FROM-A", got_b);
      collect(a, "FROM-B", got_a);
    }
    std::this_thread::sleep_for(500ms);
    collect(b, "FROM-A", got_b);
    collect(a, "FROM-B", got_a);
  };

  run("双方向に同時 broadcast (衝突で失われるのは仕様、壊れたデータが来ないこと)", [&] {
    int ok_a = 0, ok_b = 0, corrupted = 0;
    std::set<int> got_a, got_b;
    simultaneous(false, 20, ok_a, ok_b, got_a, got_b, corrupted);
    std::printf("    送信OK A=%d B=%d / 受信 B側=%zu A側=%zu (各20) / 不一致 %d\n", ok_a, ok_b, got_b.size(),
                got_a.size(), corrupted);
    std::printf("    (同時に送ると互いに受信できない。確実に届けたいときは unicast を使う)\n");
    CHECK(ok_a == 20 && ok_b == 20);
    CHECK(corrupted == 0);
  });

  run("双方向に同時 unicast (モジュールの自動再送で全部届く)", [&] {
    int ok_a = 0, ok_b = 0, corrupted = 0;
    std::set<int> got_a, got_b;
    simultaneous(true, 20, ok_a, ok_b, got_a, got_b, corrupted);
    std::printf("    送信OK A=%d B=%d / 受信 B側=%zu A側=%zu (各20) / 不一致 %d\n", ok_a, ok_b, got_b.size(),
                got_a.size(), corrupted);
    CHECK(ok_a == 20 && ok_b == 20);
    CHECK(got_a.size() == 20 && got_b.size() == 20);
    CHECK(corrupted == 0);
  });

  run("最後に解釈できなかった行がない", [&] {
    for (auto* m : {&a, &b})
      for (auto& l : m->unknown_lines()) std::printf("    %s: \"%s\"\n", m->path().c_str(), l.c_str());
    CHECK(a.unknown_lines().empty());
    CHECK(b.unknown_lines().empty());
  });
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);  // パイプ経由でも1行ずつ出力する
  std::printf("===== 単体テスト =====\n");
  unit_tests();

  if (argc >= 3) {
    int stress = argc >= 4 ? std::atoi(argv[3]) : 100;
    std::printf("\n===== 実機テスト (%s <-> %s) =====\n", argv[1], argv[2]);
    try {
      hardware_tests(argv[1], argv[2], stress);
    } catch (const std::exception& e) {
      ++g_fail;
      std::printf("実機テストを中断: %s\n", e.what());
      g_failed.push_back(std::string("実機テスト: ") + e.what());
    }
  } else {
    std::printf("\n(実機テストはポートを2つ指定すると実行します)\n");
  }

  std::printf("\n===== 結果: 成功 %d / 失敗 %d =====\n", g_pass, g_fail);
  for (auto& f : g_failed) std::printf("  失敗: %s\n", f.c_str());
  return g_fail == 0 ? 0 : 1;
}
