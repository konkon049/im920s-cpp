// im920s.hpp - IM920s (Interplan 920MHz 無線モジュール) を Linux PC から使うためのヘッダオンリーライブラリ
//
// 使い方:
//   #include "im920s.hpp"
//   im920s::Module m("/dev/ttyUSB0");          // 19200bps で開く
//   m.broadcast("HELLO");                       // 全員宛て (TXDA)
//   m.unicast(0x0001, "HI");                    // ノード0001宛て (TXDU)
//   if (auto p = m.receive(1000ms)) std::cout << p->text();
//
// 方針:
//   - モジュールの設定を書き換えるコマンド (STxx / ENWR 等) は扱わない (読み出しのみ)
//   - 送信データは 1〜32 バイト。範囲外はモジュールが黙って切り捨てるので送信前に弾く
//   - 送信間隔が 200ms 未満だとモジュールが NG を返すことがあるので、自動で間隔をあけて再送する
//   - コマンド応答待ちの間に届いた受信データは捨てずにキューへ積む
#pragma once

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace im920s {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr std::size_t kMaxPayload = 32;

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class Status { Ok, Ng, Timeout };

inline const char* to_string(Status s) {
  switch (s) {
    case Status::Ok: return "OK";
    case Status::Ng: return "NG";
    case Status::Timeout: return "TIMEOUT";
  }
  return "?";
}

struct Packet {
  uint16_t node = 0;           // 送信元ノード番号
  uint8_t rssi = 0;            // 受信強度 (大きいほど強い)
  std::vector<uint8_t> data;   // 受信データ

  std::string text() const { return std::string(data.begin(), data.end()); }
};

// ---- 文字列ユーティリティ (ハードウェア不要, テスト可能) ----

inline std::string to_hex(const uint8_t* p, std::size_t n) {
  static const char* d = "0123456789ABCDEF";
  std::string h;
  h.reserve(n * 2);
  for (std::size_t i = 0; i < n; ++i) {
    h += d[p[i] >> 4];
    h += d[p[i] & 0xF];
  }
  return h;
}

inline int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// 固定桁数の16進数を読む。失敗したら nullopt
inline std::optional<uint32_t> parse_hex(const std::string& s, std::size_t digits) {
  if (s.size() != digits) return std::nullopt;
  uint32_t v = 0;
  for (char c : s) {
    int d = hex_digit(c);
    if (d < 0) return std::nullopt;
    v = v * 16 + d;
  }
  return v;
}

// 受信行 "00,0002,EE:41,42,43" を Packet に変換。受信行でなければ nullopt
inline std::optional<Packet> parse_packet(const std::string& line) {
  auto colon = line.find(':');
  if (colon == std::string::npos) return std::nullopt;
  std::string head = line.substr(0, colon), body = line.substr(colon + 1);

  // head: "xx,nnnn,rr"
  if (head.size() != 10 || head[2] != ',' || head[7] != ',') return std::nullopt;
  auto dummy = parse_hex(head.substr(0, 2), 2);
  auto node = parse_hex(head.substr(3, 4), 4);
  auto rssi = parse_hex(head.substr(8, 2), 2);
  if (!dummy || !node || !rssi) return std::nullopt;

  // body: "41,42,43" (1〜32 バイト)
  if (body.empty() || (body.size() + 1) % 3 != 0) return std::nullopt;
  Packet p;
  p.node = static_cast<uint16_t>(*node);
  p.rssi = static_cast<uint8_t>(*rssi);
  for (std::size_t i = 0; i < body.size(); i += 3) {
    if (i + 2 < body.size() && body[i + 2] != ',') return std::nullopt;
    auto b = parse_hex(body.substr(i, 2), 2);
    if (!b) return std::nullopt;
    p.data.push_back(static_cast<uint8_t>(*b));
  }
  if (p.data.size() > kMaxPayload) return std::nullopt;
  return p;
}

// ---- モジュール本体 ----

class Module {
 public:
  struct Options {
    int baud = 19200;
    milliseconds command_timeout{500};   // 通常コマンドの応答待ち
    milliseconds unicast_timeout{8000};  // TXDU の応答待ち (相手不在だと約6秒後に NG)
    milliseconds send_interval{200};     // 送信間の最小間隔
    int send_retries = 3;                // TXDA が NG のときの再送回数
  };

  explicit Module(const std::string& path) : Module(path, Options{}) {}

  Module(const std::string& path, Options opt) : path_(path), opt_(opt) {
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) throw Error(path + " を開けません: " + std::strerror(errno));
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      ::close(fd_);
      throw Error(path + " は他のプログラムが使用中です");
    }
    termios tio{};
    if (tcgetattr(fd_, &tio) != 0) {
      ::close(fd_);
      throw Error(path + " はシリアルポートではありません");
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    speed_t sp = to_speed(opt.baud);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tcsetattr(fd_, TCSANOW, &tio);
    tcflush(fd_, TCIOFLUSH);
  }

  ~Module() {
    if (fd_ >= 0) ::close(fd_);
  }
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;

  const std::string& path() const { return path_; }

  // コマンドを送って応答1行を返す。タイムアウトなら空文字列
  std::string command(const std::string& cmd) { return command(cmd, opt_.command_timeout); }

  std::string command(const std::string& cmd, milliseconds timeout) {
    pump(milliseconds(0));  // 送信前に溜まっている受信データをキューへ
    discard_late_response();
    write_all(cmd + "\r\n");
    auto deadline = Clock::now() + timeout;
    while (auto line = read_line(deadline)) {
      if (auto p = parse_packet(*line)) {
        rx_.push_back(std::move(*p));  // 応答待ち中に届いた受信データ
        continue;
      }
      return *line;
    }
    // 応答は遅れて届く可能性がある (例: 相手不在の TXDU は約6秒後に NG)。
    // 次のコマンドの応答と取り違えないよう、届いたら捨てる
    late_deadline_ = Clock::now() + kLateResponseWait;
    return "";
  }

  // ---- 読み出し ----
  std::string id() { return command("RDID"); }
  std::string version() { return command("RDVR"); }
  std::optional<uint16_t> node() { return hex_value(command("RDNN"), 4); }
  std::optional<uint32_t> group() { return hex_value(command("RDGN"), 8); }
  std::optional<int> channel() {
    std::string r = command("RDCH");
    if (r.empty() || r.size() > 2 || r.find_first_not_of("0123456789") != std::string::npos)
      return std::nullopt;
    return std::stoi(r);
  }
  std::optional<uint8_t> rssi() {
    auto v = hex_value(command("RDRS"), 2);
    if (!v) return std::nullopt;
    return static_cast<uint8_t>(*v);
  }

  // ---- 送信 ----
  // 全員宛て (TXDA)。NG なら間隔をあけて再送する
  Status broadcast(const std::vector<uint8_t>& data) {
    check_payload(data);
    Status st = Status::Ng;
    for (int i = 0; i <= opt_.send_retries; ++i) {
      st = transmit("TXDA " + to_hex(data.data(), data.size()), opt_.command_timeout);
      if (st != Status::Ng) break;
    }
    return st;
  }
  Status broadcast(const std::string& s) { return broadcast(std::vector<uint8_t>(s.begin(), s.end())); }

  // 指定ノード宛て (TXDU)。相手から受信確認が返れば Ok、相手不在なら約6秒後に Ng
  Status unicast(uint16_t node, const std::vector<uint8_t>& data) {
    check_payload(data);
    uint8_t n[2] = {static_cast<uint8_t>(node >> 8), static_cast<uint8_t>(node)};
    return transmit("TXDU " + to_hex(n, 2) + "," + to_hex(data.data(), data.size()),
                    opt_.unicast_timeout);
  }
  Status unicast(uint16_t node, const std::string& s) {
    return unicast(node, std::vector<uint8_t>(s.begin(), s.end()));
  }

  // ---- 受信 ----
  // timeout まで待って受信データを1つ返す。なければ nullopt
  std::optional<Packet> receive(milliseconds timeout) {
    if (rx_.empty()) pump(timeout, true);
    if (rx_.empty()) return std::nullopt;
    Packet p = std::move(rx_.front());
    rx_.pop_front();
    return p;
  }

  // 受信キューと OS の受信バッファを空にする
  void clear() {
    pump(milliseconds(0));
    rx_.clear();
    unknown_.clear();
  }

  // 受信データとして解釈できなかった行 (デバッグ用)
  const std::vector<std::string>& unknown_lines() const { return unknown_; }

 private:
  static speed_t to_speed(int baud) {
    switch (baud) {
      case 1200: return B1200;
      case 2400: return B2400;
      case 4800: return B4800;
      case 9600: return B9600;
      case 19200: return B19200;
      case 38400: return B38400;
      case 57600: return B57600;
      case 115200: return B115200;
      case 230400: return B230400;
      case 460800: return B460800;
    }
    throw Error("未対応のボーレート: " + std::to_string(baud));
  }

  static std::optional<uint32_t> hex_value(const std::string& s, std::size_t digits) {
    return parse_hex(s, digits);
  }

  static void check_payload(const std::vector<uint8_t>& data) {
    if (data.empty() || data.size() > kMaxPayload)
      throw std::invalid_argument("送信データは1〜32バイトにしてください (" +
                                  std::to_string(data.size()) + "バイト)");
  }

  Status transmit(const std::string& cmd, milliseconds timeout) {
    auto wait = last_send_ + opt_.send_interval - Clock::now();
    if (wait > Clock::duration::zero()) std::this_thread::sleep_for(wait);
    std::string r = command(cmd, timeout);
    last_send_ = Clock::now();
    if (r == "OK") return Status::Ok;
    if (r.empty()) return Status::Timeout;
    return Status::Ng;
  }

  void write_all(const std::string& s) {
    std::size_t off = 0;
    while (off < s.size()) {
      ssize_t n = ::write(fd_, s.data() + off, s.size() - off);
      if (n > 0) {
        off += n;
      } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
        throw Error(path_ + " への書き込みに失敗しました: " + std::strerror(errno));
      } else {
        pollfd p{fd_, POLLOUT, 0};
        ::poll(&p, 1, 100);
      }
    }
    tcdrain(fd_);
  }

  // deadline までに1行 (CRLF 区切り) 読む
  std::optional<std::string> read_line(Clock::time_point deadline) {
    for (;;) {
      auto pos = buf_.find("\r\n");
      if (pos != std::string::npos) {
        std::string line = buf_.substr(0, pos);
        buf_.erase(0, pos + 2);
        if (line.empty()) continue;
        return line;
      }
      auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now()).count();
      if (left < 0) left = 0;
      pollfd p{fd_, POLLIN, 0};
      int r = ::poll(&p, 1, static_cast<int>(left));
      if (r < 0 && errno != EINTR) throw Error(path_ + " の読み込みに失敗しました");
      if (r > 0) {
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) throw Error(path_ + " が切断されました");
        char c[256];
        ssize_t n = ::read(fd_, c, sizeof(c));
        if (n > 0) buf_.append(c, n);
        else if (n < 0 && errno != EAGAIN && errno != EINTR) throw Error(path_ + " の読み込みに失敗しました");
      } else if (left == 0) {
        return std::nullopt;
      }
    }
  }

  // タイムアウトしたコマンドの応答が遅れて届くのを待って捨てる
  void discard_late_response() {
    if (!late_deadline_) return;
    while (auto line = read_line(*late_deadline_)) {
      if (auto p = parse_packet(*line)) {
        rx_.push_back(std::move(*p));
      } else {
        break;  // 遅れて来た応答
      }
    }
    late_deadline_.reset();
  }

  // 受信行をキューに積む。stop_on_packet なら1件積んだ時点で戻る
  void pump(milliseconds timeout, bool stop_on_packet = false) {
    auto deadline = Clock::now() + timeout;
    while (auto line = read_line(deadline)) {
      if (auto p = parse_packet(*line)) {
        rx_.push_back(std::move(*p));
        if (stop_on_packet) return;
      } else if (late_deadline_) {
        late_deadline_.reset();  // 遅れて来た応答なので捨てる
      } else {
        unknown_.push_back(*line);
      }
    }
  }

  std::string path_;
  Options opt_;
  int fd_ = -1;
  std::string buf_;
  std::deque<Packet> rx_;
  std::vector<std::string> unknown_;
  Clock::time_point last_send_{};
  std::optional<Clock::time_point> late_deadline_;

  static constexpr milliseconds kLateResponseWait{10000};
};

}  // namespace im920s
