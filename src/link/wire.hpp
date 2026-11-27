// The link between this app and Rely-A-Cam's OBS plugin (docs/LINK.md is the reference; the
// plugin keeps a copy of these numbers in obs-plugin/src/gestures/wire.hpp).
//
// Messages go over a named pipe, framed like Rely-A-Cam's wire protocol: type:u8 length:u32
// (big-endian) payload. Pictures go through shared memory: one block per phone and direction,
// two slots each, so a picture is never read while it's being written.
#pragma once

#include <QString>

#include <cstdint>

namespace racg::link {

constexpr int kVersion = 1;

enum class Msg : uint8_t {
  // plugin -> app
  Hello = 0x01,    // JSON {v, app:"rely-a-cam", version}
  Phones = 0x02,   // JSON {phones:[{device, name}]}: the phones with gestures on
  Frame = 0x03,    // JSON {device, slot, seq, w, h, stride, outW, outH}: pixels in the "in" block
  Phone = 0x04,    // JSON {device, state:{...}, caps:{...}}: what the phone can do now
  Obs = 0x05,      // JSON {recording, streaming, scene, scenes:[], hotkeys:[[name, label]]}
  Cmd = 0x06,      // JSON {openEditor} | {gestures:bool} | {overlay:{dock|phone|output:bool}}
  LibSet = 0x07,   // JSON: an edit from the phone's Library screen (docs/LINK.md)
  Upload = 0x08,   // binary: name length u32, UTF-8 name, file bytes (a meme from the phone)
  Bye = 0x0F,      // JSON {reason}
  // app -> plugin
  Layer = 0x10,    // JSON {device, slot, seq, w, h, empty, crop:[x,y,w,h]}: pixels in the "out" block
  Action = 0x12,   // JSON {device, id, op:"start"|"end", arg}: phone and OBS actions the plugin does
  Event = 0x13,    // JSON {device, gesture, name, op}: a gesture started or ended its action
  Audio = 0x14,    // binary: frames u32, then float32 stereo at 48 kHz (the Phone Memes source)
  Status = 0x15,   // JSON {version, ready, device, error, gestures, overlay:{dock, phone, output}}
  Det = 0x16,      // JSON {device, hands:[[x,y]*21], g:[{n,a,w,x,y}], stop}: for the dock/phone overlay
  Lib = 0x17,      // JSON {gestures:[...], memes:[...]}: for the phone's Library screen
};

constexpr uint32_t kMaxMessage = 32u * 1024u * 1024u;

// Shared memory blocks: a header, then two slots of BGRA pixels.
constexpr uint32_t kInSlotBytes = 640u * 640u * 4u;      // the 640-wide picture, either way up
constexpr uint32_t kOutSlotBytes = 1280u * 1280u * 4u;   // the meme layer, up to 1280x720 either way up
constexpr uint32_t kHeaderBytes = 64;

struct BlockHeader {
  char magic[8];           // "RACGLNK1"
  uint32_t slotBytes;
  uint32_t reserved;
  uint64_t seq[2];         // the picture in each slot, set after its pixels are written
  uint32_t w[2], h[2], stride[2];
};
static_assert(sizeof(BlockHeader) <= kHeaderBytes);

// "Rely-A-Cam-Gestures-link-<windows user>": the pipe the app listens on.
QString pipeName();
// "Local\RACG.<in|out>.<8 hex>" for a phone's device id.
QString blockName(const char *direction, const QString &device);

}  // namespace racg::link
