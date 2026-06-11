/* ============================================================================
 *  checks.cpp — ping / dns / port / http / trace implementations.
 *
 *  All checks are blocking and must run on the dedicated check task (not the
 *  LVGL or web task). Each returns a CheckResult with a derived state and a
 *  short human detail string mirroring the dashboard's wording.
 * ========================================================================== */
#include "checks.h"
#include "config.h"
#include "tls_gate.h"            // one outbound TLS session at a time
#include "wifi_portal.h"        // skip the TLS self-test in AP mode (no internet)
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include "ping/ping_sock.h"
#include <mbedtls/ssl.h>          // one-shot TLS self-test (Checks::tlsSelfTest)
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <esp_heap_caps.h>        // largest-free-block readout for the self-test
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
// SSL/TLS cert-expiry probe — INSECURE handshake (VERIFY_NONE: accept any cert),
// read the peer cert's notAfter, return days-to-expiry. Insecure is deliberate: we
// only read the expiry date, and on-device chain validation (CA bundle) costs more
// RAM than this board can spare. Caller MUST hold the TLS gate and have a valid clock.
//   stage: "tcp" connect failed · "tls" handshake failed · "cert" no peer cert · "ok"
// timegm() isn't available in this newlib, and mktime() depends on TZ being UTC.
// Convert a UTC broken-down date to a Unix epoch directly (Howard Hinnant's
// days-from-civil algorithm) — exact and timezone-independent.
static time_t utcEpoch(int Y,int M,int D,int hh,int mm,int ss){
  int y = Y - (M <= 2);
  long era = (y >= 0 ? y : y-399) / 400;
  unsigned yoe = (unsigned)(y - era*400);
  unsigned doy = (unsigned)((153*(M + (M>2 ? -3 : 9)) + 2)/5 + D - 1);
  unsigned doe = yoe*365 + yoe/4 - yoe/100 + doy;
  long days = era*146097 + (long)doe - 719468;        // days since 1970-01-01 (UTC)
  return (time_t)days*86400 + hh*3600 + mm*60 + ss;
}

namespace { struct CertProbe { const char* stage; int days; }; }
static CertProbe certProbe(const char* host, uint16_t port){
  CertProbe cp{ "tcp", -1 };
  // Bounded TCP preflight — mbedtls_net_connect() has no connect timeout.
  { WiFiClient pre; bool up=pre.connect(host,port,CONNECT_TIMEOUT_MS); pre.stop(); if(!up) return cp; }
  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_entropy_context ent; mbedtls_ctr_drbg_context drbg;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&drbg);
  char portStr[8]; snprintf(portStr,sizeof(portStr),"%u",port);
  const char* pers="hostmon-cert";
  if(mbedtls_ctr_drbg_seed(&drbg,mbedtls_entropy_func,&ent,(const unsigned char*)pers,strlen(pers))!=0){ cp.stage="tls"; goto done; }
  if(mbedtls_net_connect(&net,host,portStr,MBEDTLS_NET_PROTO_TCP)!=0){ cp.stage="tcp"; goto done; }
  cp.stage="tls";
  if(mbedtls_ssl_config_defaults(&conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0) goto done;
  mbedtls_ssl_conf_authmode(&conf,MBEDTLS_SSL_VERIFY_NONE);   // insecure: read expiry only
  mbedtls_ssl_conf_rng(&conf,mbedtls_ctr_drbg_random,&drbg);
  if(mbedtls_ssl_setup(&ssl,&conf)!=0) goto done;
  mbedtls_ssl_set_hostname(&ssl,host);
  mbedtls_ssl_set_bio(&ssl,&net,mbedtls_net_send,mbedtls_net_recv,nullptr);
  { int r; while((r=mbedtls_ssl_handshake(&ssl))!=0){
      if(r!=MBEDTLS_ERR_SSL_WANT_READ && r!=MBEDTLS_ERR_SSL_WANT_WRITE) goto done; } }
  cp.stage="cert";
  { const mbedtls_x509_crt* crt=mbedtls_ssl_get_peer_cert(&ssl);
    if(crt){
      const mbedtls_x509_time& t=crt->valid_to;
      time_t exp=utcEpoch(t.year,t.mon,t.day,t.hour,t.min,t.sec);   // notAfter is UTC
      time_t now=time(nullptr);
      cp.days=(int)((exp-now)/86400); cp.stage="ok";   // caller verified the clock is set
    } }
done:
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
  return cp;
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
      // For HTTPS, take the single-TLS-session gate (a concurrent notifier/test send
      // would otherwise collide on internal RAM); plain HTTP doesn't engage it.
      TlsGate::Lock tls(secure, 15000);
      if(!tls.ok()){ r.state=CheckState::Warn; strlcpy(r.detail, tls.lowMem()?"low mem":"tls busy", sizeof(r.detail)); break; }
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
    case CheckKind::Ssl: {
      const Check& sc = h.checks[(uint8_t)CheckKind::Ssl];
      uint16_t port = sc.port ? sc.port : 443;
      // Needs a real wall clock to compute days-to-expiry (the RTC is disabled on this
      // board, so if NTP hasn't set the clock yet, say so instead of a bogus number).
      if(time(nullptr) < 100000){
        r.state=CheckState::Warn; strlcpy(r.detail,"no clock — set NTP",sizeof(r.detail)); break; }
      // Single TLS session at a time (a concurrent notifier/test send would collide).
      TlsGate::Lock tls(true, 15000);
      if(!tls.ok()){ r.state=CheckState::Warn; strlcpy(r.detail, tls.lowMem()?"low mem":"tls busy", sizeof(r.detail)); break; }
      CertProbe cp = certProbe(h.addr, port);
      if(!strcmp(cp.stage,"ok")){
        if(cp.days<0){ r.state=CheckState::Down; snprintf(r.detail,sizeof(r.detail),"expired %dd ago",-cp.days); }
        else if(cp.days<=SSL_WARN_DAYS){ r.state=CheckState::Warn; snprintf(r.detail,sizeof(r.detail),"%dd — renew",cp.days); }
        else { r.state=CheckState::Up; snprintf(r.detail,sizeof(r.detail),"%dd left",cp.days); }
      } else if(!strcmp(cp.stage,"tcp")){
        r.state=CheckState::Down; snprintf(r.detail,sizeof(r.detail),"no TCP :%u",port);
      } else if(!strcmp(cp.stage,"cert")){
        r.state=CheckState::Down; strlcpy(r.detail,"no peer cert",sizeof(r.detail));
      } else {
        r.state=CheckState::Down; strlcpy(r.detail,"TLS handshake fail",sizeof(r.detail));
      }
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

// ---------------------------------------------------------------------------
// One-shot on-device TLS self-test. Attempts a full INSECURE (VERIFY_NONE) mbedTLS
// handshake to TLS_SELFTEST_HOST and records, on the Setup card, exactly where it
// got and the largest contiguous free internal block at the key moments. Insecure
// mode isolates the question we care about — can the chip allocate + run a TLS
// session at all on the current RAM headroom — without CA-bundle/clock variables.
// If this succeeds, verified TLS (cert-expiry check, notifier verify) is realistic.
static char g_tlsTest[96] = "TLS test: pending…";

void Checks::tlsSelfTest(){
  const char* host = TLS_SELFTEST_HOST;
  uint16_t    port = TLS_SELFTEST_PORT;
  if(!host[0]){ strlcpy(g_tlsTest,"TLS test: off",sizeof(g_tlsTest)); return; }
  // No point probing the internet from the first-run captive AP (no uplink).
  if(WifiPortal::isAP()){ strlcpy(g_tlsTest,"TLS test: skipped (AP mode)",sizeof(g_tlsTest)); return; }

  // Hold the single-TLS-session gate for the probe so it can't overlap a notifier
  // send (which would put two ~32 KB sessions in internal RAM at once).
  TlsGate::Lock tls(true, 10000);
  if(!tls.ok()){ strlcpy(g_tlsTest, tls.lowMem()?"TLS test: skipped (low mem)":"TLS test: busy (slot in use)", sizeof(g_tlsTest)); return; }

  auto lbk = [](){ return (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)/1024); };
  unsigned lbPre = lbk(), lbFail = lbPre;
  const char* stage = "init"; int rc = 0; bool certSeen = false;

  // Bounded TCP preflight — mbedtls_net_connect() has no connect timeout.
  { WiFiClient pre; bool up = pre.connect(host,port,CONNECT_TIMEOUT_MS); pre.stop();
    if(!up){ snprintf(g_tlsTest,sizeof(g_tlsTest),"TLS %s:%u FAIL no-TCP (unreachable) lb=%uk",host,port,lbPre); return; } }

  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_entropy_context ent; mbedtls_ctr_drbg_context drbg;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&drbg);
  char portStr[8]; snprintf(portStr,sizeof(portStr),"%u",port);
  const char* pers = "hostmon-tlstest";

  if((rc=mbedtls_ctr_drbg_seed(&drbg,mbedtls_entropy_func,&ent,(const unsigned char*)pers,strlen(pers)))!=0){ stage="drbg"; lbFail=lbk(); goto done; }
  if(mbedtls_net_connect(&net,host,portStr,MBEDTLS_NET_PROTO_TCP)!=0){ stage="connect"; lbFail=lbk(); goto done; }
  if((rc=mbedtls_ssl_config_defaults(&conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT))!=0){ stage="config"; lbFail=lbk(); goto done; }
  mbedtls_ssl_conf_authmode(&conf,MBEDTLS_SSL_VERIFY_NONE);     // insecure: this is a RAM/handshake test
  mbedtls_ssl_conf_rng(&conf,mbedtls_ctr_drbg_random,&drbg);
  if((rc=mbedtls_ssl_setup(&ssl,&conf))!=0){ stage="ssl_setup"; lbFail=lbk(); goto done; }  // the buffer alloc (~16k x2)
  mbedtls_ssl_set_hostname(&ssl,host);
  mbedtls_ssl_set_bio(&ssl,&net,mbedtls_net_send,mbedtls_net_recv,nullptr);
  { int r; while((r=mbedtls_ssl_handshake(&ssl))!=0){
      if(r!=MBEDTLS_ERR_SSL_WANT_READ && r!=MBEDTLS_ERR_SSL_WANT_WRITE){ rc=r; stage="handshake"; lbFail=lbk(); goto done; } } }
  certSeen = (mbedtls_ssl_get_peer_cert(&ssl)!=nullptr);
  stage = "ok";
done:
  if(!strcmp(stage,"ok"))
    snprintf(g_tlsTest,sizeof(g_tlsTest),"TLS %s:%u OK(insec) handshake ok cert=%s lb %uk->%uk",
             host,port,certSeen?"yes":"no",lbPre,lbk());     // lbk() here = after session, before free
  else
    snprintf(g_tlsTest,sizeof(g_tlsTest),"TLS %s FAIL %s -0x%04X lb pre=%uk now=%uk",
             host,stage,(unsigned)(-rc),lbPre,lbFail);
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
}

const char* Checks::tlsSelfTestResult(){ return g_tlsTest; }
