'use strict';
/* GoodUSB client v2 — a Screen Mouse.
   Binary protocol v1 (src/protocol.h) for input; text JSON only for the
   pre-auth handshake, pairing, takeover and settings.
   Auth: token in localStorage; per-connection nonce challenge-response:
     proof = SHA256hex( SHA256hex(token) + ":" + nonce )
   SHA-256 is implemented locally because WebCrypto (crypto.subtle) is
   unavailable on plain-http LAN origins in Safari. */
const OP = { MOVE: 1, BUTTON: 2, KEY: 3, CONSUMER: 4, PING: 5, PONG: 6, RELEASE_ALL: 7 };
const $ = (s) => document.querySelector(s);

/* SHA256-START */
function sha256Hex(ascii) {
  function rr(v, a) { return (v >>> a) | (v << (32 - a)); }
  const maxWord = Math.pow(2, 32);
  const words = [];
  const bitLen = ascii.length * 8;
  let hash = sha256Hex.h = sha256Hex.h || [];
  const k = sha256Hex.k = sha256Hex.k || [];
  if (!k.length) {
    const isComposite = {};
    for (let candidate = 2, primeCounter = 0; primeCounter < 64; candidate++) {
      if (!isComposite[candidate]) {
        for (let i = 0; i < 313; i += candidate) isComposite[i] = candidate;
        hash[primeCounter] = (Math.pow(candidate, 0.5) * maxWord) | 0;
        k[primeCounter++] = (Math.pow(candidate, 1 / 3) * maxWord) | 0;
      }
    }
  }
  hash = hash.slice(0, 8);
  ascii += '\x80';
  while (ascii.length % 64 - 56) ascii += '\x00';
  for (let i = 0; i < ascii.length; i++) {
    const j = ascii.charCodeAt(i);
    if (j >> 8) throw new Error('sha256Hex: ASCII input only');
    words[i >> 2] |= j << ((3 - i) % 4) * 8;
  }
  words[words.length] = (bitLen / maxWord) | 0;
  words[words.length] = bitLen;
  for (let j = 0; j < words.length;) {
    const w = words.slice(j, j += 16);
    const old = hash.slice(0, 8);
    for (let i = 0; i < 64; i++) {
      const w15 = w[i - 15], w2 = w[i - 2], a = hash[0], e = hash[4];
      const t1 = (hash[7] + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) +
        ((e & hash[5]) ^ ((~e) & hash[6])) + k[i] +
        (w[i] = i < 16 ? w[i] : (w[i - 16] + (rr(w15, 7) ^ rr(w15, 18) ^ (w15 >>> 3)) +
          w[i - 7] + (rr(w2, 17) ^ rr(w2, 19) ^ (w2 >>> 10))) | 0)) | 0;
      const t2 = ((rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) +
        ((a & hash[1]) ^ (a & hash[2]) ^ (hash[1] & hash[2]))) | 0;
      hash = [(t1 + t2) | 0].concat(hash);
      hash[4] = (hash[4] + t1) | 0;
      hash.pop();
    }
    for (let i = 0; i < 8; i++) hash[i] = (hash[i] + old[i]) | 0;
  }
  let result = '';
  for (let i = 0; i < 8; i++)
    for (let j = 3; j >= 0; j--) {
      const b = (hash[i] >> (j * 8)) & 255;
      result += (b < 16 ? '0' : '') + b.toString(16);
    }
  return result;
}
/* SHA256-END */

let ws = null, role = 'offline', backoff = 500, pingTimer = null;
let authed = false, pinMode = 'pair';
let token = localStorage.getItem('gu.token') || '';
let btnMask = 0;
const settings = {
  sens: parseFloat(localStorage.getItem('gu.sens') ?? '1'),
  accel: parseFloat(localStorage.getItem('gu.accel') ?? '0.6'),
  tap: localStorage.getItem('gu.tap') !== '0',
  haptics: localStorage.getItem('gu.haptics') !== '0',
};
let mode = localStorage.getItem('gu.mode') || 'touchpad';

/* ---------- wire helpers ---------- */
function send(buf) { if (ws && ws.readyState === WebSocket.OPEN) ws.send(buf); }
function sendText(obj) { send(typeof obj === 'string' ? obj : JSON.stringify(obj)); }
function sendMove(dx, dy, wheel) {
  const b = new ArrayBuffer(6), v = new DataView(b);
  v.setUint8(0, OP.MOVE);
  v.setInt16(1, Math.max(-32768, Math.min(32767, dx | 0)), true);
  v.setInt16(3, Math.max(-32768, Math.min(32767, dy | 0)), true);
  v.setInt8(5, Math.max(-127, Math.min(127, wheel | 0)));
  send(b);
}
function sendButtons() { send(new Uint8Array([OP.BUTTON, btnMask & 0x1f])); }
function setBtn(bit, down) {
  const m = down ? (btnMask | bit) : (btnMask & ~bit);
  if (m !== btnMask) { btnMask = m; sendButtons(); }
}
function sendKey(key, down, mod = 0) { send(new Uint8Array([OP.KEY, mod, key, down ? 1 : 0])); }
function sendConsumer(usage, down) {
  const b = new ArrayBuffer(4), v = new DataView(b);
  v.setUint8(0, OP.CONSUMER); v.setUint16(1, usage, true); v.setUint8(3, down ? 1 : 0);
  send(b);
}
function releaseAll() { send(new Uint8Array([OP.RELEASE_ALL])); btnMask = 0; }
function buzz() { if (settings.haptics) navigator.vibrate?.(10); }
function click(bit = 1) { setBtn(bit, true); setTimeout(() => setBtn(bit, false), 70); buzz(); }

let pendX = 0, pendY = 0, pendWheel = 0;
setInterval(() => {
  if (pendX || pendY || pendWheel) {
    sendMove(pendX, pendY, pendWheel); pendX = pendY = pendWheel = 0;
  }
}, 8);
function queueMove(dx, dy, wheel = 0) { pendX += dx; pendY += dy; pendWheel += wheel; }
function gainFor(speedPx) {
  return settings.sens * (1 + settings.accel * Math.min(speedPx / 24, 2.5));
}

/* ---------- pairing / auth UI ---------- */
function showPair(m, title, hint) {
  pinMode = m;
  $('#pairPanel').classList.remove('hidden');
  $('#pairTitle').textContent = title;
  $('#pairHint').textContent = hint;
  $('#pinBtn').textContent = m === 'pair' ? 'Pair' : 'Unlock';
  $('#pinInput').placeholder = m === 'pair' ? 'Create a 4–8 digit PIN' : 'Enter your PIN';
}
function hidePair() { $('#pairPanel').classList.add('hidden'); }
$('#pinBtn').addEventListener('click', () => {
  const pin = $('#pinInput').value.trim();
  if (!/^\d{4,8}$/.test(pin)) { $('#pairHint').textContent = 'PIN must be 4–8 digits.'; return; }
  sendText({ type: pinMode, pin });
  $('#pairHint').textContent = 'Waiting for device…';
});

/* ---------- WebSocket + latency + reconnect ---------- */
function setRole(r) {
  role = r;
  const dot = $('#dot'), pill = $('#role');
  dot.className = 'dot ' + (r === 'controller' ? 'on' : r === 'observer' ? 'ctrl' : 'off');
  pill.textContent = r;
  $('#takeover').classList.toggle('hidden', !(authed && r === 'observer'));
}
function onHello(m) {
  if (!m.paired && m.pairing) {
    showPair('pair', 'Pair this phone',
      `First setup: create a PIN within ${m.pairing_left_s || 60}s (blue LED is pulsing).`);
  } else if (!m.paired) {
    showPair('pair', 'Pair this phone',
      'Pairing window is closed. Hold the device BOOT button 3s until the LED pulses blue, then reconnect.');
  } else if (!token) {
    showPair('unlock', 'Unlock this phone', 'This device is paired. Enter the PIN to give this phone a token.');
  } else {
    $('#pairHint').textContent = 'Authenticating…';
  }
}
function connect() {
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { backoff = 500; authed = false; setRole('connected'); };
  ws.onmessage = (ev) => {
    if (typeof ev.data === 'string') {
      let m; try { m = JSON.parse(ev.data); } catch { return; }
      switch (m.type) {
        case 'hello':
          onHello(m);
          if (m.profile) {
            activeProfile = m.profile;
            renderButtons(activeProfile.buttons || []);
            $('#tplActive').textContent =
              `Active on device: ${activeProfile.name} (${activeProfile.usb === 'mouse' ? 'simple mouse' : 'full HID'})`;
            refreshTplSelect(activeProfile.name);
            renderEditor();
          }
          break;
        case 'template_saved':
          $('#health').textContent = 'Template saved on device — restarting, reconnecting…';
          break;
        case 'template_fail':
          $('#health').textContent = 'Device rejected the template (name/size/buttons).';
          break;
        case 'challenge':
          if (token) sendText({ type: 'auth', proof: sha256Hex(sha256Hex(token) + ':' + m.nonce) });
          else showPair('unlock', 'Unlock this phone', 'Enter the PIN to give this phone a token.');
          break;
        case 'paired':
          token = m.token; localStorage.setItem('gu.token', token);
          $('#pairHint').textContent = 'Paired. Finishing handshake…';
          break;
        case 'auth_ok':
          authed = true; hidePair();
          clearInterval(pingTimer);
          pingTimer = setInterval(() => {
            const b = new ArrayBuffer(5), v = new DataView(b);
            v.setUint8(0, OP.PING);
            v.setUint32(1, (performance.now() & 0xffffffff) >>> 0, true);
            send(b);
          }, 1000);
          sendText({ type: 'getsettings' });
          break;
        case 'auth_fail':
          token = ''; localStorage.removeItem('gu.token');
          showPair('unlock', 'Unlock this phone', 'Saved token was rejected. Enter the PIN to rotate to a new token.');
          break;
        case 'pair_fail':
          $('#pairHint').textContent = m.why === 'window_closed'
            ? 'Window closed. Hold BOOT 3s (blue pulse), then reconnect and pair.'
            : 'Pairing failed: use a 4–8 digit PIN.';
          if (m.why === 'window_closed') pinMode = 'pair';
          break;
        case 'unlock_fail':
          $('#pairHint').textContent = m.why === 'locked'
            ? 'Too many wrong PINs. Wait 60s.' : 'Wrong PIN.';
          break;
        case 'pairing_open':
          if (!authed) showPair('pair', 'Pair this phone', 'Pairing window is open (60s). Create or reset your PIN.');
          break;
        case 'status': if (authed) setRole(m.role); break;
        case 'settings':
          $('#setDevice').value = m.device || '';
          $('#setSsid').value = m.ssid || '';
          break;
        case 'settings_saved':
          $('#health').textContent = m.restart ? 'Settings saved — device restarting…' : 'Settings unchanged.';
          break;
      }
      return;
    }
    const v = new DataView(ev.data);
    if (v.byteLength >= 9 && v.getUint8(0) === OP.PONG) {
      const ts = v.getUint32(1, true);
      const now = (performance.now() & 0xffffffff) >>> 0;
      $('#latency').textContent = `${(now - ts) >>> 0} ms`;
    }
  };
  ws.onclose = ws.onerror = () => {
    clearInterval(pingTimer);
    authed = false;
    releaseAllLocal();
    setRole('offline');
    setTimeout(connect, backoff);
    backoff = Math.min(backoff * 2, 5000);
  };
}
function releaseAllLocal() { btnMask = 0; if (ws) releaseAll(); }
$('#takeover').addEventListener('click', () => sendText('takeover'));

window.addEventListener('blur', () => releaseAll());
document.addEventListener('visibilitychange', () => {
  if (document.hidden) releaseAll();
  else reacquireWakeLock();
});
window.addEventListener('beforeunload', () => releaseAll());

/* ---------- touchpad ---------- */
const pad = $('#pad');
const pointers = new Map();
let downAt = 0, moved = 0, twoFingerTap = false;

pad.addEventListener('pointerdown', (e) => {
  pad.setPointerCapture(e.pointerId);
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  if (pointers.size === 1) { downAt = performance.now(); moved = 0; }
  if (pointers.size === 2) twoFingerTap = true;
  if (mode === 'joystick') joyStart(e);
});
pad.addEventListener('pointermove', (e) => {
  const p = pointers.get(e.pointerId);
  if (!p) return;
  const dx = e.clientX - p.x, dy = e.clientY - p.y;
  p.x = e.clientX; p.y = e.clientY;
  moved += Math.abs(dx) + Math.abs(dy);
  if (mode === 'joystick') { joyMove(e); return; }
  if (pointers.size === 1) {
    const g = gainFor(Math.hypot(dx, dy));
    queueMove(dx * g, dy * g);
  } else if (pointers.size === 2) {
    queueMove(0, 0, -Math.sign(dy) * Math.max(1, Math.round(Math.abs(dy) / 14)));
  }
});
function padEnd(e) {
  const wasTwo = pointers.size >= 2;
  pointers.delete(e.pointerId);
  if (mode === 'joystick') { joyEnd(); return; }
  if (!wasTwo && pointers.size === 0 && settings.tap) {
    const dt = performance.now() - downAt;
    if (dt < 250 && moved < 10) click(1);
  } else if (wasTwo && pointers.size === 0 && twoFingerTap && moved < 14) {
    click(2); twoFingerTap = false;
  }
}
pad.addEventListener('pointerup', padEnd);
pad.addEventListener('pointercancel', (e) => { padEnd(e); releaseAll(); });

const strip = $('#scrollStrip');
let stripY = null, stripAcc = 0;
strip.addEventListener('pointerdown', (e) => { strip.setPointerCapture(e.pointerId); stripY = e.clientY; });
strip.addEventListener('pointermove', (e) => {
  if (stripY === null) return;
  stripAcc += e.clientY - stripY; stripY = e.clientY;
  const steps = Math.trunc(stripAcc / 12);
  if (steps) { queueMove(0, 0, -steps); stripAcc -= steps * 12; }
});
strip.addEventListener('pointerup', () => { stripY = null; });
strip.addEventListener('pointercancel', () => { stripY = null; releaseAll(); });

/* ---------- joystick mode ---------- */
const joyBase = $('#joyBase'), joyKnob = $('#joyKnob');
let joyVec = { x: 0, y: 0 }, joyOrigin = null, joyRaf = null, joyLast = 0;
function joyStart(e) {
  joyOrigin = { x: e.clientX, y: e.clientY };
  joyBase.classList.remove('hidden');
  joyBase.style.left = e.clientX - pad.offsetLeft + 'px';
  joyBase.style.top = e.clientY - pad.offsetTop + 'px';
  joyLast = performance.now();
  const step = (t) => {
    const dt = Math.min((t - joyLast) / 1000, 0.05); joyLast = t;
    if (joyVec.x || joyVec.y) {
      const speed = 850 * settings.sens;
      queueMove(joyVec.x * speed * dt, joyVec.y * speed * dt);
    }
    joyRaf = requestAnimationFrame(step);
  };
  joyRaf = requestAnimationFrame(step);
}
function joyMove(e) {
  if (!joyOrigin) return;
  let dx = e.clientX - joyOrigin.x, dy = e.clientY - joyOrigin.y;
  const len = Math.hypot(dx, dy), max = 52;
  if (len > max) { dx *= max / len; dy *= max / len; }
  joyVec = { x: dx / max, y: dy / max };
  joyKnob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
}
function joyEnd() {
  joyVec = { x: 0, y: 0 }; joyOrigin = null;
  joyBase.classList.add('hidden');
  joyKnob.style.transform = 'translate(-50%,-50%)';
  if (joyRaf) cancelAnimationFrame(joyRaf); joyRaf = null;
}
$('#modeBtn').addEventListener('click', () => {
  mode = mode === 'touchpad' ? 'joystick' : 'touchpad';
  localStorage.setItem('gu.mode', mode);
  $('#modeBtn').textContent = 'Mode: ' + (mode === 'touchpad' ? 'Touchpad' : 'Joystick');
  $('#padHint').textContent = mode;
  joyEnd();
});
if (mode === 'joystick') { $('#modeBtn').textContent = 'Mode: Joystick'; $('#padHint').textContent = 'joystick'; }

/* ---------- mouse buttons (hold to drag) ---------- */
document.querySelectorAll('.mbtn').forEach((el) => {
  const bit = parseInt(el.dataset.btn, 10);
  el.addEventListener('pointerdown', (e) => { e.preventDefault(); el.classList.add('held'); setBtn(bit, true); });
  const up = () => { el.classList.remove('held'); setBtn(bit, false); };
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', () => { up(); releaseAll(); });
  el.addEventListener('lostpointercapture', up);
});

/* ---------- template presets (shortcut panels, data-driven) ----------
   kind key: HID keyboard usage (+mod bits) — shortcuts only, no typing.
   kind consumer: HID consumer usage. kind mouse: mouse button bit. */
const MOD = { CTRL: 0x01, SHIFT: 0x02, ALT: 0x04, GUI: 0x08 };
const TEMPLATES = {
  pc: { label: 'PC', usb: 'mouse', buttons: [] },   // simple mouse, nothing else
  presentation: { label: 'Presentation', usb: 'full', buttons: [
    { label: '◀ Prev', kind: 'key', key: 0x4b }, { label: 'Next ▶', kind: 'key', key: 0x4e },
    { label: 'Start', kind: 'key', key: 0x4a }, { label: 'Esc', kind: 'key', key: 0x29 },
    { label: 'Black (B)', kind: 'key', key: 0x05 }, { label: 'Enter', kind: 'key', key: 0x28 },
    { label: '▲', kind: 'key', key: 0x52 }, { label: '▼', kind: 'key', key: 0x51 },
    { label: 'Vol−', kind: 'consumer', usage: 0x00ea },
    { label: 'Vol+', kind: 'consumer', usage: 0x00e9 },
  ]},
  media: { label: 'Media / TV', usb: 'full', buttons: [
    { label: '▲', kind: 'key', key: 0x52 }, { label: '▼', kind: 'key', key: 0x51 },
    { label: '◀', kind: 'key', key: 0x50 }, { label: '▶', kind: 'key', key: 0x4f },
    { label: 'OK', kind: 'key', key: 0x28 }, { label: 'Esc', kind: 'key', key: 0x29 },
    { label: '↩ Back', kind: 'consumer', usage: 0x0224 },
    { label: '⌂ Home', kind: 'consumer', usage: 0x0223 },
    { label: '⏮', kind: 'consumer', usage: 0x00b6 },
    { label: '⏯', kind: 'consumer', usage: 0x00cd },
    { label: '⏭', kind: 'consumer', usage: 0x00b5 },
    { label: '🔇', kind: 'consumer', usage: 0x00e2 },
    { label: 'Vol−', kind: 'consumer', usage: 0x00ea },
    { label: 'Vol+', kind: 'consumer', usage: 0x00e9 },
  ]},
  phone: { label: 'Phone', usb: 'full', buttons: [
    { label: '▲', kind: 'key', key: 0x52 }, { label: '▼', kind: 'key', key: 0x51 },
    { label: '◀', kind: 'key', key: 0x50 }, { label: '▶', kind: 'key', key: 0x4f },
    { label: 'Enter', kind: 'key', key: 0x28 }, { label: 'Esc', kind: 'key', key: 0x29 },
    { label: '↩ Back', kind: 'consumer', usage: 0x0224 },
    { label: '⌂ Home', kind: 'consumer', usage: 0x0223 },
    { label: '⏯', kind: 'consumer', usage: 0x00cd },
    { label: 'Vol−', kind: 'consumer', usage: 0x00ea },
    { label: 'Vol+', kind: 'consumer', usage: 0x00e9 },
    { label: 'Mute', kind: 'consumer', usage: 0x00e2 },
  ]},
};
function bindHold(el, down, up) {
  el.addEventListener('pointerdown', (e) => { e.preventDefault(); el.classList.add('held'); down(); });
  const end = () => { el.classList.remove('held'); up(); };
  el.addEventListener('pointerup', end);
  el.addEventListener('pointercancel', () => { end(); releaseAll(); });
  el.addEventListener('lostpointercapture', end);
}
/* Editable template library. Factory defaults are only starting points:
   the library lives in localStorage ('gu.tpllib') and every entry can be
   edited/cloned/deleted. The ACTIVE profile lives on the device (NVS):
   Apply sends it over, the ESP saves it and restarts (USB re-enumerates),
   and on reconnect the device reports its active profile in "hello",
   which is what the button panel renders. */
const FACTORY_TEMPLATES = JSON.parse(JSON.stringify(TEMPLATES));
let tplLib;
try { tplLib = JSON.parse(localStorage.getItem('gu.tpllib')) || null; } catch { tplLib = null; }
if (!tplLib || !Object.keys(tplLib).length) tplLib = JSON.parse(JSON.stringify(FACTORY_TEMPLATES));
let activeProfile = { name: 'PC', usb: 'mouse', buttons: [] };

function saveLib() { localStorage.setItem('gu.tpllib', JSON.stringify(tplLib)); }
function renderButtons(buttons) {
  const panel = $('#templatePanel');
  panel.innerHTML = '';
  panel.style.display = buttons.length ? '' : 'none';
  for (const b of buttons) {
    const el = document.createElement('button');
    el.className = 'kbtn';
    el.textContent = b.label;
    if (b.kind === 'key') bindHold(el, () => sendKey(b.key, true, b.mod || 0), () => sendKey(b.key, false, b.mod || 0));
    else if (b.kind === 'consumer') bindHold(el, () => sendConsumer(b.usage, true), () => { sendConsumer(b.usage, false); buzz(); });
    else if (b.kind === 'mouse') bindHold(el, () => setBtn(b.btn, true), () => setBtn(b.btn, false));
    panel.appendChild(el);
  }
}
function refreshTplSelect(selectedName) {
  const sel = $('#templateSel');
  sel.innerHTML = '';
  for (const t of Object.values(tplLib)) {
    const o = document.createElement('option');
    o.value = t.label; o.textContent = t.label;
    sel.appendChild(o);
  }
  if (selectedName && tplLib[slug(selectedName)]) sel.value = tplLib[slug(selectedName)].label;
}
function slug(s) { return s.toLowerCase().replace(/[^a-z0-9]+/g, '-'); }
function selectedTpl() { return tplLib[slug($('#templateSel').value)] || Object.values(tplLib)[0]; }

function renderEditor() {
  const t = selectedTpl();
  if (!t) return;
  $('#teName').value = t.label;
  $('#teUsb').value = t.usb || 'full';
  const box = $('#teButtons');
  box.innerHTML = '';
  (t.buttons || []).forEach((b) => {
    const row = document.createElement('div');
    row.className = 'row te-row';
    const val = b.kind === 'key' ? b.key : b.kind === 'consumer' ? b.usage : b.btn;
    row.innerHTML =
      `<input class="te-label" value="${b.label}" maxlength="12">` +
      `<select class="te-kind"><option value="key">key</option><option value="consumer">media</option><option value="mouse">mouse</option></select>` +
      `<input class="te-val" value="0x${(val || 0).toString(16)}">` +
      `<input class="te-mod" value="${b.mod || 0}" title="mod bits">` +
      `<button class="pill btn te-del" type="button">✕</button>`;
    row.querySelector('.te-kind').value = b.kind;
    box.appendChild(row);
  });
}
function readEditor() {
  const buttons = [];
  document.querySelectorAll('.te-row').forEach((row) => {
    const kind = row.querySelector('.te-kind').value;
    const val = parseInt(row.querySelector('.te-val').value) || 0;
    const b = { label: row.querySelector('.te-label').value || '?', kind };
    if (kind === 'key') { b.key = val; b.mod = parseInt(row.querySelector('.te-mod').value) || 0; }
    else if (kind === 'consumer') b.usage = val;
    else b.btn = val;
    buttons.push(b);
  });
  return { label: $('#teName').value.trim() || 'Template', usb: $('#teUsb').value, buttons };
}
$('#templateSel').addEventListener('change', () => { renderEditor(); buzz(); });
$('#teAdd').addEventListener('click', () => {
  const t = selectedTpl();
  t.buttons = t.buttons || [];
  t.buttons.push({ label: 'New', kind: 'key', key: 0x28 });
  saveLib(); renderEditor();
});
document.addEventListener('click', (e) => {
  if (!e.target.classList?.contains('te-del')) return;
  const t = readEditor();
  const idx = [...document.querySelectorAll('.te-del')].indexOf(e.target);
  t.buttons.splice(idx, 1);
  tplLib[slug(t.label)] = t; saveLib(); renderEditor();
});
$('#teSave').addEventListener('click', () => {
  const t = readEditor();
  const old = selectedTpl();
  if (old && slug(old.label) !== slug(t.label)) delete tplLib[slug(old.label)];
  tplLib[slug(t.label)] = t;
  saveLib(); refreshTplSelect(t.label); renderEditor();
  $('#health').textContent = `Template "${t.label}" saved locally. Tap Apply to send it to the device.`;
});
$('#teClone').addEventListener('click', () => {
  const t = readEditor();
  t.label = t.label + ' copy';
  tplLib[slug(t.label)] = t;
  saveLib(); refreshTplSelect(t.label); renderEditor();
});
$('#teDelete').addEventListener('click', () => {
  const t = selectedTpl();
  if (!t || Object.keys(tplLib).length <= 1) return;
  delete tplLib[slug(t.label)];
  saveLib(); refreshTplSelect(); renderEditor();
});
$('#teFactory').addEventListener('click', () => {
  tplLib = JSON.parse(JSON.stringify(FACTORY_TEMPLATES));
  saveLib(); refreshTplSelect('PC Desktop'); renderEditor();
});
$('#tplApply').addEventListener('click', () => {
  const t = readEditor();   // apply exactly what is shown in the editor
  sendText({ type: 'applytemplate', name: t.label, usb: t.usb, buttons: t.buttons });
  $('#health').textContent = `Applying "${t.label}" — device restarting, USB re-enumerating…`;
  buzz();
});
refreshTplSelect('PC');
renderEditor();
renderButtons(activeProfile.buttons);

/* ---------- device settings (controller only, server enforces) ---------- */
$('#setSave').addEventListener('click', () => {
  const msg = { type: 'settings' };
  const device = $('#setDevice').value.trim();
  const ssid = $('#setSsid').value.trim();
  if (device) msg.device = device;
  if (ssid) { msg.ssid = ssid; msg.wpass = $('#setPass').value; }
  if (!msg.device && !msg.ssid) { $('#health').textContent = 'Nothing to save.'; return; }
  sendText(msg);
});
$('#openPair').addEventListener('click', () => sendText({ type: 'openpair' }));

/* ---------- settings ---------- */
function bindSetting(id, valId, key, apply) {
  const el = $(id);
  if (el.type === 'range') {
    el.value = settings[key];
    if (valId) $(valId).textContent = Number(settings[key]).toFixed(2);
  } else el.checked = !!settings[key];
  el.addEventListener('input', () => {
    if (el.type === 'range') {
      settings[key] = parseFloat(el.value);
      $(valId).textContent = settings[key].toFixed(2);
      localStorage.setItem('gu.' + apply, String(settings[key]));
    } else {
      settings[key] = el.checked;
      localStorage.setItem('gu.' + apply, el.checked ? '1' : '0');
    }
  });
}
bindSetting('#sens', '#sensVal', 'sens', 'sens');
bindSetting('#accel', '#accelVal', 'accel', 'accel');
bindSetting('#tapClick', null, 'tap', 'tap');
bindSetting('#haptics', null, 'haptics', 'haptics');

/* ---------- Wake Lock ---------- */
let wakeLock = null, wakeWanted = false;
async function reacquireWakeLock() {
  if (!wakeWanted || document.hidden || !('wakeLock' in navigator)) return;
  try { wakeLock = await navigator.wakeLock.request('screen'); } catch { /* denied */ }
}
$('#wakelock').addEventListener('click', async () => {
  wakeWanted = !wakeWanted;
  $('#wakelock').textContent = wakeWanted ? '☀ Awake on' : '☀ Keep awake';
  if (wakeWanted) reacquireWakeLock();
  else { await wakeLock?.release(); wakeLock = null; }
});

/* ---------- health footer ---------- */
async function pollHealth() {
  try {
    const h = await (await fetch('/api/health', { cache: 'no-store' })).json();
    $('#health').textContent =
      `${h.device} · ${h.ip} · RSSI ${h.rssi} dBm · HID ${h.hid_ready ? 'ready' : 'waiting'} · ` +
      `${h.paired ? 'paired' : 'UNPAIRED'} · clients ${h.clients} · heap ${Math.round(h.heap / 1024)} KB`;
  } catch { $('#health').textContent = 'health unavailable'; }
}
setInterval(pollHealth, 5000); pollHealth();

connect();
