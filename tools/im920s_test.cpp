// IM920s 2台間の通信テスト (PCにUSB接続した2台用, Linux)
//
// ビルド:
//   make  (build/im920s_test ができる)
// 使い方:
//   ./build/im920s_test                                  # /dev/ttyUSB0 <-> /dev/ttyUSB1, 19200bps, 5回
//   ./build/im920s_test /dev/ttyUSB0 /dev/ttyUSB1 19200 10
//
// 手順:
//   1. 各モジュールに RDID / RDNN / RDCH / RDRS を送り、応答を確認
//   2. A から TXDA で送信 → B で受信できるか確認
//   3. B から TXDA で送信 → A で受信できるか確認
#include <fcntl.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono;

class Serial {
 public:
  Serial(const std::string& path, int baud) : path_(path) {
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) throw std::runtime_error(path + " を開けません");
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0)
      throw std::runtime_error(path + " は他のプログラム(minitermなど)が使用中です");

    termios tio{};
    tcgetattr(fd_, &tio);
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    speed_t sp = to_speed(baud);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tcsetattr(fd_, TCSANOW, &tio);
    tcflush(fd_, TCIOFLUSH);
  }
  ~Serial() {
    if (fd_ >= 0) ::close(fd_);
  }
  const std::string& path() const { return path_; }

  void flush_input() { tcflush(fd_, TCIFLUSH); }

  void write_line(const std::string& s) {
    std::string out = s + "\r\n";
    if (::write(fd_, out.data(), out.size()) != static_cast<ssize_t>(out.size()))
      throw std::runtime_error(path_ + " への書き込みに失敗しました");
    tcdrain(fd_);
  }

  // timeout 内に受信した文字列を返す (行末 CRLF を受け取ったら早めに戻る)
  std::string read_for(milliseconds timeout) {
    std::string buf;
    auto end = steady_clock::now() + timeout;
    while (steady_clock::now() < end) {
      char c[256];
      ssize_t n = ::read(fd_, c, sizeof(c));
      if (n > 0) {
        buf.append(c, n);
        if (buf.size() >= 2 && buf.compare(buf.size() - 2, 2, "\r\n") == 0) break;
      } else {
        std::this_thread::sleep_for(milliseconds(5));
      }
    }
    return buf;
  }

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
    throw std::runtime_error("未対応のボーレート: " + std::to_string(baud));
  }

  std::string path_;
  int fd_ = -1;
};

static std::string trim(const std::string& s) {
  auto b = s.find_first_not_of(" \r\n");
  if (b == std::string::npos) return "";
  return s.substr(b, s.find_last_not_of(" \r\n") - b + 1);
}

static std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream is(s);
  for (std::string l; std::getline(is, l);)
    if (!trim(l).empty()) v.push_back(trim(l));
  return v;
}

// コマンドを送り、応答を返す
static std::string cmd(Serial& port, const std::string& text, milliseconds wait = milliseconds(300)) {
  port.flush_input();
  port.write_line(text);
  return trim(port.read_for(wait));
}

static std::string to_hex(const std::string& s) {
  static const char* d = "0123456789ABCDEF";
  std::string h;
  for (unsigned char c : s) {
    h += d[c >> 4];
    h += d[c & 0xF];
  }
  return h;
}

struct RxData {
  std::string payload;
  int rssi;
};

// 受信行 '00,0001,C8:41,42,43' をデータ部の文字列と RSSI に変換
static std::optional<RxData> decode_rx(const std::string& line) {
  auto colon = line.find(':');
  if (colon == std::string::npos) return std::nullopt;
  std::string head = line.substr(0, colon), data = line.substr(colon + 1);
  try {
    RxData r;
    r.rssi = std::stoi(head.substr(head.rfind(',') + 1), nullptr, 16);
    std::istringstream is(data);
    for (std::string h; std::getline(is, h, ',');)
      r.payload += static_cast<char>(std::stoi(h, nullptr, 16));
    return r;
  } catch (...) {
    return std::nullopt;
  }
}

static bool info(const char* name, Serial& port) {
  std::printf("--- %s (%s) ---\n", name, port.path().c_str());
  bool ok = true;
  const char* cmds[][2] = {{"RDID", "固有ID"}, {"RDNN", "ノード番号"}, {"RDCH", "チャンネル"}, {"RDRS", "RSSI"}};
  for (auto& c : cmds) {
    std::string r = cmd(port, c[0]);
    std::printf("  %-5s %s: %s\n", c[0], c[1], r.empty() ? "(応答なし)" : r.c_str());
    if (std::string(c[0]) == "RDID" && r.empty()) ok = false;
  }
  return ok;
}

static int one_way(const char* tx_name, Serial& tx, const char* rx_name, Serial& rx, int count) {
  std::printf("\n=== %s -> %s 送信テスト (%d回) ===\n", tx_name, rx_name, count);
  int success = 0;
  for (int i = 0; i < count; ++i) {
    char msg[16];
    std::snprintf(msg, sizeof(msg), "TEST%03d", i);
    // 送信間隔が短いとモジュールが NG を返すので間隔をあけ、NG なら再送する
    std::this_thread::sleep_for(milliseconds(300));
    rx.flush_input();
    std::string res;
    for (int retry = 0; retry < 3; ++retry) {
      // 初期設定(DCIO)では TXDA のデータは16進表記で渡す
      res = cmd(tx, "TXDA " + to_hex(msg), milliseconds(200));
      if (res != "NG") break;
      std::this_thread::sleep_for(milliseconds(300));
    }
    auto got = lines(rx.read_for(milliseconds(1000)));

    std::optional<RxData> hit;
    for (auto& l : got) {
      auto d = decode_rx(l);
      if (d && d->payload == msg) hit = d;
    }
    if (hit) {
      ++success;
      std::printf("  [%d] 送信:%s 応答:'%s' -> 受信OK RSSI=0x%02X\n", i, msg, res.c_str(), hit->rssi);
    } else {
      std::string all;
      for (auto& l : got) all += "'" + l + "' ";
      std::printf("  [%d] 送信:%s 応答:'%s' -> 受信NG 受信内容:[%s]\n", i, msg, res.c_str(), all.c_str());
    }
  }
  std::printf("  成功 %d/%d\n", success, count);
  return success;
}

int main(int argc, char** argv) {
  std::string port_a = argc > 1 ? argv[1] : "/dev/ttyUSB0";
  std::string port_b = argc > 2 ? argv[2] : "/dev/ttyUSB1";
  int baud = argc > 3 ? std::atoi(argv[3]) : 19200;
  int count = argc > 4 ? std::atoi(argv[4]) : 5;

  try {
    Serial a(port_a, baud), b(port_b, baud);
    std::this_thread::sleep_for(milliseconds(500));

    bool ok_a = info("A", a);
    bool ok_b = info("B", b);
    if (!ok_a || !ok_b) {
      std::printf("\n応答のないモジュールがあります。アダプタ基板のコネクタの差し込みとボーレートを確認してください。\n");
      return 1;
    }

    int s1 = one_way("A", a, "B", b, count);
    int s2 = one_way("B", b, "A", a, count);
    return (s1 == count && s2 == count) ? 0 : 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "エラー: %s\n", e.what());
    return 1;
  }
}
