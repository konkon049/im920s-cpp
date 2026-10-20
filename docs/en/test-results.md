# Test results

Environment: IM920s Ver.01.21 × 2 (via IM920-USB), Ubuntu, modules close together (RSSI about 0xEB)

```bash
make hwtest A=/dev/ttyUSB0 B=/dev/ttyUSB1 N=300
```

## Result: all 152 checks passed

### Unit tests (no hardware, 52 checks)

| Test | What it checks |
|---|---|
| `to_hex` / `parse_hex` | Hex conversion; detects wrong length and invalid characters |
| `parse_packet` | Valid received lines, 32 bytes, round trip of every byte value |
| `parse_packet` (invalid input) | 19 kinds of non-data lines are not mistaken for received data: `OK` / `NG` / ID / version / `RPRM` output (contains a colon) / missing digits / wrong separators, etc. |
| `Module` | Throws for a missing port and for a file that isn't a serial port |

### Hardware tests

| Test | Result |
|---|---|
| Refuses to open the same port twice | OK |
| Doesn't mistake a late response after a timeout for the next command's response | OK |
| Reading settings (ID / version / node / channel / RSSI / group) | OK |
| Same command 100 times in a row | 100/100 match |
| Unknown command returns NG and the module keeps working | OK |
| Payload length check (0 and 33 bytes throw without sending) | OK |
| broadcast A→B / B→A text | OK |
| broadcast every length from 1 to 32 bytes | 32/32 match |
| broadcast every byte value 0x00–0xFF (including CR/LF/comma/colon) | 8/8 packets match |
| unicast A→B / B→A (including binary) | OK |
| unicast to a node that doesn't exist | NG after about 6 s, module keeps working |
| receive timeout | Returns after the requested time |
| Data received while a command runs is not lost | OK |
| Multiple queued packets come out in order | OK |
| clear empties the receive queue | OK |
| Stress A→B (300 packets) | Sent 300/300, received and matched 300, mismatched 0, about 4.1 packets/s |
| Stress B→A (300 packets) | Sent 300/300, received and matched 300, mismatched 0, about 4.0 packets/s |
| Both modules broadcast at the same moment (20 times) | 1 arrived on each side (expected), 0 corrupted |
| Both modules unicast at the same moment (20 times) | 20/20 arrived, 0 corrupted |
| No unparsed lines left over | OK |

### Other checks

- AddressSanitizer / UndefinedBehaviorSanitizer: no problems
- `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`: no warnings in the library itself

## Bug found and fixed during testing

**Mixing up a late response after a timeout**

When the response timeout was set short and `unicast` timed out, the `NG` that arrived about 6 s later
was returned as the response to the next command (for example `id()`).
Fixed: after a timeout, the library now waits for the late response and discards it before sending the next command.

## Not yet tested

- Unplugging USB while in use (the code does throw a "disconnected" exception)
- Communication over long distances or through obstacles
