#pragma once
#include <stdint.h>
// Binary wire protocol v1 — shared contract between firmware and data/app.js.
// No JSON on the hot path. All multi-byte fields little-endian.
//   0x01 MOVE     dx:i16 dy:i16 wheel:i8            (6 bytes)
//   0x02 BUTTON   mask:u8  (full state: 1=L 2=R 4=M 8=Back 16=Fwd)
//   0x03 KEY      mod:u8 key:u8(HID usage) action:u8(1=down 0=up)
//                 mod bits are HID modifier bits (0x01 LCtrl 0x02 LShift
//                 0x04 LAlt 0x08 LGUI 0x10 RCtrl 0x20 RShift 0x40 RAlt 0x80 RGUI)
//                 key >= 0xE0 means a modifier usage sent on its own.
//   0x04 CONSUMER usage:u16 action:u8(1=down 0=up)  (standard HID consumer
//                 usages: 0xCD Play/Pause 0xE2 Mute 0xE9 Vol+ 0xEA Vol-
//                 0xB5 Next 0xB6 Prev 0x223 AC Home 0x224 AC Back)
//   0x05 PING     clientTs:u32  -> server replies 0x06 PONG with the same
//                 clientTs plus serverMillis:u32 (9 bytes total)
//   0x07 RELEASE_ALL (1 byte) — client-side panic release; the server also
//                 releases everything itself when the controller disconnects.
// Text frames (low rate, control plane only): client sends "takeover";
// server sends {"type":"status","role":"controller"|"observer",...}.
enum GoodUsbOpcode : uint8_t {
  OP_MOVE = 0x01,
  OP_BUTTON = 0x02,
  OP_KEY = 0x03,
  OP_CONSUMER = 0x04,
  OP_PING = 0x05,
  OP_PONG = 0x06,
  OP_RELEASE_ALL = 0x07,
};

struct __attribute__((packed)) MoveFrame {
  uint8_t opcode; int16_t dx; int16_t dy; int8_t wheel;
};
struct __attribute__((packed)) ButtonFrame { uint8_t opcode; uint8_t mask; };
struct __attribute__((packed)) KeyFrame {
  uint8_t opcode; uint8_t mod; uint8_t key; uint8_t action;
};
struct __attribute__((packed)) ConsumerFrame {
  uint8_t opcode; uint16_t usage; uint8_t action;
};
struct __attribute__((packed)) PingFrame { uint8_t opcode; uint32_t clientTs; };
struct __attribute__((packed)) PongFrame {
  uint8_t opcode; uint32_t clientTs; uint32_t serverMillis;
};
