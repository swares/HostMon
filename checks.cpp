/* ============================================================================
 *  checks.cpp — ping / dns / port / http / trace implementations.
 *
 *  All checks are blocking and must run on the dedicated check task (not the
 *  LVGL or web task). Each returns a CheckResult with a derived state and a
 *  short human detail string mirroring the dashboard's wording.
 * ========================================================================== */
#include "checks.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include "ping/ping_sock.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Helper: resolve a host/IP string into an IPAddress (DNS-timed by caller).
static bool resolve(const char* addr, IPAddress& out){
  if(out.fromString(addr)) return true;           // literal IPv4
  return WiFi.hostByName(addr, out) == 1;
}

// ---------------------------------------------------------------------------
// PING (ICMP) — also yields a TTL we reuse for the traceroute hop estimate.
struct PingAccum { uint32_t recv=0, sent=0, sumMs=0, ttl=0; SemaphoreHandle_t done; };

static void ping_on_success(esp_ping_handle_t h, void* a){
  PingAccum* p=(PingAccum*)a; uint32_t t=0, ttl=0;
  esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
  esp_ping_get_profile(h, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
  p->recv++; p->sumMs+=t; if(ttl) p->ttl=ttl;
}
static void ping_on_timeout(esp_ping_handle_t, void*){}
static void ping_on_end(esp_ping_handle_t h, void* a){
  PingAccum* p=(PingAccum*)a;
  esp_ping_get_profile(h, ESP_PING_PROF_REQUEST, &p->sent, sizeof(p->sent));
  xSemaphoreGive(p->done);
}

struct PingOut { bool ok; float avgMs; int lossPct; int hops; };
static PingOut doPing(const IPAddress& ip, uint32_t count){
  PingOut o{false,0,100,0};
  ip_addr_t target; memset(&target,0,sizeof(target));
  target.type=IPADDR_TYPE_V4; target.u_addr.ip4.addr=(uint32_t)ip;

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr=target; cfg.count=count; cfg.timeout_ms=CONNECT_TIMEOUT_MS; cfg.interval_ms=200;

  PingAccum acc; acc.done=xSemaphoreCreateBinary();
  esp_ping_callbacks_t cb={ .cb_args=&acc, .on_ping_success=ping_on_success,
                            .on_ping_timeout=ping_on_timeout, .on_ping_end=ping_on_end };
  esp_ping_handle_t h=nullptr;
  if(esp_ping_new_session(&cfg,&cb,&h)!=ESP_OK){ vSemaphoreDelete(acc.done); return o; }
  esp_ping_start(h);
  xSemaphoreTake(acc.done, pdMS_TO_TICKS((CONNECT_TIMEOUT_MS+400)*count));
  esp_ping_stop(h); esp_ping_delete_session(h); vSemaphoreDelete(acc.done);

  if(acc.sent==0) return o;
  o.lossPct = (int)(100*(acc.sent-acc.recv)/acc.sent);
  if(acc.recv){ o.ok=true; o.avgMs=(float)acc.sumMs/acc.recv;
    // Estimate hops from the reply TTL (common initial TTLs: 64/128/255).
    int base = acc.ttl<=64?64 : acc.ttl<=128?128 : 255;
    o.hops = base-(int)acc.ttl+1; if(o.hops<1)o.hops=1; if(o.hops>30)o.hops=30; }
  return o;
}


// ---------------------------------------------------------------------------
CheckResult Checks::run(const Host& h, CheckKind k){
  CheckResult r;
  IPAddress ip;

  switch(k){
    case CheckKind::Ping: {
      if(!resolve(h.addr,ip)){ r.state=CheckState::Down; strlcpy(r.detail,"DNS fail",sizeof(r.detail)); break; }
      PingOut p=doPing(ip,PING_COUNT);
      if(!p.ok){ r.state=CheckState::Down; strlcpy(r.detail,"100% loss",sizeof(r.detail)); }
      else if(p.lossPct>0){ r.state=CheckState::Warn; snprintf(r.detail,sizeof(r.detail),"%d%% loss",p.lossPct); r.rttMs=(uint32_t)p.avgMs; }
      else { r.state=CheckState::Up; snprintf(r.detail,sizeof(r.detail),"%.1f ms",p.avgMs); r.rttMs=(uint32_t)(p.avgMs+0.5f); }
      break;
    }
    case CheckKind::Dns: {
      uint32_t t0=millis(); bool ok=resolve(h.addr,ip); uint32_t dt=millis()-t0;
      if(!ok){ r.state=CheckState::Down; strlcpy(r.detail,"no resolve",sizeof(r.detail)); }
      else if(dt>300){ r.state=CheckState::Warn; snprintf(r.detail,sizeof(r.detail),"slow · %u ms",dt); r.rttMs=dt; }
      else { r.state=CheckState::Up; snprintf(r.detail,sizeof(r.detail),"resolves · %u ms",dt); r.rttMs=dt; }
      break;
    }
    case CheckKind::Port: {
      uint16_t port = h.checks[(uint8_t)CheckKind::Port].port; if(port==0) port=80;
      WiFiClient c; uint32_t t0=millis();
      bool ok = c.connect(h.addr, port, CONNECT_TIMEOUT_MS); uint32_t dt=millis()-t0;
      if(ok){ if(dt>1000){ r.state=CheckState::Warn; snprintf(r.detail,sizeof(r.detail),"%u slow",port);} 
              else { r.state=CheckState::Up; snprintf(r.detail,sizeof(r.detail),"%u open",port);} r.rttMs=dt; c.stop(); }
      else { r.state=CheckState::Down; snprintf(r.detail,sizeof(r.detail),"refused %u",port); }
      break;
    }
    case CheckKind::Http: {
      // Scheme is now explicit (the per-check `secure` flag), decoupled from the
      // port — so HTTPS on a non-standard port (e.g. 8443) works, and the port
      // defaults to 443/80 to match the scheme when left unset.
      const Check& hc = h.checks[(uint8_t)CheckKind::Http];
      bool secure = hc.secure;
      const char* scheme = secure ? "https" : "http";
      uint16_t defPort = secure ? 443 : 80;
      uint16_t port = hc.port ? hc.port : defPort;
      char url[96];
      if(port==defPort) snprintf(url,sizeof(url),"%s://%s/",scheme,h.addr);
      else              snprintf(url,sizeof(url),"%s://%s:%u/",scheme,h.addr,port);
      HTTPClient http; http.setConnectTimeout(CONNECT_TIMEOUT_MS); http.setTimeout(HTTP_TIMEOUT_MS);
      // HTTPS here is always insecure (accept any cert) — on-device certificate
      // verification isn't viable on this board (mbedTLS can't get its session
      // buffers from the free internal heap), so there's no verify option. This
      // checks reachability + HTTP status, not certificate trust. See README §7.
      WiFiClientSecure sec; WiFiClient plain; sec.setInsecure();
      uint32_t t0=millis(); bool began = secure ? http.begin(sec,url) : http.begin(plain,url);
      if(!began){ r.state=CheckState::Down; strlcpy(r.detail,"bad url",sizeof(r.detail)); break; }
      int code=http.GET(); uint32_t dt=millis()-t0; http.end(); r.rttMs=dt;
      if(code<=0){ r.state=CheckState::Down; strlcpy(r.detail,"timeout",sizeof(r.detail)); }
      else if(code>=200 && code<400){ r.state=CheckState::Up; snprintf(r.detail,sizeof(r.detail),"%d OK",code); }
      else if(code>=400 && code<500){ r.state=CheckState::Warn; snprintf(r.detail,sizeof(r.detail),"%d",code); }
      else { r.state=CheckState::Down; snprintf(r.detail,sizeof(r.detail),"%d",code); }
      break;
    }
    case CheckKind::Trace: {
      if(!resolve(h.addr,ip)){ r.state=CheckState::Down; strlcpy(r.detail,"DNS fail",sizeof(r.detail)); break; }
      PingOut p=doPing(ip,2);
      if(!p.ok){ r.state=CheckState::Down; strlcpy(r.detail,"unreachable",sizeof(r.detail)); }
      else { r.state=CheckState::Up; snprintf(r.detail,sizeof(r.detail),"%d hop%s",p.hops,p.hops>1?"s":""); r.rttMs=(uint32_t)p.avgMs; }
      break;
    }
  }
  return r;
}
