#include <Arduino.h>
#include "gu_log.h"
#include <FS.h>  // pulls FS into the dependency graph (USBMSCFS.h needs it)

#include "auth.h"
#include "config.h"
#include "hid_composite.h"
#include "led_status.h"
#include "settings.h"
#include "wifi_manager.h"
#include "ws_server.h"

// Boot order: settings -> LED -> HID -> auth -> Wi-Fi -> server/tasks.
// Serial is UART0 (USB-CDC off), so logs come from the UART header/pins,
// not the native USB port the host sees as a mouse.
void setup() {
  Serial.begin(115200);
  delay(200);
  settings::begin();
  led::begin();
  led::setBase(led::BOOT);
  GU_LOG("GoodUSB '%s' booting (USB HID on native port GPIO19/20)",
         settings::deviceName().c_str());

  hid::begin(settings::deviceName());
#if GOODUSB_BOOT_SELF_TEST
  // Dev-only Phase 1 check; OFF by default — a product must never click
  // things on the host just because it was plugged in.
  for (int i = 0; i < 30 && !hid::isReady(); i++) delay(100);
  hid::selfTest();
#endif

  auth::begin();
  wifiMgr::begin();
  wsServer::begin();
  GU_LOG("setup done, free heap %u", (unsigned)ESP.getFreeHeap());
}

void loop() {
  auth::service();
  wifiMgr::service();
  wsServer::service();
  led::service();
  delay(5);
}
