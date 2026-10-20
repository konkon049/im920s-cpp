# Hardware setup and troubleshooting

## Setup

```
PC ─USB─ IM920-USB (USB interface board) ─ adapter board ─ IM920s
```

| Item | Details |
|---|---|
| Wireless module | Interplan IM920s (tested with Ver.01.21) |
| USB board | IM920-USB (older model) / IM920-USB2 |
| Serial settings | 19200 bps, 8 data bits, no parity, 1 stop bit, CR+LF line endings |
| PC | Linux (tested on Ubuntu) |

Depending on when it was made, the USB board uses a CP2102N or an FT231X chip.
Both work on Linux without extra drivers and appear as `/dev/ttyUSB0`, `/dev/ttyUSB1`, and so on.

```bash
ls -l /dev/serial/by-id/     # see which port belongs to which board
```

Your user must be in the `dialout` group to use `/dev/ttyUSB*`.

## Assembly

The IM920s sits on an adapter board, which plugs into the **small white board-to-board connector**
on the USB board (top right of the "U2" outline).

- Line up the connectors and **press them straight together all the way** (you should feel them seat)
- Then fix it with the screws. The screws only hold it in place; the electrical connection is only through the connector
- The 2-row pin header (U1) on the board is for the older IM920 and is not used with the IM920s

## Checking the connection

```bash
python3 -m serial.tools.miniterm -e --eol CRLF /dev/ttyUSB0 19200
```

Type `RDID` and press Enter. If an 8-digit ID comes back (for example `000023B8`), it works.
Quit with `Ctrl+]`.

## Troubleshooting

### No response at any baud rate

The adapter board's connector is most likely not pushed in all the way.
If it is screwed down on top of white spacers, it can look secure but not be electrically connected.
Remove it and press the connector in again.

Things to keep in mind:
- The PC only sees the USB board (the USB chip), so `/dev/ttyUSB*` appears even when the module is not connected
- If changing control signals (DTR/RTS), flow control, and baud rate makes no difference, the problem is hardware, not software

### It worked a moment ago but now there's no response / "in use" error

Another program such as miniterm still has the port open and is taking the responses.

```bash
fuser -v /dev/ttyUSB0 /dev/ttyUSB1    # see which process is using the port
```

Quit miniterm with `Ctrl+]`.

### Send returns OK but the received data is garbled (e.g. `DE,DD,00`)

Text is being passed to `TXDA` as-is. With the default setting (DCIO), the data must be in hex.

```
TXDA HELLO          <- wrong (garbled)
TXDA 48454C4C4F     <- right ("HELLO")
```

### Sends sometimes return NG

The sends are too close together. Leave at least 200 ms between them. The library (`im920s.hpp`) does this and retries automatically.

### Nothing is received

- Check that both modules are on the same channel (`RDCH`)
- If both modules send with `TXDA` at the same moment, neither receives the other. Use `TXDU` for data that must arrive
