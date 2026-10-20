# GoodUSB — a Screen Mouse

Plug an ESP32-S3 into any screen — **PC first**, TV or phone later — and drive
its cursor from your phone's browser. No app to install, no cloud, no account.

```mermaid
flowchart LR
    subgraph controller [Phone Browser]
        App[Touchpad plus Template Shortcuts]
    end
    subgraph esp [ESP32-S3 GoodUSB]
        Auth[Pairing plus Challenge Handshake]
        Queue[HID Queue 8ms Coalesce 125Hz]
        HID[USB Mouse plus Shortcut Keys plus Media]
        LED[RGB LED Status]
    end
    Target[PC TV or Phone Screen]
    App -->|"Wi-Fi WebSocket binary"| Auth --> Queue --> HID
    HID -->|"Native USB GPIO19 20"| Target
```

## Quickstart

1. Set Wi-Fi for the first flash: `cp src/secrets.example.h src/secrets.h`
   and put your 2.4 GHz home Wi-Fi in `src/secrets.h` (gitignored).
2. Flash: `pio run -t upload && pio run -t uploadfs` (PlatformIO, board `esp32-s3-devkitc-1`).
3. Plug the ESP32-S3 **native USB port** (not the UART port) into the PC.
4. First boot: the LED pulses blue for 60 s — the pairing window.
5. On your phone (same Wi-Fi) open `http://goodusb.local` and create a
   4–8 digit PIN. Done — this phone is now the controller.

## Usage

**Connect.** Open `http://goodusb.local` (or the device IP shown in the
footer) in Safari/Chrome on the same Wi-Fi. The header shows connection
state, your role, and round-trip latency. Green dot = you control the
screen. If the page loads but says *offline*, check the LED (below) and
that phone and device share one network.

**Pair once.** First visit asks you to create a PIN while the LED pulses
blue. Later visits authenticate automatically with a stored token — no PIN
typing. Lost the phone or using a new one? Choose *Unlock*, enter the PIN,
and the old phone's token stops working.

**Drive the mouse.**
- **Touchpad:** drag to move (speed-sensitive acceleration), tap = left
  click, two-finger tap = right click, two-finger drag = scroll.
- **Joystick mode:** toggle *Mode*, then hold and tilt — cursor keeps
  moving while you hold. Good for couches and TVs.
- **Buttons:** Left / Mid / Right can be held (hold Left + drag = select).
- **Scroll strip:** the narrow bar on the right — drag up/down to scroll.
- **Sliders:** Sensitivity and Acceleration are saved per phone.

**Pick a template.** The template picker swaps the shortcut panel under
the mouse buttons: *PC Desktop*, *Presentation*, *Media/TV*, or *Phone*.
Your last pick is remembered on that phone.

**Share or take over.** A second paired phone connects as *observer* and
can watch latency/status; tap **Take over** to become the controller (the
previous controller's held buttons are released instantly — nothing gets
stuck).

**Keep awake.** *Keep awake* prevents the phone screen from sleeping while
you present or watch.

**Change settings.** *Device settings* (controller only): rename the
device (changes its USB name and `name.local` address), change Wi-Fi, or
open a fresh pairing window for another phone. Saved to the device and
applied on restart.

**If something is stuck.** Close the tab, lock the phone, or unplug the
device: any disconnect releases all mouse buttons and keys automatically,
and a 4 s silence watchdog does the same on Wi-Fi drops.

## Security (trusted-LAN model)

- Pairing window opens only when unpaired, on BOOT-button hold (3 s), or from
  the authenticated controller — never on every boot.
- The device stores `SHA256(salt ‖ PIN)` and `SHA256(token)` in NVS, never the PIN.
- Every connection answers a fresh nonce challenge
  (`SHA256hex(SHA256hex(token) ‖ ":" ‖ nonce)`); the token itself never
  crosses the LAN after pairing and replays are useless.
- Lost phone: enter the PIN to rotate to a new token (old phone is cut off).
  5 wrong PINs lock unlocks for 60 s.
- Honest limit: traffic is `ws://` (self-signed `wss://` breaks Safari), so
  this stops casual hijack, not a determined attacker sniffing pairing day.

## LED legend (GPIO48 RGB)

Amber boot · **blue pulse pairing** · yellow blink Wi-Fi connecting ·
green dim ready · green controller connected · white flash input ·
red blink error.

## Templates

Preset shortcut panels picked in the app (mouse pad is always present):
**PC Desktop** (arrows, Enter/Esc, Alt+Tab, Desktop, media), **Presentation**
(prev/next slide, Start, Black), **Media/TV** (OK, Back, Home, Play/Pause,
volume), **Phone** (Back, Home, media). Shortcuts use HID keyboard/media
reports under the hood — there is deliberately no typing keyboard.

## Settings (trust the user)

From the app's *Device settings*: device name (= USB + `name.local` network
name, so several GoodUSBs can share one Wi-Fi) and Wi-Fi SSID/password,
stored in NVS and applied on restart.

## Compatibility checklist

- [ ] Windows PC — mouse, buttons, scroll, PC template
- [ ] macOS PC — same (note: GUI+D = Mission Control differences per OS)
- [ ] Linux PC — same
- [ ] Android TV / Tizen / webOS — mouse + Media template (varies by vendor)
- [ ] Android / iPad as host (USB-OTG) — mouse; iOS needs pointer support

## Notes

- Serial logs are on UART0 (USB-CDC is off so the native port can be a mouse).
- Boot self-test is **off** by default (`GOODUSB_BOOT_SELF_TEST` in
  `src/config.h`) — a product never clicks things just because it was plugged in.
- Layout: `src/` firmware (`hid_composite`, `auth`, `settings`, `led_status`,
  `wifi_manager`, `ws_server`, `protocol.h`), `data/` web app, `partitions.csv`.
