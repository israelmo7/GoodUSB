#include "settings.h"

#include <Preferences.h>

#include "config.h"

namespace {
Preferences prefs;
String devName = GOODUSB_DEVICE_NAME;
String host = GOODUSB_HOSTNAME;
String ssid;
String pass;
}  // namespace

namespace settings {

String slugify(const String &name) {
  String out;
  for (size_t i = 0; i < name.length() && out.length() < 24; i++) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    else if (out.length() && !out.endsWith("-")) out += '-';
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out.length() ? out : String(GOODUSB_HOSTNAME);
}

void begin() {
  prefs.begin("goodusb", true);
  devName = prefs.getString("devname", GOODUSB_DEVICE_NAME);
  host = prefs.getString("hostname", slugify(devName));
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("wpass", "");
  prefs.end();
}

String deviceName() { return devName; }
String hostname() { return host; }
String wifiSsid() { return ssid; }
String wifiPass() { return pass; }

bool saveDeviceName(const String &name) {
  String n = name; n.trim();
  if (!n.length() || n.length() > 32) return false;
  devName = n;
  host = slugify(n);
  prefs.begin("goodusb", false);
  prefs.putString("devname", devName);
  prefs.putString("hostname", host);
  prefs.end();
  return true;
}

bool saveWifi(const String &s, const String &p) {
  if (s.length() > 32 || p.length() > 64) return false;
  ssid = s; pass = p;
  prefs.begin("goodusb", false);
  prefs.putString("ssid", ssid);
  prefs.putString("wpass", pass);
  prefs.end();
  return true;
}
}  // namespace settings
