// IM920s 最低限の通信テスト: A から "HELLO" を送り、B で受信した行を表示する
// ビルド: make  (build/im920s_min ができる)
// 実行:   ./build/im920s_min /dev/ttyUSB0 /dev/ttyUSB1
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <string>

int open_port(const char* path) {
  int fd = open(path, O_RDWR | O_NOCTTY);
  termios tio{};
  tcgetattr(fd, &tio);
  cfmakeraw(&tio);
  cfsetspeed(&tio, B19200);  // IM920s 初期値 19200bps
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 10;      // read は最大 1 秒待つ
  tcsetattr(fd, TCSANOW, &tio);
  tcflush(fd, TCIOFLUSH);
  return fd;
}

void send(int fd, const std::string& s) {
  std::string line = s + "\r\n";
  if (write(fd, line.data(), line.size()) < 0) perror("write");
}

std::string recv_line(int fd) {
  std::string buf;
  char c;
  while (read(fd, &c, 1) == 1 && c != '\n')
    if (c != '\r') buf += c;
  return buf;
}

int main(int argc, char** argv) {
  int a = open_port(argc > 1 ? argv[1] : "/dev/ttyUSB0");
  int b = open_port(argc > 2 ? argv[2] : "/dev/ttyUSB1");

  send(a, "TXDA 48454C4C4F");  // "HELLO" を16進数で送信
  std::printf("A 応答: %s\n", recv_line(a).c_str());  // OK なら送信成功
  std::printf("B 受信: %s\n", recv_line(b).c_str());  // 例: 00,0002,EE:48,45,4C,4C,4F

  close(a);
  close(b);
}
