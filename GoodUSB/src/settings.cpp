#include "settings.h"

#include <Preferences.h>

#include "config.h"

namespace {
Preferences prefs;
String devName = GOODUSB_DEVICE_NAME;
String host = GOODUSB_HOSTNAME;
String ssid;
String pass;
String tplName_ = "PC";
bool tplMouse_ = true;
String tplBtns_ = "[]";
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
  tplName_ = prefs.getString("tpl_name", "PC");
  tplMouse_ = prefs.getBool("tpl_mouse", true);
  tplBtns_ = prefs.getString("tpl_btns", "[]");
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

String tplName() { return tplName_; }
bool tplMouseOnly() { return tplMouse_; }
String tplButtons() { return tplBtns_; }

bool saveTemplate(const String &name, bool mouseOnly, const String &buttonsJson) {
  String n = name; n.trim();
  if (!n.length() || n.length() > 32) return false;
  if (buttonsJson.length() > 1800) return false;
  tplName_ = n; tplMouse_ = mouseOnly; tplBtns_ = buttonsJson;
  prefs.begin("goodusb", false);
  prefs.putString("tpl_name", tplName_);
  prefs.putBool("tpl_mouse", tplMouse_);
  prefs.putString("tpl_btns", tplBtns_);
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
