#include "ws_server.h"

#include <Arduino.h>
#include "gu_log.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "auth.h"
#include "config.h"
#include "hid_composite.h"
#include "led_status.h"
#include "protocol.h"
#include "settings.h"
#include "wifi_manager.h"

namespace {

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

enum EvType : uint8_t { EV_MOVE, EV_BUTTON, EV_KEY, EV_CONSUMER, EV_RELEASE_ALL };
struct HidEvent {
  EvType type;
  int16_t dx, dy;
  int8_t wheel;
  uint8_t mask, mod, key, action;
  uint16_t usage;
};

QueueHandle_t hidQueue = nullptr;
volatile uint32_t controllerId = 0;
volatile uint32_t lastControllerSeen = 0;
volatile bool silenceLatched = false;
volatile uint32_t dropped = 0;
volatile uint32_t reports = 0;
uint32_t restartAt = 0;
bool wasPairing = false;

// ---- per-client auth sessions (max 8 sockets; control needs exactly 1) ----
struct Session { bool used = false; uint32_t id = 0; bool authed = false;
                   String nonce; uint8_t authFails = 0; };
Session sessions[8];

Session *sessionFor(uint32_t id, bool create = false) {
  Session *freeSlot = nullptr;
  for (auto &s : sessions) {
    if (s.used && s.id == id) return &s;
    if (!s.used && !freeSlot) freeSlot = &s;
  }
  if (create && freeSlot) {
    *freeSlot = Session{}; freeSlot->used = true; freeSlot->id = id;
    return freeSlot;
  }
  return nullptr;
}
bool isAuthed(uint32_t id) { Session *s = sessionFor(id); return s && s->authed; }

bool enqueue(const HidEvent &e) {
  if (!hidQueue || xQueueSend(hidQueue, &e, 0) != pdTRUE) {
    dropped = dropped + 1;
    return false;
  }
  return true;
}

void flushMove(int32_t &dx, int32_t &dy, int32_t &wheel) {
  if (dx == 0 && dy == 0 && wheel == 0) return;
  hid::move((int16_t)max(-32768, min(32767, (int)dx)),
            (int16_t)max(-32768, min(32767, (int)dy)),
            (int8_t)max(-127, min(127, (int)wheel)));
  reports = reports + 1;
  led::activity();
  dx = dy = wheel = 0;
}

// Core 1: drains the queue, coalescing MOVE deltas into one report per
// GOODUSB_COALESCE_MS window. Buttons/keys/consumer flush pending moves
// first (ordering) and are dispatched immediately — no added latency.
void hidTask(void *) {
  int32_t dx = 0, dy = 0, wheel = 0;
  uint32_t windowStart = 0;
  HidEvent e;
  for (;;) {
    bool got = xQueueReceive(hidQueue, &e,
                             pdMS_TO_TICKS(GOODUSB_COALESCE_MS)) == pdTRUE;
    if (!got) { flushMove(dx, dy, wheel); continue; }
    switch (e.type) {
      case EV_MOVE:
        if (dx == 0 && dy == 0 && wheel == 0) windowStart = millis();
        dx += e.dx; dy += e.dy; wheel += e.wheel;
        if (millis() - windowStart >= GOODUSB_COALESCE_MS)
          flushMove(dx, dy, wheel);
        break;
      case EV_BUTTON:
        flushMove(dx, dy, wheel);
        hid::setButtons(e.mask); reports = reports + 1; led::activity(); break;
      case EV_KEY:
        flushMove(dx, dy, wheel);
        hid::keyEvent(e.mod, e.key, e.action != 0); reports = reports + 1;
        led::activity(); break;
      case EV_CONSUMER:
        flushMove(dx, dy, wheel);
        hid::consumerEvent(e.usage, e.action != 0); reports = reports + 1;
        led::activity(); break;
      case EV_RELEASE_ALL:
        dx = dy = wheel = 0;
        hid::releaseAll(); reports = reports + 1; break;
    }
  }
}

// Minimal JSON string-field extractor for the low-rate text control plane.
// Values we parse are PINs, hex proofs/tokens and short names — no nesting.
bool jsonStr(const String &msg, const char *key, String &out) {
  String pat = String("\"") + key + "\"";
  int i = msg.indexOf(pat);
  if (i < 0) return false;
  int colon = msg.indexOf(':', i + pat.length());
  if (colon < 0) return false;
  int q1 = msg.indexOf('"', colon);
  if (q1 < 0) return false;
  int q2 = msg.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  out = msg.substring(q1 + 1, q2);
  return true;
}

void sendStatus(AsyncWebSocketClient *c) {
  if (!c || !c->canSend()) return;
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"status\",\"role\":\"%s\",\"hid\":%s,\"clients\":%u}",
           !isAuthed(c->id()) ? "unauthenticated"
           : (c->id() == controllerId) ? "controller" : "observer",
           hid::isReady() ? "true" : "false",
           (unsigned)ws.count());
  c->text(buf);
}
void sendStatusAll() { for (auto &c : ws.getClients()) sendStatus(&c); }

void sendChallenge(AsyncWebSocketClient *c, Session *s) {
  if (!auth::isPaired()) return;
  s->nonce = auth::newNonceHex();
  String msg = "{\"type\":\"challenge\",\"nonce\":\"" + s->nonce + "\"}";
  c->text(msg);
}

void grantAuth(AsyncWebSocketClient *c, Session *s) {
  s->authed = true;
  s->authFails = 0;
  if (controllerId == 0) {
    controllerId = c->id();
    lastControllerSeen = millis();
    silenceLatched = false;
  }
  c->text("{\"type\":\"auth_ok\"}");
  sendStatus(c);
  GU_LOG("ws id=%u authenticated (controller=%u)", (unsigned)c->id(),
         (unsigned)controllerId);
}

void promoteOldestObserver() {
  for (auto &c : ws.getClients()) {
    if (c.status() == WS_CONNECTED && isAuthed(c.id())) {
      controllerId = c.id();
      lastControllerSeen = millis();
      silenceLatched = false;
      GU_LOG("ws client %u promoted to controller", (unsigned)controllerId);
      break;
    }
  }
  sendStatusAll();
}

void onWsEvent(AsyncWebSocket *srv, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)srv;
  switch (type) {
    case WS_EVT_CONNECT: {
      Session *s = sessionFor(client->id(), true);
      String hello = "{\"type\":\"hello\",\"paired\":";
      hello += auth::isPaired() ? "true" : "false";
      hello += ",\"pairing\":";
      hello += auth::pairingOpen() ? "true" : "false";
      hello += ",\"pairing_left_s\":" + String(auth::pairingLeftMs() / 1000);
      hello += ",\"device\":\"" + settings::deviceName() + "\"";
      hello += ",\"hostname\":\"" + settings::hostname() + "\"";
      hello += ",\"profile\":{\"name\":\"" + settings::tplName() +
               "\",\"usb\":\"" + (settings::tplMouseOnly() ? "mouse" : "full") +
               "\",\"buttons\":" + settings::tplButtons() + "}}";
      client->text(hello);
      if (s) sendChallenge(client, s);
      GU_LOG("ws connect id=%u clients=%u", (unsigned)client->id(),
             (unsigned)ws.count());
      break;
    }
    case WS_EVT_DISCONNECT: {
      GU_LOG("ws disconnect id=%u", (unsigned)client->id());
      Session *s = sessionFor(client->id());
      if (s) s->used = false;
      if (client->id() == controllerId) {
        HidEvent e{}; e.type = EV_RELEASE_ALL; enqueue(e);
        controllerId = 0;
        promoteOldestObserver();
      }
      break;
    }
    case WS_EVT_PONG:
    case WS_EVT_PING:
      if (client->id() == controllerId && isAuthed(client->id()))
        lastControllerSeen = millis();
      break;
    case WS_EVT_DATA: {
      auto *info = (AwsFrameInfo *)arg;
      Session *s = sessionFor(client->id(), true);
      if (!s) return;
      if (info->opcode == WS_TEXT) {
        if (!(info->final && info->index == 0 && info->len == len)) return;
        String msg;
        msg.concat((const char *)data, len);
        String typeField;
        jsonStr(msg, "type", typeField);

        // ---- pre-auth control plane ----
        if (typeField == "pair") {
          String pin, token;
          jsonStr(msg, "pin", pin);
          if (auth::tryPair(pin, token)) {
            client->text("{\"type\":\"paired\",\"token\":\"" + token + "\"}");
            grantAuth(client, s);
          } else {
            client->text(auth::pairingOpen()
                             ? "{\"type\":\"pair_fail\",\"why\":\"bad_pin\"}"
                             : "{\"type\":\"pair_fail\",\"why\":\"window_closed\"}");
          }
          return;
        }
        if (typeField == "unlock") {
          String pin, token;
          jsonStr(msg, "pin", pin);
          if (auth::unlockWithPin(pin, token)) {
            client->text("{\"type\":\"paired\",\"token\":\"" + token + "\"}");
            grantAuth(client, s);
          } else {
            client->text(auth::pinLocked()
                             ? "{\"type\":\"unlock_fail\",\"why\":\"locked\"}"
                             : "{\"type\":\"unlock_fail\",\"why\":\"bad_pin\"}");
          }
          return;
        }
        if (typeField == "auth") {
          String proof;
          jsonStr(msg, "proof", proof);
          if (auth::checkProof(s->nonce, proof)) {
            grantAuth(client, s);
          } else {
            if (++s->authFails >= 3) client->close();
            else { client->text("{\"type\":\"auth_fail\"}"); sendChallenge(client, s); }
          }
          return;
        }
        // ---- everything below requires auth ----
        if (!s->authed) return;
        if (client->id() == controllerId) lastControllerSeen = millis();

        if (msg == "takeover" && client->id() != controllerId) {
          HidEvent e{}; e.type = EV_RELEASE_ALL; enqueue(e);
          controllerId = client->id();
          lastControllerSeen = millis();
          silenceLatched = false;
          GU_LOG("ws takeover by id=%u", (unsigned)controllerId);
          sendStatusAll();
        } else if (msg == "releaseall" && client->id() == controllerId) {
          HidEvent e{}; e.type = EV_RELEASE_ALL; enqueue(e);
        } else if (typeField == "openpair" && client->id() == controllerId) {
          auth::openPairingWindow("controller request");
        } else if (typeField == "getsettings" && client->id() == controllerId) {
          String out = "{\"type\":\"settings\",\"device\":\"" +
                       settings::deviceName() + "\",\"hostname\":\"" +
                       settings::hostname() + "\",\"ssid\":\"" +
                       settings::wifiSsid() + "\"}";
          client->text(out);
        } else if (typeField == "applytemplate" && client->id() == controllerId) {
          // Editable template -> device profile. Extract the buttons array
          // verbatim; light validation only (count/size), actions are still
          // constrained by the opcode handlers below.
          String name, usb, btns;
          jsonStr(msg, "name", name);
          jsonStr(msg, "usb", usb);
          int bpos = msg.indexOf("\"buttons\"");
          int lb = bpos >= 0 ? msg.indexOf('[', bpos) : -1;
          int rb = lb >= 0 ? msg.lastIndexOf(']') : -1;
          if (lb >= 0 && rb > lb) btns = msg.substring(lb, rb + 1);
          uint8_t btnCount = 0;
          for (int i = 0; btns.indexOf("\"label\"", i) >= 0; i = btns.indexOf("\"label\"", i) + 1) btnCount++;
          bool ok = btns.length() && btnCount <= 24 &&
                    settings::saveTemplate(name, usb == "mouse", btns);
          if (ok) {
            HidEvent ev{}; ev.type = EV_RELEASE_ALL; enqueue(ev);
            client->text("{\"type\":\"template_saved\",\"restart\":true}");
            GU_LOG("template applied: '%s' usb=%s buttons=%u, restarting",
                   name.c_str(), usb.c_str(), (unsigned)btnCount);
            restartAt = millis() + 800;   // reply first, then re-enumerate
          } else {
            client->text("{\"type\":\"template_fail\"}");
          }
        } else if (typeField == "settings" && client->id() == controllerId) {
          String v; bool changed = false;
          if (jsonStr(msg, "device", v)) changed |= settings::saveDeviceName(v);
          if (jsonStr(msg, "ssid", v)) {
            String p; jsonStr(msg, "wpass", p);
            changed |= settings::saveWifi(v, p);
          }
          client->text(changed ? "{\"type\":\"settings_saved\",\"restart\":true}"
                               : "{\"type\":\"settings_saved\",\"restart\":false}");
          if (changed) restartAt = millis() + 800;  // reply first, then reboot
        }
        return;
      }
      if (info->opcode != WS_BINARY || len < 1) return;
      // Single-controller + auth policy: everyone else is dropped here.
      if (!s->authed || client->id() != controllerId) {
        if (data[0] == OP_PING && s->authed) {
          // authed observers may still measure latency
        } else { dropped = dropped + 1; return; }
      }
      uint8_t op = data[0];
      if (op == OP_PING) {
        if (len >= sizeof(PingFrame)) {
          PingFrame in; memcpy(&in, data, sizeof(in));
          PongFrame out{OP_PONG, in.clientTs, (uint32_t)millis()};
          if (client->canSend()) client->binary((uint8_t *)&out, sizeof(out));
        }
        if (client->id() == controllerId) {
          lastControllerSeen = millis(); silenceLatched = false;
        }
        return;
      }
      if (client->id() != controllerId) { dropped = dropped + 1; return; }
      lastControllerSeen = millis();
      silenceLatched = false;
      // Simple-HID profile has no keyboard/consumer interfaces to send to.
      if (settings::tplMouseOnly() && (op == OP_KEY || op == OP_CONSUMER)) {
        dropped = dropped + 1; return;
      }

      HidEvent e{};
      switch (op) {
        case OP_MOVE:
          if (len < sizeof(MoveFrame)) { dropped = dropped + 1; return; }
          { MoveFrame f; memcpy(&f, data, sizeof(f));
            e.type = EV_MOVE; e.dx = f.dx; e.dy = f.dy; e.wheel = f.wheel; }
          break;
        case OP_BUTTON:
          if (len < sizeof(ButtonFrame)) { dropped = dropped + 1; return; }
          e.type = EV_BUTTON; e.mask = data[1] & 0x1F; break;
        case OP_KEY:
          if (len < sizeof(KeyFrame)) { dropped = dropped + 1; return; }
          { KeyFrame f; memcpy(&f, data, sizeof(f));
            e.type = EV_KEY; e.mod = f.mod; e.key = f.key;
            e.action = f.action ? 1 : 0; }
          break;
        case OP_CONSUMER:
          if (len < sizeof(ConsumerFrame)) { dropped = dropped + 1; return; }
          { ConsumerFrame f; memcpy(&f, data, sizeof(f));
            e.type = EV_CONSUMER; e.usage = f.usage;
            e.action = f.action ? 1 : 0; }
          break;
        case OP_RELEASE_ALL:
          e.type = EV_RELEASE_ALL; break;
        default:
          dropped = dropped + 1; return;
      }
      enqueue(e);
      break;
    }
    default: break;
  }
}

}  // namespace

namespace wsServer {

void begin() {
  if (!LittleFS.begin(true)) { GU_LOG("LittleFS mount failed"); led::setBase(led::ERROR); }
  hidQueue = xQueueCreate(GOODUSB_HID_QUEUE_LEN, sizeof(HidEvent));
  xTaskCreatePinnedToCore(hidTask, "hid_dispatch", 4096, nullptr, 5, nullptr, 1);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"uptime_ms\":%u,\"rssi\":%d,\"ip\":\"%s\",\"hid_ready\":%s,"
             "\"clients\":%u,\"controller\":%s,\"dropped\":%u,\"reports\":%u,"
             "\"heap\":%u,\"coalesce_ms\":%d,\"paired\":%s,\"pairing\":%s,"
             "\"device\":\"%s\",\"hostname\":\"%s\",\"template\":\"%s\",\"usb\":\"%s\"}",
             (unsigned)millis(), (int)wifiMgr::rssi(),
             wifiMgr::ip().c_str(), hid::isReady() ? "true" : "false",
             (unsigned)ws.count(), controllerId ? "true" : "false",
             (unsigned)dropped, (unsigned)reports,
             (unsigned)ESP.getFreeHeap(), (int)GOODUSB_COALESCE_MS,
             auth::isPaired() ? "true" : "false",
             auth::pairingOpen() ? "true" : "false",
             settings::deviceName().c_str(), settings::hostname().c_str(),
             settings::tplName().c_str(),
             settings::tplMouseOnly() ? "mouse" : "full");
    req->send(200, "application/json", buf);
  });
  server.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setCacheControl("no-cache");
  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "not found");
  });
  server.begin();
  GU_LOG("HTTP+WS server started");
}

void service() {
  ws.cleanupClients();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
    GU_LOG("restarting to apply settings");
    ESP.restart();
  }
  // Announce a freshly opened pairing window (e.g. BOOT hold) to sockets
  // that are already connected and waiting unauthenticated.
  bool pairing = auth::pairingOpen();
  if (pairing && !wasPairing) {
    for (auto &c : ws.getClients())
      if (!isAuthed(c.id())) c.text("{\"type\":\"pairing_open\"}");
  }
  wasPairing = pairing;

  led::setBase(pairing ? led::PAIRING
               : !wifiMgr::connected() ? led::WIFI_CONNECT
               : controllerId ? led::CONNECTED : led::READY);

  if (controllerId && !silenceLatched &&
      millis() - lastControllerSeen > GOODUSB_CLIENT_TIMEOUT_MS &&
      hid::anyHeld()) {
    GU_LOG("controller silent %u ms with keys held: releasing all",
           (unsigned)(millis() - lastControllerSeen));
    HidEvent e{}; e.type = EV_RELEASE_ALL; enqueue(e);
    silenceLatched = true;
  }
}

uint32_t clientCount() { return ws.count(); }
bool controllerPresent() { return controllerId != 0; }
uint32_t droppedFrames() { return dropped; }
uint32_t hidReports() { return reports; }
}  // namespace wsServer
