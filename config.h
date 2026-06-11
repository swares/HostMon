/* ============================================================================
 *  config.h — app-wide constants and tunables for Host Monitor
 *  Hardware pin/panel details are handled by ESP32_Display_Panel's board
 *  selection (see README). Keep board-specific magic out of this file.
 * ========================================================================== */
#pragma once
#include <Arduino.h>

// ---- Identity -------------------------------------------------------------
#define DEVICE_NAME        "hostmon-01"
#define FIRMWARE_VERSION   "1.4.18"
#define MDNS_HOSTNAME      "monitor"          // -> http://monitor.local
#define AP_SSID            "HostMon"          // first-run captive AP
#define AP_IP_OCTETS       192,168,4,1

// Optional compile-time Wi-Fi credentials. If WIFI_SSID_BUILTIN is non-empty the
// device joins THIS network directly on every boot and skips the AP setup wizard
// and any saved credentials. Leave both "" for the normal first-run AP + saved-
// credentials flow. (Baking a password into firmware is convenient for a fixed
// deployment but means anyone with the image can read it — use with that in mind.)
#define WIFI_SSID_BUILTIN  ""
#define WIFI_PASS_BUILTIN  ""

// ---- Filesystem paths -----------------------------------------------------
#define HOSTS_CSV_PATH     "/hosts.csv"       // on the SD card
#define WEB_ROOT           "/"                // LittleFS root for static assets

// ---- SD card (SPI) — adjust to your wiring; CS commonly on an EXIO pin ----
// On the Waveshare 4.3" the SD shares the SPI bus; CH422G provides SD_CS.
// If your board uses GPIO for SD_CS set it here, else SD_CS_EXIO is used.
#define SD_SCK_PIN         12
#define SD_MOSI_PIN        11
#define SD_MISO_PIN        13
#define SD_CS_PIN          -1                 // -1 => use CH422G EXIO (see display.cpp)

// ---- On-board RTC (PCF85063, I2C — shared bus with touch/expander) ---------
#define RTC_ENABLE         1
#define RTC_I2C_ADDR       0x51
#define RTC_SDA_PIN        8
#define RTC_SCL_PIN        9
#define NTP_SERVER_1       "pool.ntp.org"
#define NTP_SERVER_2       "time.nist.gov"
#define NTP_TZ             "UTC0"               // POSIX TZ; change for local time
#define NTP_TIMEOUT_MS     3000

// ---- Display --------------------------------------------------------------
#define LCD_W              800
#define LCD_H              480
#define LVGL_TICK_MS       2
#define LCD_REFRESH_MS     1000               // UI::loop tick: clock + in-place gauge updates
#define LCD_REBUILD_MS     5000               // min interval between FULL screen rebuilds.
                                              // A rebuild tears down + repaints every widget; its
                                              // PSRAM draw-buf -> PSRAM framebuffer copy starves the
                                              // RGB DMA (green underrun lines on the left edge). With
                                              // many checks the store goes dirty ~every scan, so we
                                              // coalesce rebuilds to this cadence. Raise it if the
                                              // lines persist; host data changes slowly regardless.

// ---- Monitoring engine ----------------------------------------------------
#define MAX_HOSTS          24                  // static Host g_hosts[MAX_HOSTS] (~1 KB/host) lives
                                               // in internal SRAM — keep modest to preserve heap.
#define CHECKS_PER_HOST    6                   // ping,dns,port,http,ssl,trace
#define CHECK_TASK_STACK   16384                // HTTPS/SSL checks use mbedTLS (deep stack)
#define CHECK_TASK_CORE    0   // run checks on core 0 (WiFi/lwIP core); the LVGL
                               // display loop owns core 1, so CPU-heavy checks
                               // (TLS handshake crypto, DNS) can't starve the UI.
#define DEFAULT_FAILS_BEFORE_ALERT  3
#define CONNECT_TIMEOUT_MS 4000
#define HTTP_TIMEOUT_MS    6000
#define PING_COUNT         3
#define SSL_WARN_DAYS      14                  // cert-expiry check warns when fewer days remain
#define TRACE_MAX_HOPS     12

// ---- One-shot on-device TLS self-test (RAM-viability probe) ---------------
// Runs ONCE from the check task ~3 s after boot: an insecure TLS handshake to this
// host, reporting the setup/handshake result + largest contiguous free internal block
// (before→after on success, or at the failure point) on the Alerts/Setup card. This
// is the empirical check for whether on-device TLS is worth reviving on the current RAM
// headroom. Set the host to "" to disable the probe.
#define TLS_SELFTEST_HOST  "example.com"
#define TLS_SELFTEST_PORT  443

// Heap guard: before starting ANY outbound TLS session (cert check, HTTPS host check,
// webhook, self-test) the gate requires at least this much *contiguous* free internal
// RAM. An mbedTLS session needs ~16 KB buffers and ~44 KB total; if the largest free
// block is already below this, the session would risk an out-of-memory fault, so it's
// skipped for that cycle and reported as "low mem" instead. Raise/lower to taste.
#define TLS_MIN_FREE_BLOCK 20480              // 20 KB largest-free-block minimum
#define TLS_MIN_FREE_TOTAL 49152              // ...AND 48 KB total free internal. A session needs
                                              // ~44 KB, so this is sized as crash INSURANCE: it
                                              // only blocks a session that couldn't fit anyway
                                              // (free below it -> the alloc would fail / fault), so
                                              // checks that currently succeed are unaffected. Raise
                                              // it (e.g. 54–60 KB) if you want a guaranteed higher
                                              // floor and accept TLS checks deferring under load.

// ---- Per-check default intervals (seconds) — mirrors the design spec ------
#define DEF_INT_PING       30
#define DEF_INT_DNS        300
#define DEF_INT_PORT       60
#define DEF_INT_HTTP       60
#define DEF_INT_SSL        43200
#define DEF_INT_TRACE      300

// ---- Web / API ------------------------------------------------------------
// Plain HTTP only. On-device TLS isn't viable on this board (mbedTLS per-connection
// RAM exceeds what's free — the listener starts but handshakes reset). For real TLS,
// front the device with a reverse proxy (Caddy/nginx/Traefik) or keep it on a trusted
// VLAN. See README §7.
#define HTTP_PORT          80                  // dashboard / captive setup
#define API_POLL_HINT_MS   5000                // advertised to the dashboard

// ---- Web auth (HTTP Basic) ------------------------------------------------
// A random per-device password is generated on first boot and shown on the LCD;
// these are only the seed values (the default triggers that one-time generation).
#define WEB_USER_DEFAULT   "admin"
#define WEB_PASS_DEFAULT   "hostmon"           // replaced on first boot; change in Settings

// ---- Alerting -------------------------------------------------------------
#define ALERT_LOG_MAX      40                  // ring buffer of recent alerts
#define RENOTIFY_DEFAULT   true
#define RENOTIFY_EVERY_S   1800                // 30m

// ---- Misc -----------------------------------------------------------------
#define JSON_DOC_LARGE     16384
#define JSON_DOC_MED       4096
