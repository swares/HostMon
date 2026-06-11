/* ============================================================================
 *  settings.h — persisted device configuration (NVS via Preferences).
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include "config.h"

// Email/SMTP was removed (ESP Mail Client's TLS footprint didn't fit this board's
// internal RAM). Webhook is the only delivery channel now — route to a relay for email.
struct WebhookCfg {
  bool   enabled=false;
  char   url[160]="";
  char   method[6]="POST";
  char   header[120]="";
  bool   whenDown=true, whenWarn=true, whenRecovered=true,
         whenAck=true, whenPaused=false, whenCert=false;
  bool   verify=true;           // DEPRECATED — on-device TLS cert verify removed (§7);
                                // field kept only so the NVS blob layout is unchanged.
  char   last[24]="never";
  bool   lastOk=false;
};

struct WifiCfg {
  char   ssid[33]="";
  char   pass[65]="";
  bool   apMode=false;          // true => stay an Access Point
};

struct WebAuthCfg {
  char user[24]=WEB_USER_DEFAULT;
  char pass[40]=WEB_PASS_DEFAULT;
  bool autoGen=false;           // true while pass is an auto-generated one (shown on LCD)
};

struct Defaults {
  uint32_t interval[6 /*kCheckCount*/] = { DEF_INT_PING,DEF_INT_DNS,DEF_INT_PORT,
                                           DEF_INT_HTTP,DEF_INT_SSL,DEF_INT_TRACE };
  uint8_t  failsBeforeAlert = DEFAULT_FAILS_BEFORE_ALERT;
  char     lcdHome = 'A';        // 'A' Health, 'B' Grid
  bool     renotify = RENOTIFY_DEFAULT;
  uint32_t renotifyEvery = RENOTIFY_EVERY_S;
};

namespace Settings {
  void begin();
  void ensureWebPassword();   // gen the random first-boot password — call AFTER Wi-Fi is up
                              // (esp_random needs the RF subsystem for true entropy)
  void save();

  WebhookCfg& webhook();
  WifiCfg&    wifi();
  WebAuthCfg& auth();
  Defaults&   defaults();

  // Time strategy: RTC seed at boot -> NTP when online (writes back to RTC)
  // -> RTC fallback -> runtime/uptime clock.
  void        startClock();        // call at boot (after I2C/display ready)
  void        startTime();         // call once network is up (NTP)
  const char* timeSource();        // "ntp" | "rtc" | "runtime"
  const char* clockHHMM();        // "14:53"
  const char* dateShort();        // "Sat 7 Jun"
  const char* dateLongUpper();    // "SAT 07 JUN"
  void        isoNow(char* out, size_t n);  // 2026-06-07T14:22:06Z
}
