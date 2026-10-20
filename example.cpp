// im920s.hpp の使用例: 送信して、相手からの受信を表示する
// ビルド: g++ -std=c++17 -o example example.cpp
// 実行:   ./example /dev/ttyUSB0
#include <iostream>

#include "im920s.hpp"

int main(int argc, char** argv) {
  using namespace std::chrono_literals;
  try {
    im920s::Module m(argc > 1 ? argv[1] : "/dev/ttyUSB0");
    std::cout << "ID: " << m.id() << "  ノード: " << std::hex << m.node().value_or(0) << std::dec << "\n";

    // 全員宛て
    std::cout << "broadcast: " << im920s::to_string(m.broadcast("HELLO")) << "\n";

    // 10秒間、受信したデータを表示
    auto end = im920s::Clock::now() + 10s;
    while (im920s::Clock::now() < end) {
      if (auto p = m.receive(500ms))
        std::cout << "受信 node=" << std::hex << p->node << " rssi=" << int(p->rssi) << std::dec << " : "
                  << p->text() << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "エラー: " << e.what() << "\n";
    return 1;
  }
}
