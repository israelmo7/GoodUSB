#include "auth.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "gu_log.h"

namespace {
Preferences prefs;
bool pairedFlag = false;
String saltHex, pinHashHex, verifierHex;
uint32_t windowUntil = 0;
uint8_t pinFails = 0;
uint32_t pinLockUntil = 0;
uint32_t btnDownSince = 0;
bool btnFired = false;

String toHex(const uint8_t *data, size_t len) {
  static const char *H = "0123456789abcdef";
  String out; out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) { out += H[data[i] >> 4]; out += H[data[i] & 15]; }
  return out;
}
bool fromHex(const String &hex, uint8_t *out, size_t len) {
  if ((size_t)hex.length() != len * 2) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < len; i++) {
    int a = nib(hex[i * 2]), b = nib(hex[i * 2 + 1]);
    if (a < 0 || b < 0) return false;
    out[i] = (uint8_t)(a * 16 + b);
  }
  return true;
}
String sha256Hex(const uint8_t *data, size_t len) {
  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, data, len);
  mbedtls_sha256_finish_ret(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  return toHex(digest, sizeof(digest));
}
String randomHex(size_t bytes) {
  uint8_t buf[32];
  if (bytes > sizeof(buf)) bytes = sizeof(buf);
  esp_fill_random(buf, bytes);
  return toHex(buf, bytes);
}
String hashPin(const String &pin) {
  uint8_t salt[16];
  fromHex(saltHex, salt, sizeof(salt));
  String material;
  // Hash raw salt bytes followed by the PIN bytes.
  uint8_t digestIn[16 + 16];
  size_t pinLen = min<size_t>(pin.length(), 16);
  memcpy(digestIn, salt, 16);
  memcpy(digestIn + 16, pin.c_str(), pinLen);
  return sha256Hex(digestIn, 16 + pinLen);
}
bool constEq(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) diff |= a[i] ^ b[i];
  return diff == 0;
}
bool validPin(const String &pin) {
  if (pin.length() < 4 || pin.length() > 8) return false;
  for (size_t i = 0; i < pin.length(); i++)
    if (pin[i] < '0' || pin[i] > '9') return false;
  return true;
}
void storeToken(const String &token) {
  verifierHex = sha256Hex((const uint8_t *)token.c_str(), token.length());
  prefs.begin("goodusb", false);
  prefs.putString("verifier", verifierHex);
  prefs.putBool("paired", true);
  prefs.end();
  pairedFlag = true;
}
}  // namespace

namespace auth {

void begin() {
  pinMode(GOODUSB_BOOT_BTN_PIN, INPUT_PULLUP);
  prefs.begin("goodusb", true);
  pairedFlag = prefs.getBool("paired", false);
  saltHex = prefs.getString("salt", "");
  pinHashHex = prefs.getString("pinhash", "");
  verifierHex = prefs.getString("verifier", "");
  prefs.end();
  if (!pairedFlag) openPairingWindow("unpaired first boot");
  else GU_LOG("auth: paired owner present, pairing window closed");
}

bool isPaired() { return pairedFlag; }
bool pairingOpen() { return (int32_t)(windowUntil - millis()) > 0; }
uint32_t pairingLeftMs() {
  int32_t left = (int32_t)(windowUntil - millis());
  return left > 0 ? (uint32_t)left : 0;
}
bool pinLocked() { return (int32_t)(pinLockUntil - millis()) > 0; }

bool openPairingWindow(const char *why) {
  windowUntil = millis() + GOODUSB_PAIR_WINDOW_MS;
  GU_LOG("auth: pairing window open %u s (%s)",
         (unsigned)(GOODUSB_PAIR_WINDOW_MS / 1000), why);
  return true;
}

bool tryPair(const String &pin, String &tokenOut) {
  if (!pairingOpen() || !validPin(pin)) return false;
  saltHex = randomHex(16);
  pinHashHex = hashPin(pin);
  prefs.begin("goodusb", false);
  prefs.putString("salt", saltHex);
  prefs.putString("pinhash", pinHashHex);
  prefs.end();
  tokenOut = randomHex(32);
  storeToken(tokenOut);
  windowUntil = 0;
  pinFails = 0;
  GU_LOG("auth: paired (PIN set, token issued)");
  return true;
}

bool unlockWithPin(const String &pin, String &tokenOut) {
  if (!pairedFlag || pinLocked() || !validPin(pin)) return false;
  if (!constEq(hashPin(pin), pinHashHex)) {
    if (++pinFails >= GOODUSB_MAX_PIN_ATTEMPTS) {
      pinLockUntil = millis() + GOODUSB_AUTH_LOCK_MS;
      pinFails = 0;
      GU_LOG("auth: PIN locked for %u s", (unsigned)(GOODUSB_AUTH_LOCK_MS / 1000));
    }
    return false;
  }
  pinFails = 0;
  tokenOut = randomHex(32);
  storeToken(tokenOut);   // rotates: previous phone's token dies here
  GU_LOG("auth: PIN unlock, token rotated");
  return true;
}

String newNonceHex() { return randomHex(16); }

bool checkProof(const String &nonceHex, const String &proofHex) {
  if (!pairedFlag || !verifierHex.length()) return false;
  String material = verifierHex + ":" + nonceHex;
  String expected = sha256Hex((const uint8_t *)material.c_str(), material.length());
  return constEq(expected, proofHex);
}

void service() {
  // BOOT-button hold (3 s) reopens pairing. Holding BOOT at reset enters
  // the ROM downloader instead — this only counts presses after boot.
  bool down = digitalRead(GOODUSB_BOOT_BTN_PIN) == LOW;
  uint32_t now = millis();
  if (down && !btnDownSince) btnDownSince = now;
  if (!down) { btnDownSince = 0; btnFired = false; }
  if (down && !btnFired && btnDownSince &&
      now - btnDownSince >= GOODUSB_BOOT_HOLD_MS) {
    btnFired = true;
    openPairingWindow("BOOT button hold");
  }
}
}  // namespace auth
