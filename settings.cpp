/* settings.cpp — NVS-backed configuration + time helpers. */
#include "settings.h"
#include "config.h"                 // WEB_PASS_DEFAULT
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>
#include <esp_random.h>             // hardware RNG for the first-boot web password
#include "rtc.h"
#include "display.h"                // Display::lock() to fence flash writes
#include "lwip/priv/tcpip_priv.h"   // SNTP (configTzTime) also uses raw lwIP udp

#if defined(LWIP_TCPIP_CORE_LOCKING) && LWIP_TCPIP_CORE_LOCKING
  #define HM_LOCK_TCPIP()   LOCK_TCPIP_CORE()
  #define HM_UNLOCK_TCPIP() UNLOCK_TCPIP_CORE()
#else
  #define HM_LOCK_TCPIP()
  #define HM_UNLOCK_TCPIP()
#endif

namespace {
  Preferences   prefs;
  EmailCfg      g_email;
  WebhookCfg    g_webhook;
  WifiCfg       g_wifi;
  Defaults      g_def;
  WebAuthCfg    g_auth;
  char          g_buf[24];
  const char*   g_src="runtime";
}

static void loadBlob(const char* k, void* p, size_t n){
  // Only accept a stored blob whose size EXACTLY matches the current struct. If the
  // layout changed across a firmware update — e.g. removing the SSL check shrank
  // Defaults::interval[] from 6 to 5 — an old, larger blob would be copied in
  // misaligned and corrupt every field after the change. On a size mismatch we keep
  // the compiled-in defaults instead (the user re-saves Settings if they'd customised).
  if(prefs.getBytesLength(k) != n) return;
  prefs.getBytes(k, p, n);
}

static void genPassword(char* out, size_t n){
  // Unambiguous alphabet (no 0/O/1/l/I) so it's easy to read off the LCD.
  static const char cs[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
  size_t L=12; if(L>n-1) L=n-1;
  for(size_t i=0;i<L;i++) out[i]=cs[esp_random() % (sizeof(cs)-1)];
  out[L]=0;
}

void Settings::begin(){
  prefs.begin("hostmon", false);
  loadBlob("email",   &g_email,   sizeof(g_email));
  loadBlob("webhook", &g_webhook, sizeof(g_webhook));
  loadBlob("wifi",    &g_wifi,    sizeof(g_wifi));
  loadBlob("def",     &g_def,     sizeof(g_def));
  loadBlob("auth",    &g_auth,    sizeof(g_auth));

  // First boot (or after a flash erase): the password is still the compile-time
  // default. Replace it with a random per-device one so no two units ship with the
  // same known credential. It's shown on the Alerts/Setup card until the user sets
  // their own. Runs before Display::begin(), so the NVS write here is panel-safe.
  if(strcmp(g_auth.pass, WEB_PASS_DEFAULT)==0){
    genPassword(g_auth.pass, sizeof(g_auth.pass));
    g_auth.autoGen = true;
    prefs.putBytes("auth", &g_auth, sizeof(g_auth));
  }
}
void Settings::save(){
  // Fence the NVS (flash) writes behind the LVGL lock so the RGB panel isn't
  // reading PSRAM through a disabled cache during them (see csv.cpp saveHosts).
  Display::lock();
  prefs.putBytes("email",   &g_email,   sizeof(g_email));
  prefs.putBytes("webhook", &g_webhook, sizeof(g_webhook));
  prefs.putBytes("wifi",    &g_wifi,    sizeof(g_wifi));
  prefs.putBytes("def",     &g_def,     sizeof(g_def));
  prefs.putBytes("auth",    &g_auth,    sizeof(g_auth));
  Display::unlock();
}

EmailCfg&   Settings::email(){ return g_email; }
WebhookCfg& Settings::webhook(){ return g_webhook; }
WifiCfg&    Settings::wifi(){ return g_wifi; }
WebAuthCfg& Settings::auth(){ return g_auth; }
Defaults&   Settings::defaults(){ return g_def; }

void Settings::startClock(){
  // Boot-time: seed the system clock from the external RTC if it holds a
  // valid time, so the LCD shows the right time before any network is up.
  Rtc::begin();
  struct tm tm;
  if(Rtc::get(tm)){
    time_t t=mktime(&tm); struct timeval tv={ .tv_sec=t, .tv_usec=0 };
    settimeofday(&tv, nullptr); g_src="rtc";
    Serial.println("[time] seeded from RTC");
  } else {
    g_src="runtime";
  }
  // Release the shared I2C pins to the display panel BEFORE it initialises.
  // (Wire = new driver; panel = legacy driver — they can't share a port.)
  Rtc::releaseBus();
}

void Settings::startTime(){
  // Network is up: try NTP. On success, also persist the time to the RTC so it
  // survives reboots/power loss. If NTP fails, keep whatever the RTC/runtime
  // clock already provides.
  if(WiFi.status()==WL_CONNECTED){
    // configTzTime()/esp_sntp manage their own tcpip-thread locking; holding
    // LOCK_TCPIP_CORE() here deadlocks SNTP init (same class of bug as AsyncUDP).
    configTzTime(NTP_TZ, NTP_SERVER_1, NTP_SERVER_2);
    struct tm tm;
    if(getLocalTime(&tm, NTP_TIMEOUT_MS)){
      g_src="ntp";
      if(Rtc::present()){ Rtc::set(tm); Serial.println("[time] NTP ok -> RTC updated"); }
      else Serial.println("[time] NTP ok (no RTC to persist)");
      return;
    }
    Serial.println("[time] NTP unavailable");
  }
  if(Rtc::present()){ struct tm tm; if(Rtc::get(tm)) g_src="rtc"; }
}

const char* Settings::timeSource(){ return g_src; }

static struct tm nowTm(){
  time_t t = time(nullptr);
  struct tm tm; localtime_r(&t, &tm); return tm;
}
const char* Settings::clockHHMM(){ struct tm tm=nowTm(); snprintf(g_buf,sizeof(g_buf),"%02d:%02d",tm.tm_hour,tm.tm_min); return g_buf; }
const char* Settings::dateShort(){
  static const char* W[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char* Mn[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  struct tm tm=nowTm(); snprintf(g_buf,sizeof(g_buf),"%s %d %s",W[tm.tm_wday],tm.tm_mday,Mn[tm.tm_mon]); return g_buf;
}
const char* Settings::dateLongUpper(){
  static const char* W[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
  static const char* Mn[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  struct tm tm=nowTm(); snprintf(g_buf,sizeof(g_buf),"%s %02d %s",W[tm.tm_wday],tm.tm_mday,Mn[tm.tm_mon]); return g_buf;
}
void Settings::isoNow(char* out, size_t n){
  struct tm tm=nowTm();
  snprintf(out,n,"%04d-%02d-%02dT%02d:%02d:%02dZ",
    tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
}
