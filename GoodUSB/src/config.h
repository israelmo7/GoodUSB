#pragma once
// Central tunables (Phase 6 latency/safety tuning lives here).
#define GOODUSB_HOSTNAME "goodusb"
#define GOODUSB_DEVICE_NAME "GoodUSB"
// Boot self-test (Phase 1 acceptance): after USB enumeration, jiggle the
// mouse, click, tap arrow keys and Vol+/Vol- (net-neutral). It runs ONCE,
// 3 s after boot, so keep the host on a harmless screen for first flash.
// Set to 0 for everyday use.
#define GOODUSB_BOOT_SELF_TEST 0
// HID dispatch: drain the queue at most this often; moves coalesce inside
// one window. 8 ms = 125 Hz, aligned with typical USB HID polling.
#define GOODUSB_COALESCE_MS 8
#define GOODUSB_HID_QUEUE_LEN 48
// Safety: if the controlling client goes silent (no frame of any kind)
// for this long, release everything. Catches half-open TCP on Wi-Fi drops
// before the WebSocket disconnect event fires.
#define GOODUSB_CLIENT_TIMEOUT_MS 4000
#define GOODUSB_PING_INTERVAL_MS 15000
// Pairing/auth (v2): window opens only when unpaired, via BOOT hold, or
// from the authenticated controller — NOT on every boot.
#define GOODUSB_PAIR_WINDOW_MS 60000
#define GOODUSB_MAX_PIN_ATTEMPTS 5
#define GOODUSB_AUTH_LOCK_MS 60000
#define GOODUSB_BOOT_BTN_PIN 0
#define GOODUSB_BOOT_HOLD_MS 3000
// Status RGB LED (DevKitC-1: GPIO48). Dim; set pin <0 to disable visuals.
#define GOODUSB_LED_PIN 48
#define GOODUSB_LED_BRIGHTNESS 28
