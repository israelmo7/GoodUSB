#pragma once
#include <stdint.h>

namespace wsServer {
void begin();    // LittleFS, HTTP, WebSocket, HID queue + dispatch task
void service();  // call from loop(): cleanup clients, silence watchdog
uint32_t clientCount();
bool controllerPresent();
uint32_t droppedFrames();
uint32_t hidReports();
}  // namespace wsServer
