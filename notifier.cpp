/* notifier.cpp — SMTP + webhook delivery with routing rules. */
#include "notifier.h"
#include "config.h"
#include "settings.h"
#include "store.h"
#include "model.h"                  // CheckKind / CheckState / checkMeta
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP_Mail_Client.h>
#include <ArduinoJson.h>

namespace {
  SMTPSession smtp;

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

  bool sendEmail(const char* subject, const char* body, char* err, size_t en){
    EmailCfg& e=Settings::email();
    if(!e.enabled || !e.host[0] || !e.to[0]){ if(err) strlcpy(err,"email disabled",en); return false; }
    // The mail library manages its own TLS. On-device certificate verification isn't
    // viable on this board (mbedTLS session buffers exceed the free internal heap),
    // so the SMTP server certificate is not pre-validated. See README §7.
    Session_Config cfg;
    cfg.server.host_name = e.host; cfg.server.port = e.port;
    cfg.login.email = e.user[0]? e.user : e.from; cfg.login.password = e.pass;
    cfg.login.user_domain = "";
    SMTP_Message msg;
    msg.sender.name = "Host Monitor"; msg.sender.email = e.from;
    msg.subject = subject; msg.addRecipient("", e.to);
    msg.text.content = body; msg.text.charSet="utf-8";
    if(!smtp.connect(&cfg)){ if(err) strlcpy(err,"SMTP connect failed",en); return false; }
    bool ok = MailClient.sendMail(&smtp, &msg);
    smtp.closeSession();
    e.lastOk=ok; strlcpy(e.lastTest,"now",sizeof(e.lastTest));
    if(!ok && err) strlcpy(err,smtp.errorReason().c_str(),en);
    return ok;
  }
}

void Notifier::begin(){ smtp.debug(0); MailClient.networkReconnect(true); }

void Notifier::buildPayload(const Host& h, const char* event, const char* msg, char* out, size_t n){
  char label[12]; labelFor(event,label,sizeof(label));
  char checks[48]; failingChecks(h,checks,sizeof(checks));
  char iso[28]; Settings::isoNow(iso,sizeof(iso));
  // Build with ArduinoJson so every field is properly escaped — a stray quote or
  // backslash in any value can never break out of the JSON (defence-in-depth).
  StaticJsonDocument<512> d;
  d["event"]=event; d["host"]=h.name; d["address"]=h.addr; d["status"]=label;
  d["check"]=checks; d["message"]=msg; d["fails"]=h.fails; d["since"]=iso; d["device"]=DEVICE_NAME;
  serializeJson(d, out, n);
}

void Notifier::notify(const Host& h, const char* event, const char* msg){
  // Decide channels from settings "send when" + per-host routing.
  EmailCfg& e=Settings::email(); WebhookCfg& w=Settings::webhook();
  bool isDown=!strcmp(event,"host.down"), isWarn=!strcmp(event,"host.warn"), isUp=!strcmp(event,"host.up");

  bool email = e.enabled && ((isDown&&e.whenDown)||(isWarn&&e.whenWarn)||(isUp&&e.whenRecovered));
  bool hook  = w.enabled && ((isDown&&w.whenDown)||(isWarn&&w.whenWarn)||(isUp&&w.whenRecovered));
  if(isDown && !h.alertDown){ email=hook=false; }
  if(isWarn && !h.alertWarn){ email=hook=false; }
  if(isUp   && !h.alertRecovered){ email=hook=false; }

  char payload[512]; buildPayload(h,event,msg,payload,sizeof(payload));
  char label[12]; labelFor(event,label,sizeof(label));
  char err[64];

  bool eOk=false,wOk=false;
  if(email){ char subj[96]; snprintf(subj,sizeof(subj),"[%s] %s — %s",DEVICE_NAME,label,h.name);
             eOk=sendEmail(subj,payload,err,sizeof(err)); }
  if(hook){ wOk=sendWebhook(payload,err,sizeof(err)); }

  // Record in the alert ring buffer for the dashboard/LCD.
  AlertEvt a; strlcpy(a.time,Settings::clockHHMM(),sizeof(a.time));
  strlcpy(a.host,h.name,sizeof(a.host));
  failingChecks(h,a.check,sizeof(a.check));
  a.sev=sevFor(event); strlcpy(a.label,label,sizeof(a.label));
  strlcpy(a.msg,msg,sizeof(a.msg)); a.state= isUp?2:0;
  a.viaEmail=eOk; a.viaWebhook=wOk; a.at=millis();
  Store::pushAlert(a);
  Settings::save();
}

bool Notifier::testEmail(char* err, size_t en){
  return sendEmail("Host Monitor test", "This is a test alert from Host Monitor.", err, en);
}
bool Notifier::testWebhook(char* err, size_t en){
  char payload[256];
  snprintf(payload,sizeof(payload),
    "{\"event\":\"test\",\"device\":\"%s\",\"message\":\"Host Monitor webhook test\"}",DEVICE_NAME);
  return sendWebhook(payload, err, en);
}
