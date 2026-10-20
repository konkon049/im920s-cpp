# IM920s commands and behavior observed on real hardware

A command is "4-letter command + (space + argument) + CR+LF".
Only read and send commands are listed here. Commands that change settings (`STxx`, `ENWR`, etc.)
write to flash memory, so this repository does not use them.

## Reading

| Command | Description | Example response |
|---|---|---|
| `RDID` | Unique ID | `000023B8` |
| `RDNN` | Node number | `0002` |
| `RDGN` | Group number | `000023B7` |
| `RDCH` | Channel | `16` |
| `RDRS` | Current signal strength (RSSI) | `98` |
| `RDPO` | Transmit power | `2` |
| `RDVR` | Version | `IM920s Ver.01.21` |
| `RPRM` | All parameters | `ID:000023B8` `STNN:0002` … (multiple lines) |

Unknown commands return `NG`.

## Sending

| Command | Description | Response |
|---|---|---|
| `TXDA 48454C4C4F` | Send "HELLO" to everyone (broadcast) | `OK` |
| `TXDU 0001,4849` | Send "HI" to node 0001, with acknowledgment | `OK` when acknowledged |

Data is written in hex (with the default DCIO setting).

## Receiving

When data arrives, the module outputs a line like this:

```
00,0002,EE:48,45,4C,4C,4F
│  │    │  └ data (hex, comma-separated)
│  │    └ signal strength (RSSI, higher is stronger)
│  └ sender's node number
└ (fixed)
```

The format is the same for data sent with `TXDA` and `TXDU`.

## Behavior observed on real hardware

| Behavior | What to do |
|---|---|
| Sending 33+ bytes returns `OK` but **silently truncates to 32 bytes** | Send at most 32 bytes at a time |
| Malformed hex (odd number of digits, non-hex characters) still returns `OK` and sends corrupted data | Make sure the hex is correct before sending |
| Sends less than 200 ms apart return `NG` about 1 in 10 times | Leave at least 200 ms / retry on `NG` |
| `TXDA` responds in about 20 ms | — |
| `TXDU` (receiver present) responds in about 0.05–0.6 s | — |
| `TXDU` to a node that doesn't exist returns `NG` **after about 6 s**, and no other commands are accepted meanwhile | Wait about 8 s. After a timeout the response still arrives later, so don't mistake it for the next response |
| If both modules `TXDA` at the same moment, neither receives the other (only about 1 in 20 arrived) | Use `TXDU` for data that must arrive |
| If both modules `TXDU` at the same moment, the modules retry automatically and everything arrives (takes 0.5–2 s) | — |
| Lowercase commands are accepted | — |
| Received-data lines can appear while waiting for a command response | Tell received lines and responses apart when reading |
