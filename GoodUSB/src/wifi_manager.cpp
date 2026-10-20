#include "wifi_manager.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "gu_log.h"
#include "settings.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define GOODUSB_WIFI_SSID "CHANGE_ME"
#define GOODUSB_WIFI_PASSWORD "CHANGE_ME"
#endif

namespace {
volatile bool wantReconnect = false;
bool mdnsUp = false;
uint32_t lastAttempt = 0;
String effSsid, effPass;   // settings (app) win; secrets.h is dev fallback

bool credentialsSet() { return effSsid.length() > 0; }

void startMdns() {
  if (mdnsUp) return;
  if (MDNS.begin(settings::hostname().c_str())) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "device", settings::deviceName());
    mdnsUp = true;
    GU_LOG("mDNS: http://%s.local", settings::hostname().c_str());
  } else {
    GU_LOG("mDNS start failed");
  }
}
}  // namespace

namespace wifiMgr {

void begin() {
  effSsid = settings::wifiSsid();
  effPass = settings::wifiPass();
  if (!effSsid.length() && String(GOODUSB_WIFI_SSID) != "CHANGE_ME")
    effSsid = GOODUSB_WIFI_SSID, effPass = GOODUSB_WIFI_PASSWORD;

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(settings::hostname().c_str());
  WiFi.setSleep(false);          // no modem sleep on the control path
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      mdnsUp = false;
      wantReconnect = true;
      GU_LOG("Wi-Fi disconnected, will reconnect");
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      GU_LOG("Wi-Fi connected: %s RSSI %d", WiFi.localIP().toString().c_str(),
             (int)WiFi.RSSI());
      startMdns();
    }
  });
  if (!credentialsSet()) {
    GU_LOG("No Wi-Fi set: pair later via app Settings once on a network, "
           "or set src/secrets.h for first flash. USB HID keeps working.");
    return;
  }
  lastAttempt = millis();
  WiFi.begin(effSsid, effPass);
}

void service() {
  if (!credentialsSet()) return;
  if (WiFi.status() == WL_CONNECTED) { startMdns(); return; }
  if (wantReconnect || millis() - lastAttempt > 10000) {
    wantReconnect = false;
    lastAttempt = millis();
    GU_LOG("Wi-Fi reconnecting to %s...", effSsid);
    WiFi.disconnect(false, false);
    WiFi.begin(effSsid, effPass);
  }
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
int32_t rssi() { return connected() ? WiFi.RSSI() : 0; }
String ip() { return WiFi.localIP().toString(); }
}  // namespace wifiMgr
