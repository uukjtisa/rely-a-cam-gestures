# LINK v1: Rely-A-Cam plugin ↔ Rely-A-Cam-Gestures

The plugin (inside OBS) and this app (its own process) talk over one local pipe for messages and
two shared-memory blocks per phone for pictures. Code: `src/link/` here, `obs-plugin/src/gestures/`
in Rely-A-Cam. Change both and this file together.

## Starting

- The app listens on the local server `Rely-A-Cam-Gestures-link-<Windows user>`.
- The plugin finds the app through `RACG_EXE` (a dev build), else
  `HKCU` or `HKLM\Software\Rely-A-Cam-Gestures\InstallPath`, and starts it with
  `--attach --hidden` when a phone turns gestures on. It retries at most every 20 s (30 s after 3
  tries).
- One plugin at a time: a second client gets BYE `{"reason":"in use"}`. A different `v` gets BYE
  `{"reason":"version"}`.
- When no phone wants gestures the plugin sends BYE and disconnects. An app started with
  `--attach` then quits, unless its window is open.

## Messages

```
message := type:u8  length:u32 (big-endian)  payload[length]      length <= 32 MiB
```

Payloads are JSON unless noted.

| Type | Name | Direction | Payload |
|---|---|---|---|
| 0x01 | HELLO | both ways | `{"v":1,"app":"rely-a-cam","version":"0.1.0"}`; the app answers with its own (`"app":"rely-a-cam-gestures"`) |
| 0x02 | PHONES | plugin → app | `{"phones":[{"device","name"}]}`: the phones with gestures on |
| 0x03 | FRAME | plugin → app | `{"device","slot","seq","w","h","stride","outW","outH"}`: a picture is in the phone's in block; `outW`×`outH` is the source's size |
| 0x04 | PHONE | plugin → app | `{"device","state":{"torch","muted","micOnly","front","zoom","hasTorch"}}` |
| 0x05 | OBS | plugin → app | `{"scenes":[...],"scene","recording","streaming","hotkeys":[...]}` |
| 0x06 | CMD | plugin → app | `{"openEditor":device}`, `{"gestures":bool}`, `{"overlay":{"which","place","on"}}` (`which`: `grid`, `detection`, `face`) |
| 0x07 | LIBSET | plugin → app | an edit from a phone's Library screen, plus `"device"` (Rely-A-Cam PROTOCOL.md, GSET) |
| 0x08 | UPLOAD | plugin → app | binary: `nameLength:u32  name  bytes`, a meme from a phone |
| 0x0F | BYE | either | `{"reason":...}` |
| 0x10 | LAYER | app → plugin | `{"device","empty","crop":[x,y,w,h]}`, plus `"slot","seq","w","h","stride"` when not empty: the layer is in the out block |
| 0x12 | ACTION | app → plugin | `{"device","id","arg"}` (below) |
| 0x13 | EVENT | app → plugin | `{"device","gesture","name","op":"start"|"end"}` (logged) |
| 0x14 | AUDIO | app → plugin | binary: 48 kHz stereo float, 10 ms blocks, only when not silent |
| 0x15 | STATUS | app → plugin | `{"version","ready","device","error","gestures","overlay":{"grid":{"dock","output"},"detection":{"dock","output"},"face":{"dock","output"}}}` |
| 0x16 | DET | app → plugin | `{"device","hands":[[x0,y0,...]],"g":[{"n","s","a","c","x","y"}],"stop","face"}` while the detection overlay's or the face map's dock place is on |
| 0x17 | LIB | app → plugin | `{"gestures":[{"id","name","on","mine","action","control"?}],"memes":[{"id","name","kind","place","volume","sound"}],"controls":[{"id","label","group","on","move","how","gesture"}]}` |

DET points are fractions of the upright picture (21 per hand). In `g`, `s` is `""`, `"armed"` or
`"waiting"` (for the confirm sign), `a` is the hold, 0 to 1, and `c` names the confirm sign.
The pinch moves' rings and the face map's label are `g` entries too. `face` is a list of
polylines (fractions), present while the face map is on.

## Shared memory

Each block is `Local\RACG.<in|out>.<fnv1a-32 of the device id, 8 hex>`: a 64-byte header, then two
slots, written alternately.

```
header := magic "RACGLNK1"  slotBytes:u32  reserved:u32  seq[2]:u64  w[2]:u32  h[2]:u32  stride[2]:u32
```

- **in** (the plugin makes it, 640 × 640 × 4 per slot): the phone's picture, BGRA, upright as the
  source shows it (turned, then mirrored or flipped), about 15 a second.
- **out** (the app makes it, 1280 × 1280 × 4 per slot): the layer to draw over the source,
  premultiplied BGRA, the source's shape. `empty` in LAYER means "draw nothing"; `crop` is the
  part of the picture the source shows (the framing), always sent.

A reader takes the slot whose `seq` matches the message's.

## ACTION ids

| id | arg |
|---|---|
| `phone.torch` | `on` / `off` |
| `phone.mic` | `on` (muted) / `off` |
| `phone.miconly` | `on` / `off` |
| `phone.switch` | `front` / `back` |
| `zoom.step` | `+1` / `-1` (the phone's zoom presets) |
| `snapshot` | |
| `obs.scene` | the scene's name |
| `obs.record`, `obs.stream` | `on` / `off` |
| `obs.hotkey` | the hotkey's label (`"Source: description"` for a source's) |

Camera off while attached is not an action: the layer turns opaque (black or the chosen picture)
and the phone keeps sending, so the gesture that turns it back on is still seen.
