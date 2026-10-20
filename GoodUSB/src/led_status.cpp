#include "led_status.h"

#include <Arduino.h>

#include "config.h"

namespace {
led::State base = led::BOOT;
volatile uint32_t activityUntil = 0;

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t k = GOODUSB_LED_BRIGHTNESS;  // 0..255 scale
  neopixelWrite(GOODUSB_LED_PIN, (uint8_t)(r * k / 255),
                (uint8_t)(g * k / 255), (uint8_t)(b * k / 255));
}
}  // namespace

namespace led {

void begin() { setLed(0, 0, 0); }
void setBase(State s) { base = s; }
void activity() { activityUntil = millis() + 70; }

void service() {
  uint32_t now = millis();
  if ((int32_t)(activityUntil - now) > 0) { setLed(255, 255, 255); return; }
  switch (base) {
    case BOOT:        setLed(255, 140, 0); break;                       // amber
    case PAIRING: {                                                    // blue pulse
      uint8_t v = (uint8_t)(128 + 127 * sin(now * 0.006));
      setLed(0, 0, v); break;
    }
    case WIFI_CONNECT:                                                 // yellow blink 1 Hz
      (now / 500) % 2 ? setLed(255, 200, 0) : setLed(0, 0, 0); break;
    case READY:       setLed(0, 90, 30); break;                         // green dim
    case CONNECTED:   setLed(0, 255, 80); break;                        // green
    case ERROR:                                                        // red blink 2 Hz
      (now / 250) % 2 ? setLed(255, 0, 0) : setLed(0, 0, 0); break;
  }
}
}  // namespace led
