#pragma once
#include <Arduino.h>

// User settings stored in NVS (namespace "goodusb"), editable from the app
// over the authenticated WebSocket. Trust-the-user model: values are only
// sanity-checked (length/charset), then applied on the next boot.
namespace settings {
void begin();
String deviceName();   // display name: HID product string, e.g. "GoodUSB"
String hostname();     // mDNS/DHCP slug derived from deviceName
String wifiSsid();     // empty -> fall back to src/secrets.h defaults
String wifiPass();
bool saveDeviceName(const String &name);          // also derives hostname
bool saveWifi(const String &ssid, const String &pass);
String slugify(const String &name);
// Active template (device profile). Applied via app "applytemplate", stored
// here, takes effect after the firmware restarts and USB re-enumerates.
// Default is a mouse-only "PC" profile: simple HID for maximum compatibility.
String tplName();          // e.g. "PC"
bool tplMouseOnly();       // true: mouse interface only; false: full composite
String tplButtons();       // buttons JSON array, verbatim as validated
bool saveTemplate(const String &name, bool mouseOnly, const String &buttonsJson);
}  // namespace settings
