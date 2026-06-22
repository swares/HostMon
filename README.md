# Host Monitor — Waveshare ESP32-S3-Touch-LCD-4.3

A self-hosted network-monitoring appliance for the **Waveshare ESP32-S3-Touch-LCD-4.3**
(800×480 parallel-RGB capacitive touch panel). It runs six pluggable checks against a
list of hosts, shows fleet health on the on-device LCD, and serves a **web dashboard
over plain HTTP (behind HTTP Basic Auth)** straight from firmware. It supports webhook
alerting and pause / acknowledge governance with required reasons.

Everything persists **on-board** — no SD card and no separate data upload: the host list
lives on the flash filesystem (LittleFS) and the dashboard assets are compiled into the
firmware. This is an **Arduino IDE** project (a flat sketch — Arduino builds every
`.ino/.cpp/.h` in the folder).

Firmware version: **1.4.22**.

---

## 1. Hardware

- Waveshare ESP32-S3-Touch-LCD-4.3 — ESP32-S3, **8 MB OPI PSRAM**, **8 MB flash**
  (some board revisions ship 16 MB flash; match your **Flash Size** + partition table
  to the actual chip — see §4).
- That's it. **No SD card or RTC battery is required** for normal operation.

The 4.3″ panel is **parallel RGB (ST7262/EK9716), not SPI**. Two things must happen
before any pixels render (this is where naïve attempts fail):

1. The **CH422G I/O expander** (I²C @ 0x20) must be initialised *first* — it drives LCD
   reset, the backlight, and the GT911 touch reset. `ESP32_Display_Panel` does this
   automatically **once you select the Waveshare board in its config** (§3).
2. **PSRAM (OPI 8 MB)** must be enabled, or the 768 KB RGB framebuffer won't allocate.

> **I²C note:** the GT911 touch + CH422G expander run on the panel library's *legacy*
> IDF I²C driver. Arduino's `Wire` pulls in the *new* `driver_ng`, and linking both
> aborts at boot (`check_i2c_driver_conflict`). This firmware therefore stays
> `Wire`-free. A consequence: the on-board **PCF85063 RTC is currently not used** (it
> shares that bus); time comes from **NTP**, falling back to a runtime/uptime clock.

---

## 2. Install the ESP32 board package

Arduino IDE → Preferences → *Additional boards manager URLs*:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Boards Manager → **esp32 by Espressif → `3.3.8`** (the version this code is built
against; it relies on the IDF v5 / core 3.x APIs).

---

## 3. Install libraries

Built/tested with these versions (Library Manager unless noted):

| Library | Version | Notes |
|---|---|---|
| **lvgl** | `8.4.x` | Graphics. **v9 will not compile** against this code. |
| **ESP32_Display_Panel** (Espressif) | `1.0.4` | Drives CH422G + RGB bus + GT911. |
| **ESP32_IO_Expander** (Espressif) | `1.1.1` | Dependency of ESP32_Display_Panel. |
| **esp-lib-utils** (Espressif) | `0.3.0` | Dependency of the panel stack. |
| **ESP32_IDF5_HTTPS_Server** | `1.1.1` | Used only for its **HTTP** server + Basic Auth (`HTTPServer`). An IDF5-compatible fork of `fhessel/esp32_https_server` (the original doesn't build on core 3.x). The library's HTTPS/TLS classes are **not used** — on-device TLS isn't viable here (§7). |
| **ArduinoJson** | `7.4.x` | JSON API. (Code uses the `DynamicJsonDocument` API, which still works in v7.) |

> **ESP Mail Client is no longer required** — SMTP/email alerting was removed (its TLS
> footprint exceeded this board's free internal RAM; §10/§16). You can uninstall it.

`LittleFS`, `WiFi`, `HTTPClient`, `WiFiClientSecure`, `ESPmDNS`, `Preferences`, the
esp_ping/lwIP stack, and **mbedTLS** ship with the core. (mbedTLS is pulled in by
`WiFiClientSecure` for HTTPS host checks and outbound notifications, both unverified — §7.)

### lv_conf.h (required)
Copy **`lv_conf.h`** from this sketch folder into your Arduino **`libraries/`** folder —
it must sit *next to* the `lvgl` folder (`…/Arduino/libraries/lv_conf.h`). This build
sets `LV_MEM_CUSTOM=1` and allocates the LVGL pool in **PSRAM**, which frees internal
RAM for Wi-Fi and the network checks — don't skip it.

### ESP32_Display_Panel board selection (required)
This is the single most important setup step. Place a file named
**`esp_panel_board_supported_conf.h`** at your Arduino **`libraries/` root** (next to
`lv_conf.h`) containing:

```c
#define ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED   (1)
#define BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3
```

The library locates this config via `__has_include("../../../esp_panel_board_supported_conf.h")`,
so it must live at the libraries root, **not** inside the library folder. Without the
board selected, `Board::init()` fails and the screen stays black (backlight on).

---

## 4. Arduino IDE board settings (Tools menu)

- **Board:** ESP32S3 Dev Module
- **PSRAM:** **OPI PSRAM** ← mandatory
- **Flash Size:** **8MB (64Mb)** — or 16MB if your board has it
- **Partition Scheme:** **Custom** → use the bundled **`partitions.csv`** (5 MB app /
  ~2.9 MB LittleFS / coredump, 8 MB total). The app build is large (LVGL + HTTP server +
  mbedTLS + Display Panel), so a normal "default" scheme won't fit.
- **Upload Speed:** 921600
- **USB CDC On Boot:** Enabled (for the serial log; note serial is unreliable once the
  RGB panel + Wi-Fi are both live — the device surfaces diagnostics on the LCD instead)

> The LittleFS partition is auto-formatted on first boot. **You do not upload a `data/`
> image** — see §5.

---

## 5. Build & flash (the dashboard is embedded — no data upload)

The web dashboard is **compiled into the firmware**: the files under `data/` are gzipped
into **`web_assets.h`** and served from flash by the API's static handler. So the build
is just:

1. Open `HostMonitor.ino` in Arduino IDE, apply the Tools settings above.
2. **Sketch → Upload** (or *Export Compiled Binary* then flash with esptool).

Nothing needs to go on an SD card or a LittleFS image.

### If you edit the dashboard (`data/…`)
Regenerate `web_assets.h` after changing any HTML/CSS/JS in `data/`. The header is a
table of gzipped blobs plus a `WEB_ASSETS[]` index; rebuild it with your generator of
choice (the repo's tooling gzips each file under `data/` and emits the byte arrays).
Then rebuild the sketch.

### Clean-rebuild note
Changing a **header** (`config.h`, `model.h`, `settings.h`, …) or `web_assets.h` can
leave a stale object in Arduino's build cache. If a change "doesn't take", delete
`…/AppData/Local/arduino/sketches/*` and rebuild. The **Alerts / Setup** card shows a
`built HH:MM:SS` marker so you can confirm the running binary is current.

---

## 6. First run

1. With no saved Wi-Fi, the device raises an **open captive AP `HostMon`**.
2. Join `HostMon`; the setup page at `http://192.168.4.1` opens (plain HTTP, so the
   captive detector works).
3. Pick your network, enter the password → the device **reboots onto your LAN**.
   (There's no "stay an access point" mode; the appliance is meant to live on your LAN.
   AP mode is only the no-credentials fallback.)
4. Open the dashboard at **`http://monitor.local/`** (or the IP shown on the LCD). It's
   HTTP only — see §7 for how to add TLS in front of the device.

### Web login — auto-generated password
On first boot the device generates a **random per-device web password** and shows it on
the **Alerts / Setup** card as `Web login: admin / …`. Read it off the screen to log in.
There is **no shared default password.** Set your own in the dashboard (Settings → it
must be 8–39 chars); the LCD line disappears once you do.

### Optional: bake Wi-Fi credentials into the firmware
For a fixed deployment you can skip the AP wizard entirely. In `config.h`:

```c
#define WIFI_SSID_BUILTIN  "my-ssid"
#define WIFI_PASS_BUILTIN  "my-pass"
```

If `WIFI_SSID_BUILTIN` is non-empty the device joins that network on every boot and
ignores saved/AP credentials. (Baked-in credentials are readable from the firmware
image — fine for a controlled deployment, just be aware.)

---

## 7. Transport / TLS

The dashboard serves over **plain HTTP** only (`http://monitor.local/` or the IP on the
LCD). **On-device HTTPS was removed** — it isn't viable on this hardware: an mbedTLS
connection needs ~16–32 KB of *contiguous internal* RAM, which the board can't reliably
spare alongside Wi-Fi + the check engine + the web server. With it enabled the TLS
listener came up but **handshakes reset** (`ERR_CONNECTION_RESET`, "no certificate")
regardless of cert type — confirmed with both RSA and ECDSA. Shrinking the mbedTLS
buffers needs a core rebuild Arduino IDE doesn't expose, so the TLS server, the cert
provisioning, and the embedded cert/key were all deleted from the firmware.

### Getting real TLS
Terminate it **in front of** the device — the standard pattern for an HTTP-only embedded
appliance:

- A **reverse proxy** with a proper certificate — **Caddy** (automatic HTTPS), **nginx**,
  or **Traefik** on a Raspberry Pi / NAS / OpenWrt router — listening on 443 and
  forwarding to `http://<device-ip>/`. You get a real green-lock cert; the device just
  speaks HTTP behind it.
- Or keep the device on a **trusted / isolated VLAN** and accept plain HTTP there.

### Outbound TLS: one insecure session at a time
A *single* outbound mbedTLS session (~44 KB here, peak) does fit, now that internal RAM
was reclaimed (smaller `MAX_HOSTS`, the removed cert cruft) — confirmed by the boot-time
self-test (§16). So **one TLS session at a time** is viable, and the firmware uses it for:

- The **SSL cert-expiry check** (§9): an **insecure** handshake that reads the cert's
  `notAfter` and reports days-to-expiry. Insecure is deliberate — it only needs the date,
  and the next item explains why *verified* TLS is still off the table.
- **HTTPS host checks** and the **webhook notifier** (HTTPS POST/PUT): the transport is
  encrypted but the server certificate is **not validated**.

What's still **not** available is **verified** on-device TLS (chain validation against the
Mozilla CA bundle). Attaching + parsing that bundle costs materially more RAM than the bare
insecure session that was measured to fit, so there's no "verify" option anywhere in the UI.
For verified TLS or anything needing *several* concurrent sessions (e.g. a real HTTPS
dashboard server, where one page load opens multiple connections), use the reverse proxy.

Only one session fits, so a global gate (§10) serializes every outbound TLS site across the
check and web tasks. Ping, DNS, port, and traceroute don't touch TLS at all.

Because the dashboard is HTTP, the Basic-Auth password traverses the network in cleartext
on an untrusted segment — the proxy or VLAN above is what protects it. (The web password
is still random per-device, so there's no shared default to guess.)

---

## 8. Host list & persistence

Hosts are stored on the on-flash LittleFS as **`/hosts.csv`** (auto-created). You
normally manage hosts from the dashboard, which writes the file back; you rarely touch
it directly. Header row required, one host per line:

```
name,address,group,checks,intervals,alerts
```

- `checks` — pipe list of enabled checks. Keys: `ping | dns | port | http | https | ssl | trace`.
  - `http` = HTTP check over plaintext; `https` = over TLS, **always insecure** (accepts
    any/self-signed cert — see §7 for why there's no verified mode).
  - `ssl` = cert-expiry check (insecure TLS handshake, reads `notAfter`).
  - Legacy tokens from older files are accepted on load and normalised: `httpsv` → `https`
    and `sslv` → `ssl` (verified modes aren't supported — §7). On the next save the file is
    rewritten with the current keys.
- `intervals` — optional `key:seconds` overrides, e.g. `ping:30|http:60`
  (seconds must be one of 10, 30, 60, 120, 300, 900, 3600, 21600, 43200, 86400).
- `alerts` — optional pipe list from `down | warn | recovered` (default `down|recovered`).

Every row is **validated** on load; malformed rows are skipped and logged. New hosts
default to **ping only** (enable other checks per host in the dashboard).

> Persistence is robust against the boot race: hosts are loaded *before* the web server
> starts, and `saveHosts()` refuses to run until the load has happened, so a request
> arriving during boot can't overwrite the file with an empty one.

---

## 9. The six checks

| Check | What it does |
|---|---|
| **Ping** | ICMP reachability + packet-loss % (esp_ping). |
| **DNS** | Resolves the hostname and times it. |
| **Port** | TCP connect to a port (set per host; default 80). Bounded by `CONNECT_TIMEOUT_MS`. |
| **HTTP** | GET the host, expect 2xx/3xx. Per-host **HTTP/HTTPS** scheme (HTTPS is always insecure — accepts any cert; see §7); HTTPS works on non-standard ports. |
| **SSL** | TLS handshake → reads the peer-cert `notAfter` and reports days-to-expiry (warns under `SSL_WARN_DAYS`, default 14; DOWN once expired). **Insecure** (accepts any cert — it only reads the expiry date, see §7). Port per host (default 443). One TLS session at a time via the gate (§10). **Use the hostname, not an IP** — the cert is selected by SNI, so an IP gets the server's *default* cert (often a different/older one), not the cert for that name. |
| **Trace** | Reachability + hop-count estimate from the reply TTL. |

The check engine runs on its own (statically-allocated) task pinned to core 0, away from
the LVGL display loop on core 1. The **Alerts / Setup** card shows load metrics —
`queue=` (overdue checks), `timeouts=`, `scan=…ms` — to spot overload.

---

## 10. Alerting

**Webhook only** (JSON POST/PUT). Per-event routing (`down / warn / recovered`, plus
`ack / paused`) and re-notify. Alert payloads are built with ArduinoJson (properly
escaped). Each fired alert is also recorded in the on-device ring buffer for the
dashboard + LCD.

**Payload format** (Settings → Webhook → *Payload format*): pick **Native** or
**M5Stack inject** per webhook.

- *Native* (default):
  `{"event":"host.down","host":…,"address":…,"status":"DOWN","check":…,"message":…,"fails":3,"since":…,"device":…}`
- *M5Stack inject*: the schema the **M5Stack Device Framework**'s `POST /api/alerts/inject`
  endpoint expects — `{"slug":"<device>","key":"<host>","value":<fails>,"severity":"critical|warn|info"}`.
  Point the webhook at `https://<m5-ip>/api/alerts/inject`, put the M5Stack's `X-API-Key:
  <key>` in the **Custom header** field, and HostMonitor alerts flow straight into that
  device's alert engine (LCD/MQTT/SD/buzzer/etc.) with no relay. The format is stored in its
  own NVS key, so switching it never disturbs your saved webhook URL/routing.

> **Email/SMTP was removed.** ESP Mail Client's TLS session needed more internal RAM than
> this board can spare (it was the heaviest TLS user — see §16). If you want email, point a
> webhook at a downstream relay that emails for you (ntfy, Home Assistant, a webhook→email
> gateway, Zapier, etc.). The whole alert engine — routing, re-notify, ring buffer,
> pause/ack governance — is unchanged; only the SMTP delivery channel is gone.

**Outbound TLS:** HTTPS webhooks are delivered **without on-device certificate
verification** — validating a server cert needs the CA bundle, which costs more RAM than
this board can spare (§7). The transport is still encrypted by `WiFiClientSecure`, but the
server's identity is **not** checked, so prefer endpoints reached over a trusted network
(or via your reverse proxy). There is no "verify TLS cert" toggle.

**One TLS session at a time, with a heap guard.** Only one mbedTLS session (~44 KB peak)
fits in internal RAM, and outbound TLS can originate from two tasks — the check engine
(HTTPS host checks, the SSL cert check, the boot self-test, alert webhooks) and the web
server (the dashboard's "Send test"). A global mutex (`tls_gate.*`) serializes them so two
sessions can never allocate at once. **Before** starting a session the gate also checks the
heap: the largest free *contiguous* block must be ≥ `TLS_MIN_FREE_BLOCK` (default 20 KB) **and**
total free internal RAM ≥ `TLS_MIN_FREE_TOTAL` (default 48 KB, sized just above a session's
~44 KB so it only blocks one that couldn't fit anyway). If RAM is too tight it **skips that
TLS op for the cycle** rather than risk an out-of-memory fault — reported as `low mem` on the
LCD / a deferred webhook (re-notify retries later). If a TLS site can't get the slot within
~15 s it instead reports `tls busy`. Raise `TLS_MIN_FREE_TOTAL` (54–60 KB) for a guaranteed
higher floor at the cost of TLS checks deferring more under load.
Watch `min` on the Setup card's RAM gauge as you scale; if it trends toward single digits,
the guard is what keeps a depleted moment from crashing the device.

---

## 11. Web / JSON API (HTTP, Basic Auth)

```
GET  /api/summary               fleet counts + device/net + clock
GET  /api/hosts                 all hosts
GET  /api/host?id=h1            one host
GET  /api/alerts                recent alerts
GET  /api/settings              webhook/defaults/device
GET  /api/status                {ap, online, ip, ssid}
GET  /api/wifi/scan             nearby networks
POST /api/host/ack              {id, reason, who?}          (reason required)
POST /api/host/pause            {id, reason, until?, who?}  (reason required)
POST /api/host/resume           {id}
POST /api/host/clear            {id}
POST /api/host/interval         {id, key, every}
POST /api/host                  {id?, name, addr, group, checks[{key,enabled,every,port,secure}], alerts{}}
POST /api/host/delete           {id}
POST /api/settings/webhook      {url,method,header,enabled,when[],format}   (format: native|m5inject)
POST /api/settings/defaults     {interval[6],fails,lcdHome,renotify,renotifyEvery}
POST /api/settings/auth         {user, pass}                (pass 8-39 chars)
POST /api/test/webhook          {}
POST /api/sd/reload             {}    (reloads hosts from flash)
POST /api/wifi/join             {ssid, pass}                (saves + reboots)
```

Every POST body is validated server-side (`validate.cpp`); invalid input returns
`400 {ok:false,error:"…"}`. Request bodies are capped (8 KB).

---

## 12. Security posture & hardening

This is a **LAN appliance**, not an internet-facing service. It is designed to be safe on
a trusted home/lab network and to fail closed on bad input — but it deliberately stops
short of protections the hardware can't support (on-device TLS) or that add cost without
much benefit on a trusted segment (flash encryption is opt-in). Read this before exposing
it anywhere beyond your LAN.

### What's protected (current posture)

- **Authentication.** Every `/api/*` endpoint requires **HTTP Basic Auth**. The password
  is **randomly generated per device on first boot** (12 chars, unambiguous alphabet) —
  there is **no shared default**. It's generated *after* Wi-Fi starts, so `esp_random()`
  draws on the hardware RNG with the RF subsystem active (true entropy, not the weaker
  pre-RF PRNG). Credentials are compared in **constant time** to avoid timing oracles.
  Only the static dashboard shell (HTML/CSS/JS) is public; all data and all mutations sit
  behind auth.
- **CSRF.** State-changing requests are rejected unless the `Origin`/`Host` check passes,
  so a malicious page in the user's browser can't drive the API. Same-origin dashboard
  traffic and non-browser clients (curl, scripts) are unaffected.
- **Input validation (fail closed).** All API and CSV input is validated server-side
  (`validate.cpp`): hostnames/addresses limited to IPv4 or RFC-1123 hostnames, intervals
  restricted to a fixed whitelist, **CSV-injection** characters rejected (and name/group
  may not *start* with `= + - @`, so a value can't become a spreadsheet **formula** if
  `hosts.csv` is opened in Excel), strings capped and limited to printable ASCII, and the
  webhook custom header **rejects CR/LF** (no header injection). Request bodies are **capped
  at 8 KB**. Invalid input returns `400 {ok:false,error:…}` and is never persisted.
- **Safe persistence + concurrency.** Hosts load *before* the web server starts and
  `saveHosts()` refuses to run until that load completes, so a boot-race request can't
  truncate `hosts.csv`. NVS blobs are length-checked on load, so a struct-layout change
  across a firmware update can't be read back misaligned. The check engine **snapshots a
  host under lock** and runs the (seconds-long, unlocked) checks against the copy, re-finding
  the live host by id for every write-back — so deleting a host from the dashboard mid-scan
  can't misattribute a result or touch a stale slot.
- **Governance.** Pause/acknowledge actions require a non-empty reason and are recorded,
  giving an audit trail for suppressed alerts.

### What is NOT protected (known residuals — by design / hardware)

- **The dashboard is cleartext HTTP.** On-device TLS isn't viable on this board (§7), so
  the Basic-Auth password and all API traffic cross the network **unencrypted**. Anyone
  who can sniff the segment can read them. This is the single most important reason to keep
  the device on a trusted network or behind a TLS-terminating reverse proxy.
- **Outbound webhooks are unverified TLS.** An HTTPS webhook is encrypted but the **server
  certificate is not checked** (§10), so it's vulnerable to an active man-in-the-middle on
  an untrusted path.
- **Secrets are stored in plaintext.** The web password, Wi-Fi PSK, and any webhook token
  live in **NVS without flash encryption** by default — readable by anyone who can dump the
  flash. Compile-time Wi-Fi credentials (§6) are likewise readable in the firmware image.
- **The auto-generated web password is shown on the LCD** until you set your own — anyone
  with physical line-of-sight to the screen can read it.
- **No auth rate-limiting / lockout.** Basic Auth has no brute-force throttle; the random
  password is the defense, so don't replace it with a weak one.
- **No Secure Boot by default**, so the firmware image isn't verified at boot.

### Recommended deployment (use)

1. **Keep it on a trusted or isolated network.** Put the appliance on your management LAN
   or a dedicated **IoT VLAN**, and firewall it off from the internet and from untrusted
   clients. Treat the HTTP dashboard as a LAN-only service.
2. **Set your own web password immediately** (Settings → 8–39 chars) so the LCD-visible
   auto-password stops being the credential. Use something strong; there's no lockout.
3. **Put TLS in front if you need encryption/remote access** — a reverse proxy (Caddy,
   nginx, Traefik) terminating HTTPS and forwarding to `http://<device-ip>/` (§7). Add an
   IP allowlist or the proxy's own auth in front of the dashboard for defense in depth.
4. **Point notifications at trusted endpoints.** Prefer a webhook receiver / SMTP relay on
   your own network (or reached through the proxy), since the device can't verify their
   certs.
5. **Physically site the device** so the LCD isn't visible to people who shouldn't have the
   password, and so the USB port isn't casually accessible.

### Hardening checklist (higher assurance)

- [ ] **Change the web password** from the auto-generated one (and keep it strong).
- [ ] **Isolate on a VLAN** with firewall rules; block inbound WAN entirely.
- [ ] **Front with a TLS reverse proxy**; optionally add proxy-level auth or an IP allowlist.
- [ ] **Enable flash encryption + Secure Boot v2** (ESP-IDF) so NVS secrets and the firmware
      image aren't readable/forgeable from physical access. *(One-way fuses — test on a
      spare board first.)*
- [ ] **Bake Wi-Fi credentials** (§6) only on encrypted-flash builds, since the image
      otherwise exposes the PSK.
- [ ] **Use a scoped/low-privilege webhook token** (in the custom header) so a flash dump
      doesn't yield a reusable high-value credential.
- [ ] **Restrict who can see the LCD** until the password has been changed.
- [ ] **Keep the firmware/toolchain current** (core 3.3.8 + the pinned libraries in §3).

---

## 13. Useful `config.h` knobs

```
WIFI_SSID_BUILTIN / _PASS_BUILTIN compile-time Wi-Fi creds (skip AP)
NTP_TZ / NTP_SERVER_1/2           timezone (POSIX TZ) + NTP servers
CONNECT_TIMEOUT_MS / HTTP_TIMEOUT_MS   check timeouts
CHECK_TASK_STACK / CHECK_TASK_CORE     check engine task
TLS_MIN_FREE_BLOCK / _TOTAL       largest-block + total-free floors to allow a TLS session (heap guard)
TLS_SELFTEST_HOST / _PORT         boot TLS self-test target ("" disables)
LCD_REFRESH_MS                    UI tick (clock + gauge in-place updates)
LCD_REBUILD_MS                    min interval between full screen rebuilds (RGB-underrun fix)
MAX_HOSTS                         host capacity (~1 KB internal RAM each)
HTTP_PORT                         dashboard port (default 80)
WEB_USER_DEFAULT / WEB_PASS_DEFAULT    seed only; replaced by the random password
```

---

## 14. Source layout

```
HostMonitor.ino     entry point: setup()/loop(), boot order (load hosts → web → display)
config.h            tunables, pins, intervals, web/auth/wifi defaults
model.h/.cpp        Host/Check types + metadata
store.h/.cpp        in-memory host store + governance mutations (mutex-guarded)
validate.h/.cpp     input validation (CSV + API)
csv.h/.cpp          LittleFS hosts.csv read/write (validated) + storage diag
settings.h/.cpp     NVS-persisted config, first-boot password gen, NTP/runtime time
rtc.h/.cpp          PCF85063 driver — DISABLED in this build (single-I2C-driver)
checks.h/.cpp       the six checks (ping/dns/port/http/ssl/trace) + insecure cert probe + TLS self-test
scheduler.h/.cpp    per-check interval engine + status/alert transitions + load metrics
notifier.h/.cpp     webhook (HTTPS/HTTP) delivery + routing (outbound TLS unverified; email removed)
tls_gate.h/.cpp     one-TLS-session-at-a-time mutex + heap guard (skip TLS when RAM tight)
wifi_portal.h/.cpp  STA join (saved or compile-time creds) + captive 'HostMon' AP + mDNS
webserver.h/.cpp    plain-HTTP dashboard/API server (task-pumped)
                    (certs.h/.cpp + cert_embedded.h removed — on-device TLS dropped)
api.h/.cpp          JSON REST handlers + embedded static dashboard (Basic Auth + CSRF)
web_assets.h        gzipped dashboard, generated from data/
display.h/.cpp      CH422G→RGB→GT911→LVGL bring-up (PSRAM draw buffers)
theme.h/.cpp        LVGL design tokens + widget factories
ui.h/.cpp           LCD root + 3-tab nav (Home / Hosts / Alerts·Setup) + refresh loop
screen_health.cpp   LCD Home (donut + needs-attention)
screen_grid.cpp     LCD Hosts (scrollable host tiles; in-place dot/chip/count repaint)
lv_conf.h           LVGL v8 config (copy to libraries/)
partitions.csv      custom 8 MB layout (5 MB app / ~2.9 MB LittleFS)
data/               dashboard sources (compiled into web_assets.h — not uploaded)
```

---

## 15. Troubleshooting

- **Black screen, backlight on** → the Waveshare board isn't selected in
  `esp_panel_board_supported_conf.h` at the libraries root (§3), or PSRAM isn't OPI.
- **Won't fit / link errors about size** → use the bundled `partitions.csv` (Custom
  scheme) and set Flash Size to match your chip.
- **Change "didn't take"** → stale build cache; delete `…/AppData/Local/arduino/sketches/*`
  and rebuild. Confirm via the `built HH:MM:SS` marker on the Alerts / Setup card.
- **`https://` won't load** → the device is **HTTP only**; on-device TLS was removed as
  unviable on this hardware (§7). Use **`http://`**, and put a TLS-terminating reverse
  proxy in front if you need HTTPS.
- **Lost the web password** → it's on the Alerts / Setup card while auto-generated; if
  you've set your own and forgotten it, erase NVS (`esptool … erase_flash` wipes the
  filesystem too) to regenerate.
- **Checks stuck "pending" / scheduler not started** → should not occur in current
  firmware (the check task uses a static stack and the scheduler starts inline); if seen,
  read the Setup card's `sched …` line and the `fs …` storage line for diagnosis.
- **Green flickering lines on the left edge** → RGB DMA underrun during a full screen
  rebuild (the PSRAM draw-buffer → framebuffer copy). Mostly designed out: the scheduler
  only marks the screen dirty on a real **state transition** (not every check run), and
  the Hosts grid repaints just the changed **dots/chips/counts in place** rather than
  rebuilding — so routine updates never do the heavy flush. A full rebuild only happens
  on a *structural* change (host added/removed/renamed/checks toggled) or a tab switch,
  coalesced to `LCD_REBUILD_MS` (default 5 s). If you still catch a line during one of
  those, raise `LCD_REBUILD_MS` (6000–8000). (Moving the LVGL draw buffer into internal
  SRAM would also help, but a useful ~16–32 KB buffer would collide with on-device TLS —
  this board can't do both.)
- **SSL check says "expired" for a valid cert** → you configured the host as an **IP**.
  The cert is chosen by SNI, so an IP gets the server's default (often older/expired) cert.
  Use the **hostname** (§9).

---

## 16. Known limitations & dropped features

Capabilities that were attempted and then removed (or never shipped) because of hardware
or platform constraints on this board. Almost all of them trace to two roots: **not enough
free internal RAM for on-device TLS**, and the panel stack's **single-I²C-driver**
requirement. Recorded here so the gaps — and *why* they exist — are explicit.

### On-device TLS — limited by RAM (one insecure session only)
An mbedTLS session needs ~16–44 KB of *contiguous internal* RAM. After reclaiming RAM
(smaller `MAX_HOSTS`, removing the cert/HTTPS-server cruft) the board can run **one insecure
session at a time** — confirmed by the self-test below — so the **cert-expiry check** and
insecure HTTPS host checks/notifications work, serialized by the TLS gate (§10). What it
**still can't** do is run **multiple concurrent** sessions (a real HTTPS dashboard server,
where one page load opens several connections) or **verified** TLS (CA-bundle chain
validation, which costs materially more RAM). Those remain reverse-proxy territory — see §7.

> **How this was settled — the live gauge + self-test** on the **Alerts / Setup** card:
> `RAM free Nk · largest Nk · min Nk` (updated every second), plus a one-shot insecure
> handshake to `TLS_SELFTEST_HOST` (`config.h`) ~3 s after boot reporting `TLS … OK(insec)
> … lb A->B` (largest free block before→during the session). It came back **OK** with ~23 KB
> still free at the session's peak, which green-lit restoring the cert-expiry check. Keep an
> eye on `largest`/`min` if you add hosts; `MAX_HOSTS` frees ~1 KB/host of the RAM TLS draws
> from. Set `TLS_SELFTEST_HOST` to `""` to disable the boot probe.

- **HTTPS for the dashboard.** The TLS server, runtime cert provisioning, and the embedded
  cert/key were removed; the dashboard is **plain HTTP only** (a page load needs several
  concurrent TLS sessions, which don't fit). Use a reverse proxy / VLAN.
- **Self-signed cert + trust-on-first-use (TOFU).** Wired up as a lighter alternative, then
  abandoned — handshakes reset regardless of cert type (tested both RSA and ECDSA).
- **SSL/TLS certificate-expiry check** — *removed in 1.4.3, **restored (insecure) in 1.4.8***
  once the self-test confirmed a single session fits. Reads the cert `notAfter`; does **not**
  validate the chain (verified mode would need the CA bundle = more RAM than measured to fit).
- **HTTPS host-check "verify" option.** Removed — HTTPS host checks are **insecure-only**
  (reachability + status, not certificate trust).
- **Email / SMTP alerting (ESP Mail Client)** — *removed in 1.4.9*. It was the heaviest TLS
  user (its own larger TLS stack), the one path the self-test couldn't vouch for; dropping
  it raises the worst-case internal-RAM floor. **Webhook** is the only delivery channel now
  — route it to a downstream relay if you need email. The library can be uninstalled.
- **Notifier outbound TLS verification** (HTTPS webhooks, SMTP-over-465 against the CA
  bundle). Removed — the webhook is encrypted but the server identity is **not** checked.
- **`cert<14d` alert routing.** Collateral — its only trigger was the removed cert check.
- **Verified HTTPS via `HTTPClient::setCACertBundle()`.** Abandoned earlier for a separate
  reason: the core CA bundle couldn't be referenced by symbol on this core (link error
  `undefined reference _binary_…_crt_bundle_bin`), which is why the raw-mbedTLS probe was
  used in the interim before it too was removed.

### Hardware / driver conflicts
- **On-board PCF85063 RTC (battery-backed clock).** Disabled. The panel library uses the
  *legacy* IDF I²C driver; Arduino's `Wire` pulls in the *new* `driver_ng`, and linking both
  aborts at boot (`check_i2c_driver_conflict`). The firmware stays **`Wire`-free**, so time
  comes from **NTP** with a runtime/uptime fallback — there's no wall clock across a
  power-cycle until NTP re-syncs.
- **SD-card storage.** Dropped. SD chip-select falls back to GPIO10, an active RGB data line
  (DATA4); mounting SD while the panel runs corrupts the parallel bus. Persistence uses the
  **on-flash LittleFS** instead.
- **Captive-portal auto-redirect** (the setup popup in AP mode). Dropped. The bundled
  AsyncUDP/DNSServer calls `udp_new()` without the lwIP core lock and asserts under
  `LWIP_TCPIP_CORE_LOCKING`. The AP still works — browse to its IP manually.
- **Reliable runtime USB-serial logging.** Given up as the primary diagnostic channel:
  serial is unreliable once the RGB panel + Wi-Fi are both live, so diagnostics surface on
  the LCD's **Alerts / Setup** card instead.

### Tried and reverted (memory pressure)
- **Internal-RAM LVGL draw buffers.** Attempted to fix the green RGB flicker, but it
  consumed enough internal heap to make the check task fail to create. Reverted to **PSRAM**
  draw buffers; the flicker was instead designed out in firmware — mark dirty only on a
  state transition, repaint the Hosts grid in place, and coalesce any full rebuild to
  `LCD_REBUILD_MS` (§15).

### Platform pins (constraints accepted, not lost features)
- **LVGL stays on v8.4** — v9 won't compile against this code.
- **A custom `partitions.csv` is mandatory** — the app won't fit a default partition scheme.
- **The build must stay `Wire`-free** — see the RTC note above.
