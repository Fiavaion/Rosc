# RoscCam protocol, version 1

How the RoscCam app and the Rosc editor plugin talk to each other (ADR-014). Both ends follow this
file; a change to it raises the version.

## Finding the editor
While its RoscCam server runs, the editor sends a UDP broadcast to port **47820** once a second.
The body is UTF-8 JSON:

```json
{ "rosccam": 1, "name": "EDIT-PC", "port": 47821 }
```

`rosccam` is the protocol version, `name` the computer's name, `port` the TCP port to connect to.
The app lists what it hears, and can also be given an address by hand.

## Messages
One TCP connection, to port **47821**, carries everything. Each message is:

| Bytes | What |
|---|---|
| 4 | Length of what follows (type and body), unsigned, little-endian |
| 1 | Type |
| Length − 1 | Body |

A message longer than 8 MB closes the connection. All JSON is UTF-8. Numbers in binary bodies
are little-endian.

| Type | Name | From | Body |
|---|---|---|---|
| 0x01 | Hello | app | JSON `{ "protocol": 1, "device": "Pixel 8", "app": "0.1.0" }` |
| 0x02 | Welcome | editor | JSON `{ "protocol": 1, "editor": "Rosc 0.1.0" }`. The editor shows a pairing code |
| 0x03 | Pair | app | JSON `{ "code": "4821" }`, the code the user read in the editor |
| 0x04 | Paired | editor | JSON `{ "paired": true }`, or `{ "paired": false, "reason": "..." }` |
| 0x10 | Picture | editor | A JPEG of the camera's view. Only to a paired app |
| 0x20 | Motion | app | Binary, 40 bytes, below. Ignored until paired |
| 0x30 | Control | app | JSON `{ "action": "...", ... }`, below. Ignored until paired |
| 0x31 | State | editor | JSON, below. Only to a paired app |
| 0x32 | Result | editor | JSON `{ "action": "...", "ok": true, "message": "..." }`, the answer to a control |

A Hello with a protocol version the editor does not speak is answered with a Welcome of its own
version, and the connection is closed; the app says which side to update.

### Motion (0x20)
| Offset | Type | What |
|---|---|---|
| 0 | f64 | The device's clock, in seconds |
| 8 | f32 × 4 | The device's rotation as a quaternion w, x, y, z |
| 24 | f32 × 3 | The device's position in metres, x, y, z |
| 36 | u32 | Flags: bit 0, the position is tracked |

The rotation and position are in the device's world frame: right-handed, z up, the way Android's
rotation vector and iOS Core Motion (`xArbitraryZVertical`) give them. The device frame is the
screen's: x to the right of the screen held upright, y to its top, z out of the screen. The app is
held in landscape with the top of the device to the left, and the camera looks out of its back.
The editor turns this into a camera rotation; only changes from the way the device was held when
Operate began are used, so the world frame's heading does not matter.

### Controls (0x30)
| `action` | Other fields | Does what the panel's control does |
|---|---|---|
| `operate` | | Operate on (the device drives the support) |
| `release` | | Operate off |
| `zero` | | Zero |
| `record` | | Record |
| `stop` | | Stop recording |
| `bake` | | Bake Take |
| `lens` | `step`: 1 or −1 | The next or previous lens of the kit |
| `rack` | `mark`: index | Rack the focus to that focus mark |

### State (0x31)
Sent when it changes, and at least twice a second.

```json
{
  "camera": "A Cam", "support": "Steadicam",
  "operating": true, "live": true, "recording": false, "takeSeconds": 0, "hasTake": false,
  "lens": "35 mm", "tStop": "T2.8", "focus": "2.4 m", "aspect": 2.39,
  "marks": ["Anna", "Door"], "warnings": ["Pan peaks at 389.8°/s; this steadicam manages 120°/s."]
}
```

`aspect` is the delivery aspect the app draws frame lines for. Text is in the user's units and
language, ready to show.
