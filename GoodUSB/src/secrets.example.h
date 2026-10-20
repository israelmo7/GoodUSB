#pragma once
// ---------------------------------------------------------------------------
// Wi-Fi credentials for the FIRST flash only.
//
//   cp src/secrets.example.h src/secrets.h
//
// Then replace the values below with your home Wi-Fi (2.4 GHz — the
// ESP32-S3 has no 5 GHz radio). src/secrets.h is gitignored; never commit
// real credentials.
//
// After the device is online, Wi-Fi can be changed from the phone app:
// Device settings -> Wi-Fi name / password (stored in NVS, wins over this
// file, applied on restart).
// ---------------------------------------------------------------------------
#define GOODUSB_WIFI_SSID "YourWiFiName"
#define GOODUSB_WIFI_PASSWORD "YourWiFiPassword"
