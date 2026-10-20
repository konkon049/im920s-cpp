# im920s-cpp

[日本語](README.md) | English

A header-only C++ library for using the **Interplan IM920s** 920 MHz wireless module from a Linux PC over USB serial.

- Single header (`include/im920s.hpp`), C++17, no external dependencies
- Broadcast send (`TXDA`), unicast send (`TXDU`), receive, and reading module settings
- Does not include commands that change module settings (read-only)
- 152 test checks pass on two real modules

Tested with: IM920s Ver.01.21 + IM920-USB board × 2, Ubuntu

> **Note**
> Most of this code and documentation was written with the help of an AI (Claude).
> It has been tested only on the hardware listed above, so it may not work on other
> firmware versions, boards, or environments, and it may contain bugs that the tests did
> not catch. It is provided as-is with no warranty (see [LICENSE](LICENSE)).
> Please test it yourself before using it in anything important.
> Bug reports and pull requests are welcome.

## Layout

```
include/im920s.hpp       The library
tests/test_im920s.cpp    Tests (unit tests + hardware tests)
examples/example.cpp     Usage example
tools/                   Standalone test programs that don't use the library
  im920s_test.py           Python communication test
  im920s_test.cpp          C++ communication test
  im920s_min.cpp           Minimal communication test (~50 lines)
docs/                    Documentation (Japanese)
docs/en/                 Documentation (English)
  setup.md                 Hardware setup and troubleshooting
  commands.md              IM920s commands and behavior observed on real hardware
  test-results.md          Test results
Makefile
```

## Quick start

```bash
make                     # build everything into build/
make test                # unit tests (no hardware needed)
make hwtest              # hardware tests with two modules (/dev/ttyUSB0 and /dev/ttyUSB1)
make hwtest N=300        # set the number of packets for the stress test
```

To check that a module is connected:

```bash
python3 -m serial.tools.miniterm -e --eol CRLF /dev/ttyUSB0 19200
# Type RDID and press Enter -> an 8-digit ID means it works (quit with Ctrl+])
```

If there is no response, see [docs/en/setup.md](docs/en/setup.md).

## Usage

```cpp
#include "im920s.hpp"
using namespace std::chrono_literals;

im920s::Module m("/dev/ttyUSB0");      // open at 19200 bps

m.broadcast("HELLO");                  // to everyone (TXDA)
m.unicast(0x0001, "HI");               // to node 0001, with acknowledgment (TXDU)

if (auto p = m.receive(1000ms))        // receive (wait up to 1 s)
  std::cout << p->node << ": " << p->text() << "\n";

std::cout << m.id() << " " << m.channel().value_or(-1) << "\n";  // read settings
```

| Function | Description |
|---|---|
| `broadcast(data)` | Send to everyone. Returns `Status::Ok` / `Ng` / `Timeout` |
| `unicast(node, data)` | Send to one node. Returns `Ok` when the receiver acknowledges |
| `receive(timeout)` | Returns one received `Packet` (`node`, `rssi`, `data`, `text()`), or `nullopt` |
| `id()` `node()` `channel()` `rssi()` `version()` `group()` | Read settings |
| `command(cmd)` | Send any command and return one response line |
| `clear()` | Empty the receive queue |

- Payload must be 1–32 bytes; otherwise `std::invalid_argument` is thrown
- Failing to open the port, or the port being used by another program, throws `im920s::Error`
- Timeouts and retry counts can be changed with `im920s::Module::Options`

## IM920s behavior the library handles

| Module behavior | What the library does |
|---|---|
| With the default setting (DCIO), send data must be written in hex | Converts data to hex automatically |
| Sending 33+ bytes returns `OK` but silently truncates to 32 bytes | Throws before sending |
| Sends less than 200 ms apart return `NG` about 1 in 10 times | Waits between sends and retries on `NG` |
| `TXDU` to a node that doesn't exist returns `NG` after about 6 s | Waits up to 8 s; discards late responses after a timeout |
| Received data can arrive while waiting for a command response | Keeps it in the receive queue instead of dropping it |
| If both modules `TXDA` at the same moment, neither receives the other | Hardware behavior. Use `unicast` for data that must arrive |

See [docs/en/commands.md](docs/en/commands.md) for details and [docs/en/test-results.md](docs/en/test-results.md) for test results.

## License

MIT License
