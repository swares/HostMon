/* wifi_portal.cpp — Wi-Fi connection management + captive DNS. */
#include "wifi_portal.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>          // internal-heap diagnostics for the LCD card
#include "lwip/priv/tcpip_priv.h"   // LOCK_TCPIP_CORE / UNLOCK_TCPIP_CORE

// arduino-esp32 3.x (IDF5) lwIP has core-locking checks; DNSServer.start()
// calls raw udp_new() and must hold the TCPIP core lock or it asserts.
#if defined(LWIP_TCPIP_CORE_LOCKING) && LWIP_TCPIP_CORE_LOCKING
  #define HM_LOCK_TCPIP()   LOCK_TCPIP_CORE()
  #define HM_UNLOCK_TCPIP() UNLOCK_TCPIP_CORE()
#else
  #define HM_LOCK_TCPIP()
  #define HM_UNLOCK_TCPIP()
#endif

namespace {
  bool      g_ap=false;
  bool      g_started=false;
  char      g_ip[20]="0.0.0.0";
  char      g_diag[56]="wifi: not started";

  void startAP(){
    g_ap=true;
    // Progressive breadcrumb shown on the LCD Setup card (serial is dead at
    // runtime). The LAST token reached tells us exactly where AP bring-up stalls.
    //   ih/lb = internal heap free / largest free block (KB) before WiFi init
    //   >mode  reached after WiFi.mode()
    //   >sAP1/0 softAP() returned (1=ok, 0=fail)
    //   >cfg   reached after softAPConfig()
    //   >dns   reached after DNS server start (function completed)
    snprintf(g_diag, sizeof(g_diag), "ih=%uk lb=%uk",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)/1024));
    WiFi.persistent(false);
    // AP_STA (not AP): the station interface must exist for WiFi.scanNetworks() to
    // run during setup. In plain WIFI_AP mode the scan blocks forever and the setup
    // page's "find networks" request never returns. The AP stays up either way.
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);   // keep clocks up: WiFi modem-sleep drops USB-Serial-JTAG
    delay(100);
    strlcat(g_diag, " >mode", sizeof(g_diag));
    bool ok = WiFi.softAP(AP_SSID);
    strlcat(g_diag, ok ? " >sAP1" : " >sAP0", sizeof(g_diag));
    delay(150);
    IPAddress apIP(AP_IP_OCTETS), mask(255,255,255,0);
    WiFi.softAPConfig(apIP, apIP, mask);
    strlcpy(g_ip, WiFi.softAPIP().toString().c_str(), sizeof(g_ip));
    strlcat(g_diag, " >cfg", sizeof(g_diag));
    // No captive DNS redirect. The installed standalone AsyncUDP/DNSServer calls
    // udp_new() without the lwIP core lock and asserts under
    // LWIP_TCPIP_CORE_LOCKING. The AP works regardless — connect to it and browse
    // directly to the IP below; no auto-popup portal.
    strlcat(g_diag, " >up", sizeof(g_diag));
    Serial.printf("[wifi] AP '%s' %s ip=%s  (open https://%s)\n",
                  AP_SSID, g_diag, g_ip, g_ip);
  }

  bool startSTA(){
    // Compile-time credentials (config.h) take precedence and skip the AP entirely.
    // Otherwise fall back to whatever the setup wizard saved. No creds anywhere ->
    // return false so begin() brings up the first-run AP.
    const char* ssid; const char* pass;
    if(WIFI_SSID_BUILTIN[0]){
      ssid=WIFI_SSID_BUILTIN; pass=WIFI_PASS_BUILTIN;
    } else {
      WifiCfg& w=Settings::wifi();
      if(w.ssid[0]==0) return false;
      ssid=w.ssid; pass=w.pass;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   // keep clocks up: WiFi modem-sleep drops USB-Serial-JTAG
    WiFi.setHostname(MDNS_HOSTNAME);
    WiFi.begin(ssid, pass);
    uint32_t t0=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-t0<12000){ delay(250); Serial.print('.'); }
    Serial.println();
    if(WiFi.status()==WL_CONNECTED){
      g_ap=false; strlcpy(g_ip, WiFi.localIP().toString().c_str(), sizeof(g_ip));
      snprintf(g_diag, sizeof(g_diag), "STA '%s' -> %s", ssid, g_ip);
      if(MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http","tcp",HTTP_PORT);
      Settings::startTime();
      Serial.printf("[wifi] STA '%s' -> %s (http://%s.local)\n", ssid, g_ip, MDNS_HOSTNAME);
      return true;
    }
    Serial.println("[wifi] STA join failed");
    return false;
  }
}

void WifiPortal::begin(){ if(!startSTA()) startAP(); g_started=true; }

void WifiPortal::loop(){
  if(!g_started) return;          // no-op until the init task has run begin()
  if(!g_ap && WiFi.status()!=WL_CONNECTED){
    static uint32_t last=0;
    if(millis()-last>15000){ last=millis(); WiFi.reconnect(); }
  }
}

bool WifiPortal::isAP(){ return g_ap; }
bool WifiPortal::isOnline(){ return !g_ap && strcmp(g_ip,"0.0.0.0")!=0; }
const char* WifiPortal::ipString(){ return g_ip; }
const char* WifiPortal::diag(){ return g_diag; }

String WifiPortal::scanJson(){
  int n=WiFi.scanNetworks();
  String s="[";
  for(int i=0;i<n;i++){
    if(i) s+=",";
    int rssi=WiFi.RSSI(i);
    int bars = rssi>-55?4 : rssi>-65?3 : rssi>-75?2 : 1;
    bool open = WiFi.encryptionType(i)==WIFI_AUTH_OPEN;
    String ssid=WiFi.SSID(i); ssid.replace("\"","");
    s += "{\"ssid\":\""+ssid+"\",\"sig\":"+bars+",\"lock\":"+(open?"false":"true")+"}";
  }
  s+="]"; WiFi.scanDelete();
  return s;
}

bool WifiPortal::joinNetwork(const char* ssid, const char* pass){
  WifiCfg& w=Settings::wifi();
  strlcpy(w.ssid, ssid, sizeof(w.ssid));
  strlcpy(w.pass, pass?pass:"", sizeof(w.pass));
  w.apMode=false; Settings::save();
  delay(400); ESP.restart();      // reboot to join cleanly (matches setup UX)
  return true;
}
