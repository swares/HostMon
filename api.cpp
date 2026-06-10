/* api.cpp — HTTPS JSON API + static dashboard, with Basic Auth + validation. */
#include "api.h"
#include "config.h"
#include "store.h"
#include "settings.h"
#include "validate.h"
#include "csv.h"
#include "notifier.h"
#include "wifi_portal.h"
#include "web_assets.h"          // firmware-embedded gzip dashboard
#include <ArduinoJson.h>
#include <string>
#include <HTTPServer.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <ResourceNode.hpp>

using namespace httpsserver;

// ---- auth ------------------------------------------------------------------
// Constant-time comparison: avoids leaking how many leading bytes of the password
// matched via response timing. (Length is allowed to differ-fast; that's standard.)
static bool ctEqual(const std::string& a, const std::string& b){
  if(a.size()!=b.size()) return false;
  uint8_t d=0; for(size_t i=0;i<a.size();i++) d |= (uint8_t)a[i] ^ (uint8_t)b[i];
  return d==0;
}
bool Api::authed(HTTPRequest* req, HTTPResponse* res){
  // CSRF defence: a browser always attaches an Origin header to cross-site POSTs.
  // If Origin is present and its host doesn't match our own Host header, this is a
  // cross-origin request driven by another site — reject it. Non-browser clients
  // (curl/scripts) send no Origin and are unaffected; Basic Auth still applies.
  std::string origin=req->getHeader("Origin");
  if(!origin.empty()){
    std::string host=req->getHeader("Host");
    size_t s=origin.find("://"); std::string oh=(s==std::string::npos)?origin:origin.substr(s+3);
    if(oh!=host){
      res->setStatusCode(403); res->setHeader("Content-Type","application/json");
      res->print("{\"ok\":false,\"error\":\"cross-origin request blocked\"}");
      return false;
    }
  }
  std::string u=req->getBasicAuthUser(), p=req->getBasicAuthPassword();
  WebAuthCfg& a=Settings::auth();
  bool ok = ctEqual(u, std::string(a.user)) & ctEqual(p, std::string(a.pass));   // & = no short-circuit
  if(ok) return true;
  res->setStatusCode(401);
  res->setHeader("WWW-Authenticate","Basic realm=\"Host Monitor\", charset=\"UTF-8\"");
  res->setHeader("Content-Type","application/json");
  res->print("{\"ok\":false,\"error\":\"authentication required\"}");
  return false;
}

// ---- helpers ---------------------------------------------------------------
static const size_t MAX_BODY_BYTES = 8192;   // cap request bodies to avoid RAM exhaustion (DoS)
static std::string readBody(HTTPRequest* req){
  std::string body; uint8_t buf[512];
  while(!req->requestComplete()){
    size_t n=req->readBytes(buf,sizeof(buf)); if(!n) break;
    if(body.size()+n > MAX_BODY_BYTES) break;     // over cap → truncate; JSON parse then 400s
    body.append((char*)buf,n);
  }
  return body;
}
static void sendJson(HTTPResponse* res, JsonDocument& d, int code=200){
  res->setStatusCode(code); res->setHeader("Content-Type","application/json");
  std::string out; serializeJson(d,out); res->printStd(out);
}
static void sendErr(HTTPResponse* res, int code, const char* msg){
  DynamicJsonDocument d(160); d["ok"]=false; d["error"]=msg; sendJson(res,d,code);
}
static void sendOk(HTTPResponse* res){ DynamicJsonDocument d(64); d["ok"]=true; sendJson(res,d); }
static bool param(HTTPRequest* req, const char* k, std::string& v){
  return req->getParams()->getQueryParameter(k, v);
}
// parse the request body into doc; on error sends 400 and returns false.
static bool body(HTTPRequest* req, HTTPResponse* res, DynamicJsonDocument& doc){
  std::string b=readBody(req);
  if(deserializeJson(doc, b)){ sendErr(res,400,"invalid JSON"); return false; }
  return true;
}

// ---- host serialisation ----------------------------------------------------
static void hostToJson(Host* h, JsonObject o){
  o["id"]=h->id; o["name"]=h->name; o["addr"]=h->addr; o["group"]=h->group;
  o["status"]=statusKey(h->status); o["up"]=h->upStr; o["rtt"]=h->rtt; o["last"]=h->last;
  if(h->msg[0]) o["msg"]=h->msg;
  o["fails"]=h->fails;
  if(h->status==Status::Ack){ o["ackBy"]=h->ackBy; o["ackReason"]=h->ackReason; o["ackAt"]=h->ackAt; }
  if(h->status==Status::Paused){ o["pauseBy"]=h->pauseBy; o["pauseReason"]=h->pauseReason; o["pauseUntil"]=h->pauseUntil; }
  JsonObject al=o.createNestedObject("alerts");
  al["down"]=h->alertDown; al["warn"]=h->alertWarn; al["recovered"]=h->alertRecovered;
  JsonArray ck=o.createNestedArray("checks");
  for(uint8_t i=0;i<kCheckCount;i++){
    Check& c=h->checks[i]; JsonObject co=ck.createNestedObject();
    co["key"]=checkMeta((CheckKind)i).key; co["state"]=checkStateKey(c.state);
    co["detail"]=c.detail; co["enabled"]=c.enabled; co["port"]=c.port;
    if((CheckKind)i==CheckKind::Http) co["secure"]=c.secure;   // HTTPS vs HTTP (HTTPS is always insecure)
    uint32_t every=c.every?c.every:defaultInterval((CheckKind)i);
    co["every"]=every; co["isDefault"]=(every==defaultInterval((CheckKind)i));
    JsonArray hist=co.createNestedArray("hist");
    for(uint8_t j=0;j<c.histLen;j++) hist.add(c.hist[j]);
  }
}

// ---- GET -------------------------------------------------------------------
static void getSummary(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return;
  Store::recount(); Summary s=Store::summary();
  DynamicJsonDocument d(1024);
  d["total"]=s.total; d["up"]=s.up; d["warn"]=s.warn; d["down"]=s.down;
  d["paused"]=s.paused; d["ack"]=s.ack; d["attention"]=s.attention; d["uptime30"]=s.uptime30;
  d["clock"]=Settings::clockHHMM(); d["date"]=Settings::dateShort(); d["dateLong"]=Settings::dateLongUpper();
  JsonObject dev=d.createNestedObject("device");
  dev["name"]=DEVICE_NAME; dev["fw"]=FIRMWARE_VERSION; dev["lcdHome"]=String(Settings::defaults().lcdHome); dev["time"]=Settings::timeSource();
  JsonObject net=d.createNestedObject("net");
  net["ap"]=WifiPortal::isAP(); net["online"]=WifiPortal::isOnline(); net["ip"]=WifiPortal::ipString();
  net["ssid"]=Settings::wifi().ssid;
  net["email"]=Settings::email().enabled; net["webhook"]=Settings::webhook().enabled;
  sendJson(res,d);
}
static void getHosts(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return;
  res->setStatusCode(200); res->setHeader("Content-Type","application/json");
  res->print("{\"hosts\":[");
  Store::lock();
  for(int i=0;i<Store::count();i++){
    if(i) res->print(",");
    DynamicJsonDocument d(2048); hostToJson(Store::at(i), d.to<JsonObject>());
    std::string out; serializeJson(d,out); res->printStd(out);
  }
  Store::unlock();
  res->print("]}");
}
static void getHost(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return;
  std::string id; if(!param(req,"id",id)){ sendErr(res,400,"missing id"); return; }
  Store::lock(); Host* h=Store::byId(id.c_str());
  if(!h){ Store::unlock(); sendErr(res,404,"no such host"); return; }
  DynamicJsonDocument d(2048); hostToJson(h,d.to<JsonObject>()); Store::unlock(); sendJson(res,d);
}
static void getAlerts(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return;
  DynamicJsonDocument d(JSON_DOC_LARGE); JsonArray arr=d.createNestedArray("alerts");
  for(int i=0;i<Store::alertCount();i++){ AlertEvt a=Store::alertAt(i); JsonObject o=arr.createNestedObject();
    o["time"]=a.time; o["host"]=a.host; o["check"]=a.check; o["sev"]=checkStateKey(a.sev);
    o["label"]=a.label; o["msg"]=a.msg; o["state"]= a.state==0?"firing":a.state==1?"ack":"resolved";
    JsonArray ch=o.createNestedArray("channels"); if(a.viaEmail)ch.add("email"); if(a.viaWebhook)ch.add("webhook"); }
  sendJson(res,d);
}
static void getSettings(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return;
  EmailCfg& e=Settings::email(); WebhookCfg& w=Settings::webhook(); Defaults& df=Settings::defaults();
  DynamicJsonDocument d(JSON_DOC_MED);
  JsonObject jd=d.createNestedObject("defaults"); JsonArray iv=jd.createNestedArray("interval");
  for(int i=0;i<kCheckCount;i++) iv.add(df.interval[i]);
  jd["fails"]=df.failsBeforeAlert; jd["lcdHome"]=String(df.lcdHome);
  jd["renotify"]=df.renotify; jd["renotifyEvery"]=df.renotifyEvery;
  JsonObject je=d.createNestedObject("email");
  je["enabled"]=e.enabled; je["server"]=String(e.host)+":"+e.port; je["host"]=e.host; je["port"]=e.port;
  je["from"]=e.from; je["to"]=e.to; je["lastTest"]=e.lastTest; je["ok"]=e.lastOk;
  JsonArray ew=je.createNestedArray("when");
  if(e.whenDown)ew.add("down"); if(e.whenWarn)ew.add("warn"); if(e.whenRecovered)ew.add("recovered");
  JsonObject jw=d.createNestedObject("webhook");
  jw["enabled"]=w.enabled; jw["url"]=w.url; jw["method"]=w.method; jw["header"]=w.header; jw["last"]=w.last; jw["ok"]=w.lastOk;
  JsonArray ww=jw.createNestedArray("when");
  if(w.whenDown)ww.add("down"); if(w.whenWarn)ww.add("warn"); if(w.whenRecovered)ww.add("recovered");
  if(w.whenAck)ww.add("ack"); if(w.whenPaused)ww.add("paused");
  JsonObject sd=d.createNestedObject("sd");
  sd["file"]=HOSTS_CSV_PATH; sd["count"]=Store::count(); sd["synced"]=Storage::sdReady();
  JsonObject dev=d.createNestedObject("device");
  dev["name"]=DEVICE_NAME; dev["fw"]=FIRMWARE_VERSION; dev["ip"]=WifiPortal::ipString();
  dev["ssid"]=Settings::wifi().ssid; dev["ap"]=WifiPortal::isAP(); dev["user"]=Settings::auth().user;
  sendJson(res,d);
}
static void getStatus(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return;
  DynamicJsonDocument d(192); d["ap"]=WifiPortal::isAP(); d["online"]=WifiPortal::isOnline();
  d["ip"]=WifiPortal::ipString(); d["ssid"]=Settings::wifi().ssid; sendJson(res,d);
}
static void getScan(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return;
  String j=WifiPortal::scanJson();
  res->setStatusCode(200); res->setHeader("Content-Type","application/json"); res->print(j.c_str());
}

// ---- POST ------------------------------------------------------------------
static void postAck(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(JSON_DOC_MED); if(!body(req,res,d)) return;
  const char* id=d["id"]|""; const char* reason=d["reason"]|""; const char* who=d["who"]|""; char err[64];
  if(!*id){ sendErr(res,400,"id required"); return; }
  if(!Valid::reason(reason,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Valid::who(who,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Store::ack(id,reason,who)){ sendErr(res,404,"no such host"); return; } sendOk(res);
}
static void postPause(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(JSON_DOC_MED); if(!body(req,res,d)) return;
  const char* id=d["id"]|""; const char* reason=d["reason"]|""; const char* until=d["until"]|"Until resumed"; const char* who=d["who"]|""; char err[64];
  if(!*id){ sendErr(res,400,"id required"); return; }
  if(!Valid::reason(reason,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Valid::who(who,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(strlen(until)>23){ sendErr(res,400,"duration too long"); return; }
  if(!Store::pause(id,reason,until,who)){ sendErr(res,404,"no such host"); return; } sendOk(res);
}
static void postResume(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(512); if(!body(req,res,d)) return;
  const char* id=d["id"]|""; if(!*id){ sendErr(res,400,"id required"); return; }
  if(!Store::resume(id)){ sendErr(res,404,"no such host"); return; } sendOk(res);
}
static void postClear(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(512); if(!body(req,res,d)) return;
  const char* id=d["id"]|""; if(!*id){ sendErr(res,400,"id required"); return; }
  if(!Store::clearAck(id)){ sendErr(res,404,"no such host"); return; } sendOk(res);
}
static void postInterval(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(512); if(!body(req,res,d)) return;
  const char* id=d["id"]|""; const char* key=d["key"]|""; char err[64];
  if(!*id||!*key){ sendErr(res,400,"id and key required"); return; }
  if(!d.containsKey("every")){ sendErr(res,400,"every required"); return; }
  long every=d["every"]|0; CheckKind k;
  if(!Valid::checkKind(key,k,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Valid::interval(every,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Store::setEvery(id,k,(uint32_t)every)){ sendErr(res,404,"no such host"); return; }
  Csv::saveHosts(HOSTS_CSV_PATH); sendOk(res);
}
static void postHost(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(JSON_DOC_MED); if(!body(req,res,d)) return;
  char err[64];
  const char* name=d["name"]|""; const char* addr=d["addr"]|""; const char* group=d["group"]|"Apps";
  if(!Valid::hostName(name,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Valid::address(addr,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Valid::groupName(group,err,sizeof(err))){ sendErr(res,400,err); return; }
  Store::lock();
  Host* h=nullptr; const char* id=d["id"]|"";
  if(*id){ h=Store::byId(id); if(!h){ Store::unlock(); sendErr(res,404,"no such host"); return; } }
  else { h=Store::add(); if(!h){ Store::unlock(); sendErr(res,507,"host list full"); return; } }
  strlcpy(h->name,name,sizeof(h->name)); strlcpy(h->addr,addr,sizeof(h->addr)); strlcpy(h->group,group,sizeof(h->group));
  if(d.containsKey("alerts")){ JsonObject a=d["alerts"];
    h->alertDown=a["down"]|true; h->alertWarn=a["warn"]|false; h->alertRecovered=a["recovered"]|true; }
  if(d.containsKey("checks")){
    for(JsonObject c : d["checks"].as<JsonArray>()){
      const char* ck=c["key"]|""; CheckKind k;
      if(!Valid::checkKind(ck,k,err,sizeof(err))){ Store::unlock(); sendErr(res,400,err); return; }
      long every=c["every"]|(long)defaultInterval(k);
      if(!Valid::interval(every,err,sizeof(err))){ Store::unlock(); sendErr(res,400,err); return; }
      long port=c["port"]|0;
      if(port!=0 && !Valid::port(port,err,sizeof(err))){ Store::unlock(); sendErr(res,400,err); return; }
      Check& chk=h->checks[(uint8_t)k]; chk.enabled=c["enabled"]|false; chk.every=(uint32_t)every; chk.port=(uint16_t)port;
      // HTTP check scheme: explicit `secure` flag from the form; default HTTPS when
      // the client doesn't send it (new checks default to HTTPS).
      if(k==CheckKind::Http) chk.secure = c["secure"] | true;   // HTTPS host checks are always insecure
      if(!chk.enabled){ chk.state=CheckState::Off; strlcpy(chk.detail,"—",sizeof(chk.detail)); }
      else if(chk.state==CheckState::Off){ chk.state=CheckState::Up; strlcpy(chk.detail,"pending…",sizeof(chk.detail)); chk.lastRun=0; }
    }
  }
  Store::unlock(); Store::recount(); Csv::saveHosts(HOSTS_CSV_PATH);
  DynamicJsonDocument r(128); r["ok"]=true; r["id"]=h->id; sendJson(res,r);
}
static void postHostDelete(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(512); if(!body(req,res,d)) return;
  const char* id=d["id"]|""; if(!*id){ sendErr(res,400,"id required"); return; }
  if(!Store::removeById(id)){ sendErr(res,404,"no such host"); return; }
  Store::recount(); Csv::saveHosts(HOSTS_CSV_PATH); sendOk(res);
}
static void postEmail(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(JSON_DOC_MED); if(!body(req,res,d)) return;
  EmailCfg& e=Settings::email(); char err[64];
  const char* host=d["host"]|e.host; const char* from=d["from"]|e.from; const char* to=d["to"]|e.to;
  if(strlen(host)>63){ sendErr(res,400,"server too long"); return; }
  if(!Valid::email(from,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Valid::email(to,err,sizeof(err))){ sendErr(res,400,err); return; }
  long port=d["port"]|e.port; if(!Valid::inRange(port,1,65535)){ sendErr(res,400,"bad SMTP port"); return; }
  strlcpy(e.host,host,sizeof(e.host)); e.port=(uint16_t)port; strlcpy(e.from,from,sizeof(e.from)); strlcpy(e.to,to,sizeof(e.to));
  if(d.containsKey("user")) strlcpy(e.user,d["user"]|"",sizeof(e.user));
  if(d.containsKey("pass")) strlcpy(e.pass,d["pass"]|"",sizeof(e.pass));
  if(d.containsKey("enabled")) e.enabled=d["enabled"];
  if(d.containsKey("when")){ e.whenDown=e.whenWarn=e.whenRecovered=false;
    for(const char* w : d["when"].as<JsonArray>()){ if(!strcmp(w,"down"))e.whenDown=true; else if(!strcmp(w,"warn"))e.whenWarn=true;
      else if(!strcmp(w,"recovered"))e.whenRecovered=true; } }
  Settings::save(); sendOk(res);
}
static void postWebhook(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(JSON_DOC_MED); if(!body(req,res,d)) return;
  WebhookCfg& w=Settings::webhook(); char err[64];
  const char* url=d["url"]|w.url; if(!Valid::url(url,err,sizeof(err))){ sendErr(res,400,err); return; }
  const char* method=d["method"]|w.method;
  if(strcmp(method,"POST")&&strcmp(method,"PUT")){ sendErr(res,400,"method must be POST or PUT"); return; }
  const char* header=d["header"]|w.header;
  if(strlen(header)>119){ sendErr(res,400,"header too long"); return; }
  if(strpbrk(header,"\r\n")){ sendErr(res,400,"header has invalid characters"); return; }   // no header injection
  strlcpy(w.url,url,sizeof(w.url)); strlcpy(w.method,method,sizeof(w.method)); strlcpy(w.header,header,sizeof(w.header));
  if(d.containsKey("enabled")) w.enabled=d["enabled"];
  if(d.containsKey("when")){ w.whenDown=w.whenWarn=w.whenRecovered=w.whenAck=w.whenPaused=false;
    for(const char* x : d["when"].as<JsonArray>()){ if(!strcmp(x,"down"))w.whenDown=true; else if(!strcmp(x,"warn"))w.whenWarn=true;
      else if(!strcmp(x,"recovered"))w.whenRecovered=true; else if(!strcmp(x,"ack"))w.whenAck=true;
      else if(!strcmp(x,"paused"))w.whenPaused=true; } }
  Settings::save(); sendOk(res);
}
static void postDefaults(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(JSON_DOC_MED); if(!body(req,res,d)) return;
  Defaults& df=Settings::defaults(); char err[64];
  if(d.containsKey("interval")){ int i=0; for(long v : d["interval"].as<JsonArray>()){ if(i>=kCheckCount)break;
      if(!Valid::interval(v,err,sizeof(err))){ sendErr(res,400,err); return; } df.interval[i++]=(uint32_t)v; } }
  if(d.containsKey("fails")){ long f=d["fails"]; if(f!=1&&f!=3&&f!=5){ sendErr(res,400,"fails must be 1, 3 or 5"); return; } df.failsBeforeAlert=(uint8_t)f; }
  if(d.containsKey("lcdHome")){ const char* lh=d["lcdHome"]|"A"; if(lh[0]!='A'&&lh[0]!='B'){ sendErr(res,400,"lcdHome must be A or B"); return; } df.lcdHome=lh[0]; }
  if(d.containsKey("renotify")) df.renotify=d["renotify"];
  if(d.containsKey("renotifyEvery")){ long re=d["renotifyEvery"]; if(!Valid::inRange(re,300,86400)){ sendErr(res,400,"bad re-notify interval"); return; } df.renotifyEvery=(uint32_t)re; }
  Settings::save(); sendOk(res);
}
static void postAuth(HTTPRequest* req, HTTPResponse* res){
  if(!Api::authed(req,res)) return; DynamicJsonDocument d(512); if(!body(req,res,d)) return;
  WebAuthCfg& a=Settings::auth();
  const char* user=d["user"]|a.user; const char* pass=d["pass"]|"";
  if(strlen(user)<1||strlen(user)>23){ sendErr(res,400,"user must be 1-23 chars"); return; }
  if(*pass){ size_t pl=strlen(pass); if(pl<8||pl>39){ sendErr(res,400,"password must be 8-39 chars"); return; } strlcpy(a.pass,pass,sizeof(a.pass)); a.autoGen=false; }
  strlcpy(a.user,user,sizeof(a.user)); Settings::save(); sendOk(res);
}
static void testEmail(HTTPRequest* req, HTTPResponse* res){ if(!Api::authed(req,res)) return;
  char e[80]; if(Notifier::testEmail(e,sizeof(e))) sendOk(res); else sendErr(res,502,e); }
static void testWebhook(HTTPRequest* req, HTTPResponse* res){ if(!Api::authed(req,res)) return;
  char e[80]; if(Notifier::testWebhook(e,sizeof(e))) sendOk(res); else sendErr(res,502,e); }
static void sdReload(HTTPRequest* req, HTTPResponse* res){ if(!Api::authed(req,res)) return;
  int n=Csv::loadHosts(HOSTS_CSV_PATH); Store::recount();
  DynamicJsonDocument d(96); d["ok"]=true; d["count"]=n; sendJson(res,d); }
static void wifiJoin(HTTPRequest* req, HTTPResponse* res){ if(!Api::authed(req,res)) return;
  DynamicJsonDocument d(512); if(!body(req,res,d)) return; char err[64];
  const char* ssid=d["ssid"]|""; const char* pass=d["pass"]|"";
  if(!Valid::ssid(ssid,err,sizeof(err))){ sendErr(res,400,err); return; }
  if(!Valid::wifiPass(pass,err,sizeof(err))){ sendErr(res,400,err); return; }
  sendOk(res); WifiPortal::joinNetwork(ssid,pass); }

// ---- static dashboard (default node) --------------------------------------
static const char* mime(const std::string& p){
  if(p.rfind(".html")!=std::string::npos) return "text/html";
  if(p.rfind(".css")!=std::string::npos)  return "text/css";
  if(p.rfind(".js")!=std::string::npos)   return "application/javascript";
  if(p.rfind(".json")!=std::string::npos) return "application/json";
  if(p.rfind(".svg")!=std::string::npos)  return "image/svg+xml";
  if(p.rfind(".ico")!=std::string::npos)  return "image/x-icon";
  return "text/plain";
}
static void staticHandler(HTTPRequest* req, HTTPResponse* res){
  std::string path=req->getRequestString();
  size_t q=path.find('?'); if(q!=std::string::npos) path=path.substr(0,q);
  // Bare root and OS captive-portal probes get the right shell page: the WiFi
  // setup page when unconfigured (AP mode), otherwise the dashboard. The static
  // shell is public; the /api/* data endpoints enforce Basic Auth, so the browser
  // prompts for admin/password on the first data fetch.
  if(path=="/"||path.empty()||path=="/generate_204"||path=="/hotspot-detect.html"||
     path=="/ncsi.txt"||path=="/connecttest.txt")
    path = WifiPortal::isAP() ? "/setup.html" : "/index.html";
  for(size_t i=0;i<WEB_ASSETS_COUNT;i++){
    if(path==WEB_ASSETS[i].path){
      res->setStatusCode(200);
      res->setHeader("Content-Type", WEB_ASSETS[i].mime);
      res->setHeader("Content-Encoding","gzip");   // assets are stored gzip-compressed
      res->write(WEB_ASSETS[i].data, WEB_ASSETS[i].len);
      return;
    }
  }
  res->setStatusCode(404); res->setHeader("Content-Type","application/json");
  res->print("{\"ok\":false,\"error\":\"not found\"}");
}

void Api::registerRoutes(HTTPServer* s){
  s->registerNode(new ResourceNode("/api/summary","GET",&getSummary));
  s->registerNode(new ResourceNode("/api/hosts","GET",&getHosts));
  s->registerNode(new ResourceNode("/api/host","GET",&getHost));
  s->registerNode(new ResourceNode("/api/alerts","GET",&getAlerts));
  s->registerNode(new ResourceNode("/api/settings","GET",&getSettings));
  s->registerNode(new ResourceNode("/api/status","GET",&getStatus));
  s->registerNode(new ResourceNode("/api/wifi/scan","GET",&getScan));

  s->registerNode(new ResourceNode("/api/host/ack","POST",&postAck));
  s->registerNode(new ResourceNode("/api/host/pause","POST",&postPause));
  s->registerNode(new ResourceNode("/api/host/resume","POST",&postResume));
  s->registerNode(new ResourceNode("/api/host/clear","POST",&postClear));
  s->registerNode(new ResourceNode("/api/host/interval","POST",&postInterval));
  s->registerNode(new ResourceNode("/api/host/delete","POST",&postHostDelete));
  s->registerNode(new ResourceNode("/api/host","POST",&postHost));
  s->registerNode(new ResourceNode("/api/settings/email","POST",&postEmail));
  s->registerNode(new ResourceNode("/api/settings/webhook","POST",&postWebhook));
  s->registerNode(new ResourceNode("/api/settings/defaults","POST",&postDefaults));
  s->registerNode(new ResourceNode("/api/settings/auth","POST",&postAuth));
  s->registerNode(new ResourceNode("/api/test/email","POST",&testEmail));
  s->registerNode(new ResourceNode("/api/test/webhook","POST",&testWebhook));
  s->registerNode(new ResourceNode("/api/sd/reload","POST",&sdReload));
  s->registerNode(new ResourceNode("/api/wifi/join","POST",&wifiJoin));

  s->setDefaultNode(new ResourceNode("","",&staticHandler));   // static dashboard
}
