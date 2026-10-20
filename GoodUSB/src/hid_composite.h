#pragma once
#include <Arduino.h>
#include <stdint.h>

// One composite USB HID device: mouse + keyboard + consumer control.
// All functions are safe to call before the host enumerates (they no-op
// or queue state that is applied to reports once mounted).
namespace hid {
void begin(const String &productName);
bool isReady();              // host has enumerated / USB mounted
void move(int16_t dx, int16_t dy, int8_t wheel);  // chunked to int8 reports
void setButtons(uint8_t mask);                    // full-state, diffed
void keyEvent(uint8_t mod, uint8_t key, bool down);
void consumerEvent(uint16_t usage, bool down);
void releaseAll();           // stuck-key safety: buttons+keys+consumer up
uint8_t buttonMask();
bool anyHeld();              // any button/key/consumer currently down
void selfTest();             // Phase 1 scripted check (see config.h gate)
}  // namespace hid
