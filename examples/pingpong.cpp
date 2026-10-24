// IM920s 同士でひたすら送信し続ける (Ctrl+C で停止して統計を表示)
// ビルド: make  (build/pingpong ができる)
//
// 2台を同じPCにつなぐ場合 (A→B, B→A を交互に送信して届いたか確認):
//   ./build/pingpong /dev/ttyUSB0 /dev/ttyUSB1
//
// 1台ずつ別のPCにつなぐ場合 (両方のPCで実行。送信しつつ相手からの受信を表示):
//   ./build/pingpong /dev/ttyUSB0
//
// オプション: -i <ms> 送信ごとの追加待ち (既定 0 = 最速。ライブラリが同じモジュールの送信を 200ms 以上あける)
//            -s <bytes> 1パケットのバイト数 (8〜32, 既定 32 = IM920s の上限)
//            -u 無線ACK付きの unicast (TXDU) で送る
//            -f 高速モード (2台のみ。受信確認を待たずに送り続ける)  -p <ms> 高速モードの各モジュールの送信周期 (既定 200)
//            -t ターボ: 115200bps に一時的に切り替えて高速モード (-f -p 130)。終了時に 19200 に戻す
//            -b <baud> PC とモジュール間の通信速度 (既定 19200。モジュール側も SBRT で同じ速度にしておく)
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../include/im920s.hpp"

using namespace std::chrono_literals;
using std::chrono::milliseconds;

static std::atomic<bool> g_stop{false};

static std::size_t g_size = 32;  // 1パケットのバイト数 (IM920s の上限は 32)
static int g_baud = 19200;        // PC とモジュール間の通信速度 (-b)

static im920s::Module::Options make_opt() {
  im920s::Module::Options o;
  o.baud = g_baud;
  return o;
}

static std::string human_bytes(double b) {
  char buf[32];
  if (b >= 1e6) std::snprintf(buf, sizeof buf, "%.3f MB", b / 1e6);
  else if (b >= 1e3) std::snprintf(buf, sizeof buf, "%.2f KB", b / 1e3);
  else std::snprintf(buf, sizeof buf, "%.0f B", b);
  return buf;
}

struct Stats {
  long sent = 0, send_ng = 0, received = 0, lost = 0;
  long rssi_sum = 0;
  double bytes_sent = 0, bytes_received = 0;
  void print(const std::string& name, double sec) const {
    std::cout << name << ": 送信 " << sent << " (失敗 " << send_ng << ")  受信 " << received << "  欠落 " << lost;
    if (received) std::cout << "  平均RSSI " << rssi_sum / received;
    if (sent) std::cout << "  到達率 " << 100.0 * received / sent << "%";
    std::cout << "\n    送信量 " << human_bytes(bytes_sent) << "  受信量 " << human_bytes(bytes_received);
    if (sec > 0) std::cout << "  実効速度 " << bytes_received / sec << " B/s (" << bytes_received * 8 / sec << " bps)";
    std::cout << "\n";
  }
};

// "A:123:" の後ろを埋めて g_size バイトにする
static std::string make_payload(char tag, long seq) {
  std::string s = std::string(1, tag) + ":" + std::to_string(seq) + ":";
  for (std::size_t i = 0; s.size() < g_size; i++) s += static_cast<char>('a' + (seq + i) % 26);
  return s;
}

// "A:123:..." -> 123, 形式が違えば -1
static long parse_seq(const std::string& s, char tag) {
  if (s.size() < 3 || s[0] != tag || s[1] != ':') return -1;
  char* end = nullptr;
  long v = std::strtol(s.c_str() + 2, &end, 10);
  return (end && (*end == '\0' || *end == ':')) ? v : -1;
}

static im920s::Status send(im920s::Module& m, bool unicast, uint16_t to, const std::string& s) {
  return unicast ? m.unicast(to, s) : m.broadcast(s);
}

// 2台モード: A が送って B で受信、B が送って A で受信、を交互に繰り返す
static void run_pair(const std::string& pa, const std::string& pb, milliseconds interval, bool unicast) {
  im920s::Module a(pa, make_opt()), b(pb, make_opt());
  uint16_t na = a.node().value_or(0), nb = b.node().value_or(0);
  std::cout << "A " << pa << " ID " << a.id() << " node " << std::hex << na << "\n"
            << "B " << pb << " ID " << b.id() << " node " << nb << std::dec << "\n";
  a.clear();
  b.clear();

  struct Side { im920s::Module& tx; im920s::Module& rx; char tag; uint16_t to; Stats st; };
  Side sides[2] = {{a, b, 'A', nb, {}}, {b, a, 'B', na, {}}};
  long seq = 0;
  auto t0 = im920s::Clock::now();

  while (!g_stop) {
    for (auto& s : sides) {
      if (g_stop) break;
      std::string msg = make_payload(s.tag, seq);
      auto st = send(s.tx, unicast, s.to, msg);
      s.st.sent++;
      s.st.bytes_sent += msg.size();
      bool ok = false;
      int rssi = 0;
      if (st != im920s::Status::Ok) {
        s.st.send_ng++;
      } else {
        // 自分の番号が来るまで待つ (古いパケットは読み捨て)
        auto end = im920s::Clock::now() + 1000ms;
        while (im920s::Clock::now() < end) {
          auto p = s.rx.receive(std::chrono::duration_cast<milliseconds>(end - im920s::Clock::now()));
          if (!p) break;
          if (parse_seq(p->text(), s.tag) == seq) { ok = p->text() == msg; rssi = p->rssi; break; }
        }
      }
      if (ok) { s.st.received++; s.st.rssi_sum += rssi; s.st.bytes_received += msg.size(); } else s.st.lost++;
      double total = sides[0].st.bytes_received + sides[1].st.bytes_received;
      double sec = std::chrono::duration<double>(im920s::Clock::now() - t0).count();
      std::cout << s.tag << "->" << (s.tag == 'A' ? 'B' : 'A') << " #" << seq << " "
                << im920s::to_string(st) << (ok ? " 受信OK rssi=" + std::to_string(rssi) : " 未受信")
                << "  累計 " << human_bytes(total) << " (" << static_cast<int>(total / sec) << " B/s)\n";
      if (interval.count() > 0) std::this_thread::sleep_for(interval);
    }
    seq++;
  }

  double sec = std::chrono::duration<double>(im920s::Clock::now() - t0).count();
  std::cout << "\n--- " << static_cast<long>(sec) << " 秒 ---\n";
  sides[0].st.print("A->B", sec);
  sides[1].st.print("B->A", sec);
  std::cout << "合計受信量 " << human_bytes(sides[0].st.bytes_received + sides[1].st.bytes_received) << "\n";
}

// 1台モード: 一定間隔で送信しながら、相手から届いたものを表示する
static void run_single(const std::string& port, milliseconds interval, bool unicast) {
  im920s::Module m(port, make_opt());
  uint16_t me = m.node().value_or(0);
  std::cout << port << " ID " << m.id() << " node " << std::hex << me << std::dec << "\n";
  if (unicast) std::cout << "(1台モードでは宛先不明のため broadcast で送信します)\n";
  m.clear();

  // 両方が同時に TXDA すると互いに受信できないので、間隔に揺らぎを入れる
  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> jitter(0, static_cast<int>(std::max<long>(interval.count(), 200) / 2));
  Stats st;
  long seq = 0, last_rx = -1;
  auto t0 = im920s::Clock::now(), next = t0;

  while (!g_stop) {
    if (im920s::Clock::now() >= next) {
      std::string msg = make_payload('S', seq++);
      auto r = m.broadcast(msg);
      st.sent++;
      st.bytes_sent += msg.size();
      if (r != im920s::Status::Ok) st.send_ng++;
      std::cout << "送信 " << msg << " " << im920s::to_string(r) << "\n";
      next = im920s::Clock::now() + interval + milliseconds(jitter(rng));
    }
    if (auto p = m.receive(20ms)) {
      long s = parse_seq(p->text(), 'S');
      st.received++;
      st.rssi_sum += p->rssi;
      st.bytes_received += p->data.size();
      if (s >= 0 && last_rx >= 0 && s > last_rx + 1) st.lost += s - last_rx - 1;
      if (s >= 0) last_rx = s;
      std::cout << "    受信 node=" << std::hex << p->node << std::dec << " rssi=" << int(p->rssi) << " : " << p->text()
                << "\n";
    }
  }
  std::cout << "\n";
  st.print(port, std::chrono::duration<double>(im920s::Clock::now() - t0).count());
}

// 高速モード: 受信確認を待たずに A と B が period ごとに送信する (B は半周期ずらす)。
// 受信側の転送 (約56ms) と次の送信を重ねられるので、往復を待つより速い
static void run_fast(const std::string& pa, const std::string& pb, milliseconds period) {
  auto opt = make_opt();
  opt.send_interval = period;
  im920s::Module a(pa, opt), b(pb, opt);
  std::cout << "高速モード 周期 " << period.count() << "ms\n"
            << "A " << pa << " ID " << a.id() << "\nB " << pb << " ID " << b.id() << "\n";
  a.clear();
  b.clear();

  struct Side {
    im920s::Module& tx; im920s::Module& rx; char tag; Stats st;
    long seq = 0, last_rx = -1; im920s::Clock::time_point next;
  };
  auto t0 = im920s::Clock::now();
  Side sides[2] = {{a, b, 'A', {}, 0, -1, t0}, {b, a, 'B', {}, 0, -1, t0 + period / 2}};
  auto last_print = t0;

  auto drain = [&](Side& s) {
    while (auto p = s.rx.receive(0ms)) {
      long n = parse_seq(p->text(), s.tag);
      if (n < 0 || p->text() != make_payload(s.tag, n)) continue;  // 壊れたデータは数えない
      if (n <= s.last_rx) continue;                                 // 重複して届いた分は数えない
      s.st.lost += n - s.last_rx - 1;
      s.last_rx = n;
      s.st.received++;
      s.st.rssi_sum += p->rssi;
      s.st.bytes_received += p->data.size();
    }
  };

  while (!g_stop) {
    auto now = im920s::Clock::now();
    for (auto& s : sides) {
      if (now < s.next) continue;
      auto r = s.tx.broadcast(make_payload(s.tag, s.seq++));
      s.st.sent++;
      s.st.bytes_sent += g_size;
      if (r != im920s::Status::Ok) s.st.send_ng++;
      s.next += period;
      if (s.next < im920s::Clock::now()) s.next = im920s::Clock::now();  // 遅れたら追いつこうとしない
    }
    for (auto& s : sides) drain(s);

    now = im920s::Clock::now();
    if (now - last_print >= 1s) {
      last_print = now;
      double sec = std::chrono::duration<double>(now - t0).count();
      double total = sides[0].st.bytes_received + sides[1].st.bytes_received;
      std::cout << static_cast<long>(sec) << "秒  A->B " << sides[0].st.received << "/" << sides[0].st.sent
                << "  B->A " << sides[1].st.received << "/" << sides[1].st.sent
                << "  NG " << sides[0].st.send_ng + sides[1].st.send_ng
                << "  累計 " << human_bytes(total) << " (" << static_cast<int>(total / sec) << " B/s)\n";
    }
    auto wake = std::min(sides[0].next, sides[1].next);
    auto wait = std::chrono::duration_cast<milliseconds>(wake - im920s::Clock::now());
    if (wait > 0ms) std::this_thread::sleep_for(std::min(wait, 5ms));  // 受信データは OS がためておく
  }

  std::this_thread::sleep_for(300ms);  // 飛んでいる途中のパケットを受け取る
  for (auto& s : sides) drain(s);
  double sec = std::chrono::duration<double>(im920s::Clock::now() - t0).count();
  std::cout << "\n--- " << static_cast<long>(sec) << " 秒 ---\n";
  sides[0].st.print("A->B", sec);
  sides[1].st.print("B->A", sec);
  std::cout << "合計受信量 " << human_bytes(sides[0].st.bytes_received + sides[1].st.bytes_received) << "\n";
}

// ---- 通信速度の自動切り替え ----
// SBRT は ENWR なしだとフラッシュに保存されない (電源を入れ直すと 19200 に戻る)

static bool responds(const std::string& port, int baud) {
  im920s::Module::Options o;
  o.baud = baud;
  im920s::Module m(port, o);
  return m.command("RDVR").find("IM920s") != std::string::npos;
}

static char sbrt_code(int baud) {
  switch (baud) {
    case 9600: return '3';
    case 19200: return '4';
    case 38400: return '5';
    case 57600: return '6';
    case 115200: return '7';
  }
  throw im920s::Error("SBRT で設定できない速度: " + std::to_string(baud));
}

// モジュールの今の速度を探して want に合わせる
static void ensure_baud(const std::string& port, int want) {
  if (responds(port, want)) return;
  for (int b : {19200, 115200, 57600, 38400, 9600}) {
    if (b == want || !responds(port, b)) continue;
    {
      im920s::Module::Options o;
      o.baud = b;
      im920s::Module m(port, o);
      m.command(std::string("SBRT ") + sbrt_code(want), 300ms);  // 応答は新しい速度で返るので読めなくてよい
    }
    if (responds(port, want)) {
      std::cout << port << ": " << b << " -> " << want << " bps\n";
      return;
    }
  }
  throw im920s::Error(port + " が応答しません (USB を抜き差しすると 19200bps に戻ります)");
}

int main(int argc, char** argv) {
  milliseconds interval{0};
  bool unicast = false, fast = false;
  milliseconds period{200};
  bool turbo = false, period_set = false;
  std::vector<std::string> ports;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-i" && i + 1 < argc) interval = milliseconds(std::atoi(argv[++i]));
    else if (a == "-u") unicast = true;
    else if (a == "-f") fast = true;
    else if (a == "-t") turbo = true;
    else if (a == "-b" && i + 1 < argc) g_baud = std::atoi(argv[++i]);
    else if (a == "-p" && i + 1 < argc) period = milliseconds(std::max(20, std::atoi(argv[++i]))), period_set = true;
    else if (a == "-s" && i + 1 < argc) g_size = std::clamp(std::atoi(argv[++i]), 8, 32);
    else ports.push_back(a);
  }
  if (ports.empty() || ports.size() > 2) {
    std::cerr << "使い方: " << argv[0] << " [-i ms] [-s bytes] [-u] [-t] [-f [-p ms]] [-b baud] PORT_A [PORT_B]\n";
    return 2;
  }

  std::signal(SIGINT, [](int) { g_stop = true; });
  std::signal(SIGTERM, [](int) { g_stop = true; });

  // -t: 115200bps に一時的に切り替えて高速モード (終了時に 19200 に戻す)
  if (turbo) {
    g_baud = 115200;
    fast = true;
    if (!period_set) period = 130ms;  // 実測で取りこぼしなく最速だった周期
  }

  int code = 0;
  try {
    for (auto& p : ports) ensure_baud(p, g_baud);  // 前回 115200 のまま残っていても合わせる
    if (ports.size() == 2 && fast) run_fast(ports[0], ports[1], period);
    else if (ports.size() == 2) run_pair(ports[0], ports[1], interval, unicast);
    else run_single(ports[0], interval, unicast);
  } catch (const std::exception& e) {
    std::cerr << "エラー: " << e.what() << "\n";
    code = 1;
  }
  if (turbo) {
    try {
      for (auto& p : ports) ensure_baud(p, 19200);
      std::cout << "通信速度を 19200bps に戻しました\n";
    } catch (const std::exception& e) {
      std::cerr << "19200bps に戻せませんでした: " << e.what() << "\n";
      code = 1;
    }
  }
  return code;
}
