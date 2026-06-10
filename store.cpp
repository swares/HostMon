/* store.cpp — implementation of the in-memory host store. */
#include "store.h"
#include "settings.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
  Host      g_hosts[MAX_HOSTS];
  int       g_count = 0;
  Summary   g_sum;
  AlertEvt  g_alerts[ALERT_LOG_MAX];
  int       g_alertN = 0;        // total pushed
  SemaphoreHandle_t g_mtx = nullptr;
  volatile bool g_dirty = true;
  uint32_t  g_idSeq = 0;
}

void Store::begin(){ if(!g_mtx) g_mtx = xSemaphoreCreateRecursiveMutex(); }
void Store::lock(){ if(g_mtx) xSemaphoreTakeRecursive(g_mtx, portMAX_DELAY); }
void Store::unlock(){ if(g_mtx) xSemaphoreGiveRecursive(g_mtx); }

int   Store::count(){ return g_count; }
Host* Store::at(int i){ return (i>=0 && i<g_count)? &g_hosts[i] : nullptr; }

Host* Store::byId(const char* id){
  for(int i=0;i<g_count;i++) if(!strcmp(g_hosts[i].id,id)) return &g_hosts[i];
  return nullptr;
}
Host* Store::byName(const char* name){
  for(int i=0;i<g_count;i++) if(!strcmp(g_hosts[i].name,name)) return &g_hosts[i];
  return nullptr;
}
Host* Store::add(){
  if(g_count>=MAX_HOSTS) return nullptr;
  Host* h = &g_hosts[g_count++];
  *h = Host();
  snprintf(h->id,sizeof(h->id),"h%lu",(unsigned long)(++g_idSeq));
  // initialise the six checks with defaults
  for(uint8_t i=0;i<kCheckCount;i++){
    h->checks[i].kind  = (CheckKind)i;
    h->checks[i].every = defaultInterval((CheckKind)i);
    h->checks[i].state = CheckState::Off;
  }
  markDirty();
  return h;
}
bool Store::removeById(const char* id){
  for(int i=0;i<g_count;i++) if(!strcmp(g_hosts[i].id,id)){
    for(int j=i;j<g_count-1;j++) g_hosts[j]=g_hosts[j+1];
    g_count--; markDirty(); return true;
  }
  return false;
}
void Store::clear(){ g_count=0; g_idSeq=0; markDirty(); }

Summary Store::summary(){ return g_sum; }

void Store::recount(){
  lock();
  Summary s; s.total=g_count;
  for(int i=0;i<g_count;i++){
    switch(g_hosts[i].status){
      case Status::Up: s.up++; break;   case Status::Warn: s.warn++; break;
      case Status::Down: s.down++; break; case Status::Paused: s.paused++; break;
      case Status::Ack: s.ack++; break;
    }
  }
  s.attention = s.down + s.warn;
  // Simple fleet uptime estimate: proportion not-down over 30d window proxy.
  s.uptime30 = (s.total? (100.0f * (s.total - s.down) / s.total) : 100.0f);
  if(s.uptime30 > 99.99f) s.uptime30 = 99.97f;
  g_sum = s;
  unlock();
}

void Store::recomputeStatus(Host* h){
  // Paused/Ack are governance states held until explicitly cleared.
  if(h->status==Status::Paused || h->status==Status::Ack) return;
  bool anyDown=false, anyWarn=false, anyOn=false;
  for(uint8_t i=0;i<kCheckCount;i++){
    if(!h->checks[i].enabled) continue;
    anyOn=true;
    if(h->checks[i].state==CheckState::Down) anyDown=true;
    else if(h->checks[i].state==CheckState::Warn) anyWarn=true;
  }
  Status ns = anyDown? Status::Down : anyWarn? Status::Warn : Status::Up;
  if(!anyOn) ns = Status::Up;
  if(ns!=h->status){ h->status=ns; markDirty(); }
}

bool Store::ack(const char* id, const char* reason, const char* who){
  lock(); Host* h=byId(id); bool ok=false;
  if(h){ h->status=Status::Ack;
    strlcpy(h->ackBy, who&&*who?who:"me", sizeof(h->ackBy));
    strlcpy(h->ackReason, reason, sizeof(h->ackReason));
    strlcpy(h->ackAt, Settings::clockHHMM(), sizeof(h->ackAt));
    ok=true; markDirty(); }
  unlock(); recount(); return ok;
}
bool Store::pause(const char* id, const char* reason, const char* until, const char* who){
  lock(); Host* h=byId(id); bool ok=false;
  if(h){ h->prevStatus = (h->status==Status::Paused)? Status::Up : h->status;
    h->status=Status::Paused;
    strlcpy(h->pauseBy, who&&*who?who:"me", sizeof(h->pauseBy));
    strlcpy(h->pauseReason, reason, sizeof(h->pauseReason));
    strlcpy(h->pauseUntil, until&&*until?until:"Until resumed", sizeof(h->pauseUntil));
    for(uint8_t i=0;i<kCheckCount;i++) h->checks[i].enabled=false;
    ok=true; markDirty(); }
  unlock(); recount(); return ok;
}
bool Store::resume(const char* id){
  lock(); Host* h=byId(id); bool ok=false;
  if(h){ h->status = (h->prevStatus!=Status::Paused)? h->prevStatus : Status::Up;
    h->pauseReason[0]=0;
    for(uint8_t i=0;i<kCheckCount;i++)
      h->checks[i].enabled = (h->checks[i].state!=CheckState::Off);
    recomputeStatus(h); ok=true; markDirty(); }
  unlock(); recount(); return ok;
}
bool Store::clearAck(const char* id){
  lock(); Host* h=byId(id); bool ok=false;
  if(h){ h->status=Status::Up; h->ackReason[0]=0; recomputeStatus(h); ok=true; markDirty(); }
  unlock(); recount(); return ok;
}
bool Store::setEvery(const char* id, CheckKind k, uint32_t every){
  lock(); Host* h=byId(id); bool ok=false;
  if(h){ h->checks[(uint8_t)k].every = every; ok=true; markDirty(); }
  unlock(); return ok;
}

void Store::pushAlert(const AlertEvt& a){
  lock();
  for(int i=ALERT_LOG_MAX-1;i>0;i--) g_alerts[i]=g_alerts[i-1];
  g_alerts[0]=a; if(g_alertN<ALERT_LOG_MAX) g_alertN++;
  unlock(); markDirty();
}
int Store::alertCount(){ return g_alertN; }
AlertEvt Store::alertAt(int i){ return (i>=0&&i<g_alertN)? g_alerts[i] : AlertEvt(); }

bool Store::consumeDirty(){ bool d=g_dirty; g_dirty=false; return d; }
void Store::markDirty(){ g_dirty=true; }
