#include "hid_composite.h"

#include <Arduino.h>
#include "gu_log.h"
#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

#include "config.h"

namespace {
USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;
USBHIDConsumerControl ConsumerControl;

volatile bool usbMounted = false;
uint8_t currentMask = 0;
uint16_t heldConsumer = 0;

// Small pressed-key set so releaseAll() can unwind exactly what we pressed.
uint8_t pressedKeys[24];
uint8_t pressedCount = 0;

bool trackDown(uint8_t k) {
  for (uint8_t i = 0; i < pressedCount; i++)
    if (pressedKeys[i] == k) return false;
  if (pressedCount < sizeof(pressedKeys)) pressedKeys[pressedCount++] = k;
  return true;
}
void trackUp(uint8_t k) {
  for (uint8_t i = 0; i < pressedCount; i++)
    if (pressedKeys[i] == k) {
      pressedKeys[i] = pressedKeys[--pressedCount];
      return;
    }
}

// HID modifier bit (protocol) -> raw modifier usage 0xE0..0xE7.
void applyMod(uint8_t mod, bool down) {
  for (uint8_t bit = 0; bit < 8; bit++) {
    if (!(mod & (1u << bit))) continue;
    uint8_t usage = 0xE0 + bit;
    if (down) {
      if (trackDown(usage)) Keyboard.pressRaw(usage);
    } else {
      trackUp(usage);
      Keyboard.releaseRaw(usage);
    }
  }
}

int8_t clampChunk(int16_t v) { return (int8_t)max(-127, min(127, (int)v)); }
}  // namespace

namespace hid {

void begin(const String &productName) {
  USB.onEvent([](void *, esp_event_base_t base, int32_t id, void *) {
    if (base != ARDUINO_USB_EVENTS) return;
    if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT)
      usbMounted = true;
    if (id == ARDUINO_USB_STOPPED_EVENT) usbMounted = false;
    // SUSPEND: host may sleep; reports are dropped by TinyUSB until RESUME.
  });
  USB.VID(0x303A);          // Espressif dev VID; pick your own for production
  USB.PID(0x4001);
  USB.productName(productName.c_str());
  USB.manufacturerName("GoodUSB");
  USB.firmwareVersion(0x0100);
  Mouse.begin();
  Keyboard.begin();
  ConsumerControl.begin();
  USB.begin();
}

bool isReady() { return usbMounted; }

void move(int16_t dx, int16_t dy, int8_t wheel) {
  // USBHIDMouse::move takes int8 deltas; split larger coalesced deltas.
  while (dx != 0 || dy != 0 || wheel != 0) {
    int8_t cx = clampChunk(dx), cy = clampChunk(dy), cw = clampChunk(wheel);
    Mouse.move(cx, cy, cw);
    dx -= cx; dy -= cy; wheel -= cw;
  }
}

void setButtons(uint8_t mask) {
  uint8_t changed = mask ^ currentMask;
  for (uint8_t bit = 0; bit < 5; bit++) {
    uint8_t b = (uint8_t)(1u << bit);
    if (!(changed & b)) continue;
    if (mask & b) Mouse.press(b); else Mouse.release(b);
  }
  currentMask = mask;
}

uint8_t buttonMask() { return currentMask; }
bool anyHeld() { return currentMask != 0 || pressedCount != 0 || heldConsumer != 0; }

void keyEvent(uint8_t mod, uint8_t key, bool down) {
  if (down) {
    applyMod(mod, true);
    if (key && trackDown(key)) Keyboard.pressRaw(key);
  } else {
    if (key) { trackUp(key); Keyboard.releaseRaw(key); }
    applyMod(mod, false);
  }
}

void consumerEvent(uint16_t usage, bool down) {
  if (down) {
    if (heldConsumer && heldConsumer != usage) ConsumerControl.release();
    heldConsumer = usage;
    ConsumerControl.press(usage);
  } else {
    ConsumerControl.release();
    if (heldConsumer == usage) heldConsumer = 0;
  }
}

void releaseAll() {
  setButtons(0);
  Keyboard.releaseAll();
  pressedCount = 0;
  ConsumerControl.release();
  heldConsumer = 0;
}

void selfTest() {
  if (!isReady()) {
    GU_LOG("self-test skipped: USB host not enumerated yet");
    return;
  }
  GU_LOG("self-test: mouse jiggle, click, arrows, Vol+/Vol- (3 s)");
  delay(3000);
  for (int i = 0; i < 10; i++) { Mouse.move(12, 0); delay(20); }
  for (int i = 0; i < 10; i++) { Mouse.move(-12, 0); delay(20); }
  for (int i = 0; i < 10; i++) { Mouse.move(0, 12); delay(20); }
  for (int i = 0; i < 10; i++) { Mouse.move(0, -12); delay(20); }
  Mouse.press(1); delay(60); Mouse.release(1);          // left click
  const uint8_t arrows[] = {0x4F, 0x51, 0x50, 0x52};    // R D L U (HID usages)
  for (uint8_t k : arrows) {
    Keyboard.pressRaw(k); delay(60); Keyboard.releaseRaw(k); delay(120);
  }
  consumerEvent(0x00E9, true); delay(80); consumerEvent(0x00E9, false);
  delay(150);
  consumerEvent(0x00EA, true); delay(80); consumerEvent(0x00EA, false);
  releaseAll();
  GU_LOG("self-test done");
}
}  // namespace hid
