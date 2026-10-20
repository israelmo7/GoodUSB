#pragma once
#include <Arduino.h>

// Pairing + token auth (NVS namespace "goodusb"). Threat model: trusted
// home LAN. Goals: stop casual hijack / accidental control, never send the
// long-term token in the clear after pairing, make replays useless.
// Flow:
//   1. Pairing window (GOODUSB_PAIR_WINDOW_MS) opens automatically ONLY if
//      no owner is paired. Later it reopens via BOOT-button hold (3 s) or
//      from the authenticated controller ("openpair"). Never on every boot.
//   2. During the window the user CREATES a PIN in the app. The ESP stores
//      SHA256(salt || PIN) and returns a random 256-bit token ONCE.
//   3. Daily use: server sends a random nonce; the phone answers
//      proof = SHA256hex( SHA256hex(token) || ":" || nonce ). The server
//      stores only SHA256hex(token) (the verifier) — same value the phone
//      derives locally, so the token itself never crosses the LAN again.
//   4. Lost phone? Enter the PIN ("unlock") to rotate to a fresh token.
namespace auth {
void begin();          // loads NVS; opens window if unpaired
void service();        // window expiry + BOOT-button hold (call from loop)
bool isPaired();
bool pairingOpen();
uint32_t pairingLeftMs();
bool openPairingWindow(const char *why);
bool tryPair(const String &pin, String &tokenOut);   // window only
bool unlockWithPin(const String &pin, String &tokenOut);  // rotates token
String newNonceHex();
bool checkProof(const String &nonceHex, const String &proofHex);
bool pinLocked();
}  // namespace auth
