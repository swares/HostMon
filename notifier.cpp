/* notifier.cpp — webhook (HTTPS/HTTP) alert delivery with routing rules.
 * (Email/SMTP was removed: ESP Mail Client's TLS footprint exceeded this board's
 *  free internal RAM. Route alerts via webhook to a downstream relay if you want
 *  email — see README §10.) */
#include "notifier.h"
#include "config.h"
#include "settings.h"
#include "store.h"
#include "model.h"                  // CheckKind / CheckState / checkMeta
#include "tls_gate.h"               // serialize outbound TLS across the check + web tasks
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

namespace {
  void labelFor(const char* event, char* out, size_t n){
    if(!strcmp(event,"host.down")) strlcpy(out,"DOWN",n);
    else if(!strcmp(event,"host.warn")) strlcpy(out,"WARN",n);
    else if(!strcmp(event,"host.up")) strlcpy(out,"RECOVERED",n);
    else strlcpy(out,"INFO",n);
  }
  CheckState sevFor(const char* event){
    if(!strcmp(event,"host.down")) return CheckState::Down;
    if(!strcmp(event,"host.warn")) return CheckState::Warn;
    return CheckState::Up;
  }

  // Pick a representative comma list of the checks currently failing.
  void failingChecks(const Host& h, char* out, size_t n){
    out[0]=0; bool first=true;
    for(uint8_t i=0;i<kCheckCount;i++){
      if(h.checks[i].enabled && (h.checks[i].state==CheckState::Down||h.checks[i].state==CheckState::Warn)){
        size_t l=strlen(out); snprintf(out+l,n-l,"%s%s",first?"":",",checkMeta((CheckKind)i).key); first=false; }
    }
    if(!out[0]) strlcpy(out,"-",n);
  }

  bool sendWebhook(const char* payload, char* err, size_t en){
    WebhookCfg& w=Settings::webhook();
    if(!w.enabled || !w.url[0]){ if(err) strlcpy(err,"webhook disabled",en); return false; }
    bool https = strncmp(w.url,"https://",8)==0;
    // Only ONE outbound TLS session can fit in internal RAM at a time, and a "Send
    // test" webhook (web task) can race a cert check / alert send (check task), so
    // HTTPS sends take the global TLS gate for their duration. Plain-HTTP webhooks
    // don't engage it. Block up to 15 s for the slot, else report busy.
    TlsGate::Lock tls(https, 15000);
    if(!tls.ok()){ if(err) strlcpy(err, tls.lowMem()?"low memory — webhook deferred":"TLS busy, retry", en); return false; }
    // HTTPS webhooks are delivered insecurely (cert not validated): on-device
    // certificate verification isn't viable on this board — mbedTLS can't get its
    // session buffers from the free internal heap. See README §7.
    WiFiClientSecure sec; sec.setInsecure(); WiFiClient plain;
    HTTPClient http;
    bool began = https? http.begin(sec,w.url) : http.begin(plain,w.url);
    if(!began){ if(err) strlcpy(err,"bad webhook url",en); return false; }
    http.addHeader("Content-Type","application/json");
    if(w.header[0]){ char* c=strchr(w.header,':');
      if(c){ *c=0; http.addHeader(w.header, c+2); *c=':'; } }
    int code = (strcmp(w.method,"PUT")==0)? http.PUT((uint8_t*)payload,strlen(payload))
                                          : http.POST((uint8_t*)payload,strlen(payload));
    http.end();
    snprintf(w.last,sizeof(w.last),"%d · now",code);
    w.lastOk = (code>=200 && code<300);
    if(!w.lastOk && err) snprintf(err,en,"webhook HTTP %d",code);
    return w.lastOk;
  }
}

void Notifier::begin(){}

void Notifier::buildPayload(const Host& h, const char* event, const char* msg, char* out, size_t n){
  // Build with ArduinoJson so every field is properly escaped — a stray quote or
  // backslash in any value can never break out of the JSON (defence-in-depth).
  if(Settings::webhookFormat()==1){
    // M5Stack /api/alerts/inject schema: {slug,key,value,severity}. slug=this device,
    // key=host name (their key cap is 15), value=consecutive fails, severity from event.
    char slug[12]; strlcpy(slug, DEVICE_NAME, sizeof(slug));   // their slug cap is 11
    char key[16];  strlcpy(key,  h.name,      sizeof(key));    // their key cap is 15
    const char* sev = !strcmp(event,"host.down") ? "critical"
                    : !strcmp(event,"host.warn") ? "warn" : "info";
    StaticJsonDocument<192> d;
    d["slug"]=slug; d["key"]=key; d["value"]=h.fails; d["severity"]=sev;
    serializeJson(d, out, n);
    return;
  }
  // Native HostMonitor schema.
  char label[12]; labelFor(event,label,sizeof(label));
  char checks[48]; failingChecks(h,checks,sizeof(checks));
  char iso[28]; Settings::isoNow(iso,sizeof(iso));
  StaticJsonDocument<512> d;
  d["event"]=event; d["host"]=h.name; d["address"]=h.addr; d["status"]=label;
  d["check"]=checks; d["message"]=msg; d["fails"]=h.fails; d["since"]=iso; d["device"]=DEVICE_NAME;
  serializeJson(d, out, n);
}

void Notifier::notify(const Host& h, const char* event, const char* msg){
  // Decide delivery from settings "send when" + per-host routing.
  WebhookCfg& w=Settings::webhook();
  bool isDown=!strcmp(event,"host.down"), isWarn=!strcmp(event,"host.warn"), isUp=!strcmp(event,"host.up");

  bool hook  = w.enabled && ((isDown&&w.whenDown)||(isWarn&&w.whenWarn)||(isUp&&w.whenRecovered));
  if(isDown && !h.alertDown)      hook=false;
  if(isWarn && !h.alertWarn)      hook=false;
  if(isUp   && !h.alertRecovered) hook=false;

  char payload[512]; buildPayload(h,event,msg,payload,sizeof(payload));
  char label[12]; labelFor(event,label,sizeof(label));
  char err[64];

  bool wOk=false;
  if(hook){ wOk=sendWebhook(payload,err,sizeof(err)); }

  // Record in the alert ring buffer for the dashboard/LCD.
  AlertEvt a; strlcpy(a.time,Settings::clockHHMM(),sizeof(a.time));
  strlcpy(a.host,h.name,sizeof(a.host));
  failingChecks(h,a.check,sizeof(a.check));
  a.sev=sevFor(event); strlcpy(a.label,label,sizeof(a.label));
  strlcpy(a.msg,msg,sizeof(a.msg)); a.state= isUp?2:0;
  a.viaEmail=false; a.viaWebhook=wOk; a.at=millis();
  Store::pushAlert(a);
  Settings::save();
}

bool Notifier::testWebhook(char* err, size_t en){
  char payload[256];
  if(Settings::webhookFormat()==1)
    snprintf(payload,sizeof(payload),
      "{\"slug\":\"%s\",\"key\":\"test\",\"value\":0,\"severity\":\"info\"}",DEVICE_NAME);
  else
    snprintf(payload,sizeof(payload),
      "{\"event\":\"test\",\"device\":\"%s\",\"message\":\"Host Monitor webhook test\"}",DEVICE_NAME);
  return sendWebhook(payload, err, en);
}
