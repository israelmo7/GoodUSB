#pragma once

// Status LED: the device's only face. On ESP32-S3-DevKitC-1 the addressable
// RGB LED is on GPIO48 (config.h GOODUSB_LED_PIN); neopixelWrite is a no-op
// visually on boards without one. Colors are kept dim on purpose.
namespace led {
enum State { BOOT, PAIRING, WIFI_CONNECT, READY, CONNECTED, ERROR };
void begin();
void setBase(State s);
void activity();   // brief flash on HID activity (called from HID task)
void service();    // call from loop(): pulses, blinks, activity timeout
}  // namespace led
