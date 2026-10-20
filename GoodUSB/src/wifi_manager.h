#pragma once
#include <Arduino.h>

namespace wifiMgr {
void begin();
void service();  // call from loop(): reconnect with backoff, (re)start mDNS
bool connected();
int32_t rssi();
String ip();
}  // namespace wifiMgr
