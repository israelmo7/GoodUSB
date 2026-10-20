#pragma once
#include <Arduino.h>
// Serial logging that survives CORE_DEBUG_LEVEL: the Arduino-ESP32 libs
// builder force-defines CORE_DEBUG_LEVEL=0 after user flags, which would
// compile log_i/log_w/log_e out. Use GU_LOG (UART0; USB-CDC is off).
#define GU_LOG(...)            \
  do {                         \
    Serial.printf(__VA_ARGS__); \
    Serial.print('\n');        \
  } while (0)
