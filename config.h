/* ============================================================================
 *  config.h — app-wide constants and tunables for Host Monitor
 *  Hardware pin/panel details are handled by ESP32_Display_Panel's board
 *  selection (see README). Keep board-specific magic out of this file.
 * ========================================================================== */
#pragma once
#include <Arduino.h>

// ---- Identity -------------------------------------------------------------
#define DEVICE_NAME        "hostmon-01"
#define FIRMWARE_VERSION   "1.4.3"
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
#define LCD_REFRESH_MS     1000               // redraw LCD data this often

// ---- Monitoring engine ----------------------------------------------------
#define MAX_HOSTS          64
#define CHECKS_PER_HOST    5                   // ping,dns,port,http,trace
#define CHECK_TASK_STACK   16384                // HTTPS host check uses WiFiClientSecure/mbedTLS (deep stack)
#define CHECK_TASK_CORE    0   // run checks on core 0 (WiFi/lwIP core); the LVGL
                               // display loop owns core 1, so CPU-heavy checks
                               // (TLS handshake crypto, DNS) can't starve the UI.
#define DEFAULT_FAILS_BEFORE_ALERT  3
#define CONNECT_TIMEOUT_MS 4000
#define HTTP_TIMEOUT_MS    6000
#define PING_COUNT         3
#define TRACE_MAX_HOPS     12

// ---- Per-check default intervals (seconds) — mirrors the design spec ------
#define DEF_INT_PING       30
#define DEF_INT_DNS        300
#define DEF_INT_PORT       60
#define DEF_INT_HTTP       60
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
