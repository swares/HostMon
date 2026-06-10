/* ============================================================================
 *  settings.h — persisted device configuration (NVS via Preferences).
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include "config.h"

struct EmailCfg {
  bool   enabled=false;
  char   host[64]="smtp.fastmail.com";
  uint16_t port=465;
  char   user[64]="";
  char   pass[64]="";
  char   from[64]="bot@home.net";
  char   to[64]="me@home.net";
  bool   whenDown=true, whenWarn=false, whenRecovered=true, whenCert=true;
  bool   verify=true;           // DEPRECATED — on-device TLS cert verify removed (§7);
                                // field kept only so the NVS blob layout is unchanged.
  char   lastTest[24]="never";
  bool   lastOk=false;
};

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
  uint32_t interval[5 /*kCheckCount*/] = { DEF_INT_PING,DEF_INT_DNS,DEF_INT_PORT,
                                           DEF_INT_HTTP,DEF_INT_TRACE };
  uint8_t  failsBeforeAlert = DEFAULT_FAILS_BEFORE_ALERT;
  char     lcdHome = 'A';        // 'A' Health, 'B' Grid
  bool     renotify = RENOTIFY_DEFAULT;
  uint32_t renotifyEvery = RENOTIFY_EVERY_S;
};

namespace Settings {
  void begin();
  void save();

  EmailCfg&   email();
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
