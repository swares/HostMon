/* scheduler.cpp — periodic check execution + status/alert transitions. */
#include "scheduler.h"
#include "config.h"
#include "store.h"
#include "settings.h"
#include "checks.h"
#include "notifier.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
  // Live scheduler diagnostics shown on the LCD Setup card (serial is gone once
  // the panel is up): scan = runOnce() iterations, wifi = WiFi.status(),
  // ran = checks actually executed.
  volatile uint32_t g_scans=0, g_ran=0;
  volatile int      g_wifi=-1, g_gated=0;
  char              g_diag[64]="sched: not started";
  // Load metrics for overload debugging (shown on the Alerts/Setup card):
  //   scanMs   = wall-clock time of the last full scan pass
  //   timeouts = cumulative checks that ran the full timeout (slow/unreachable)
  //   backlog  = enabled checks currently overdue (couldn't keep up = overloaded)
  volatile uint32_t g_scanMs=0, g_timeouts=0, g_backlog=0;
  static const uint32_t CHECK_SLOW_MS = 3000;   // >= this counts as a timeout

  // Per-host alert bookkeeping (parallel to store order by id hash isn't safe,
  // so we track by id string in a small table).
  struct AlertState { char id[12]; bool firing; uint32_t lastNotify; Status last; };
  AlertState g_as[MAX_HOSTS]; int g_asN=0;

  AlertState* asFor(const char* id){
    for(int i=0;i<g_asN;i++) if(!strcmp(g_as[i].id,id)) return &g_as[i];
    if(g_asN<MAX_HOSTS){ AlertState* a=&g_as[g_asN++]; strlcpy(a->id,id,sizeof(a->id));
      a->firing=false; a->lastNotify=0; a->last=Status::Up; return a; }
    return nullptr;
  }

  void pushHist(Check& c, uint32_t rtt){
    if(rtt>9999) rtt=9999;
    if(c.histLen<24) c.hist[c.histLen++]=(uint16_t)rtt;
    else { for(int i=0;i<23;i++) c.hist[i]=c.hist[i+1]; c.hist[23]=(uint16_t)rtt; }
  }

  // Updates host status/msg under the caller's lock and decides whether an
  // alert should be sent. Returns the event ("" = none) WITHOUT doing any
  // network I/O — the caller sends it AFTER releasing the store lock so a slow
  // SMTP/webhook send never blocks the UI or web server.
  bool evaluateHost(Host* h, char* ev, size_t en){
    ev[0]=0;
    Status before = h->status;
    Store::recomputeStatus(h);

    h->msg[0]=0;
    for(uint8_t i=0;i<kCheckCount;i++)
      if(h->checks[i].enabled && h->checks[i].state==CheckState::Down){
        snprintf(h->msg,sizeof(h->msg),"%s: %s",checkMeta((CheckKind)i).name,h->checks[i].detail); break; }
    if(!h->msg[0]) for(uint8_t i=0;i<kCheckCount;i++)
      if(h->checks[i].enabled && h->checks[i].state==CheckState::Warn){
        snprintf(h->msg,sizeof(h->msg),"%s: %s",checkMeta((CheckKind)i).name,h->checks[i].detail); break; }

    AlertState* a=asFor(h->id); if(!a) return false;
    uint8_t failsNeeded = Settings::defaults().failsBeforeAlert;

    if(h->status==Status::Down){
      if(before!=Status::Down){ h->fails=1; h->sinceMs=millis(); }
      else h->fails++;
      bool reached = h->fails>=failsNeeded;
      bool reNotifyDue = a->firing && Settings::defaults().renotify &&
        (millis()-a->lastNotify >= Settings::defaults().renotifyEvery*1000UL);
      if((reached && !a->firing) || reNotifyDue){
        if(h->alertDown){ strlcpy(ev,"host.down",en); a->lastNotify=millis(); }
        a->firing=true;
      }
    } else if(h->status==Status::Warn){
      if(h->alertWarn && before!=Status::Warn) strlcpy(ev,"host.warn",en);
    } else if(h->status==Status::Up){
      if(a->firing){ if(h->alertRecovered) strlcpy(ev,"host.up",en); a->firing=false; h->fails=0; }
    }
    a->last=h->status;
    return ev[0]!=0;
  }
}

void Scheduler::runOnce(){
  g_scans++; g_wifi=WiFi.status();
  uint32_t scanStart = millis();
  // No connectivity gate. On this build WiFi.status()/localIP() are unreliable
  // (they report STOPPED/0 while actually connected and serving), so we just run
  // the checks unconditionally — each has its own timeout, and with no network a
  // host simply shows "down". (g_wifi is kept only for the on-screen readout.)
  uint32_t nowS = millis()/1000;

  for(int i=0;i<Store::count();i++){
    Host* h; Store::lock(); h=Store::at(i); Store::unlock();
    if(!h) continue;
    if(h->status==Status::Paused) continue;          // checks disabled

    bool ranThisHost=false;   // did any check actually fire this scan?
    for(uint8_t c=0;c<kCheckCount;c++){
      Check& chk=h->checks[c];
      if(!chk.enabled) continue;
      uint32_t every = chk.every? chk.every : defaultInterval((CheckKind)c);
      if(chk.lastRun!=0 && (nowS-chk.lastRun) < every) continue;

      g_ran++; ranThisHost=true;
      uint32_t cs = millis();
      CheckResult res = Checks::run(*h, (CheckKind)c);   // network-blocking
      if(millis()-cs >= CHECK_SLOW_MS) g_timeouts++;     // ran the full timeout

      Store::lock();
      chk.lastRun=nowS;
      chk.state=res.state;
      strlcpy(chk.detail,res.detail,sizeof(chk.detail));
      if(res.rttMs){ chk.lastRttMs=res.rttMs; pushHist(chk,res.rttMs); }
      if((CheckKind)c==CheckKind::Ping && res.rttMs) h->rtt=res.rttMs;
      h->last=0;
      Store::unlock();
    }

    char ev[12]; char msg[80];
    Store::lock(); bool fire=evaluateHost(h,ev,sizeof(ev)); strlcpy(msg,h->msg,sizeof(msg)); Store::unlock();
    // Only repaint the LCD when a check actually ran (i.e. data changed). Marking
    // dirty every scan forced a full-screen teardown/rebuild every second, which
    // saturated the PSRAM bus and produced the green RGB-underrun artifacts.
    if(ranThisHost) Store::markDirty();
    if(fire) Notifier::notify(*h, ev, msg[0]?msg:"Recovered");   // network I/O, lock released
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  Store::recount();
  // Backlog = enabled checks that are now overdue (interval elapsed but they
  // haven't been re-run yet). A persistently rising backlog means the sequential
  // check engine can't keep up with the configured intervals (overloaded).
  uint32_t bk=0;
  Store::lock();
  for(int i=0;i<Store::count();i++){ Host* hh=Store::at(i);
    if(!hh || hh->status==Status::Paused) continue;
    for(uint8_t c=0;c<kCheckCount;c++){ Check& ch=hh->checks[c];
      if(!ch.enabled) continue;
      uint32_t every = ch.every? ch.every : defaultInterval((CheckKind)c);
      if(ch.lastRun!=0 && (nowS-ch.lastRun) >= every) bk++; } }
  Store::unlock();
  g_backlog = bk;
  g_scanMs  = millis() - scanStart;
  snprintf(g_diag,sizeof(g_diag),"sc=%u wifi=%d h=%d ran=%u",
           (unsigned)g_scans,g_wifi,Store::count(),(unsigned)g_ran);
}

const char* Scheduler::status(){ return g_diag; }

const char* Scheduler::perf(){
  static char buf[52];
  snprintf(buf,sizeof(buf),"queue=%u timeouts=%u scan=%ums",
           (unsigned)g_backlog,(unsigned)g_timeouts,(unsigned)g_scanMs);
  return buf;
}

static void checkTask(void*){
  for(;;){
    Scheduler::runOnce();
    // tick "seconds since last check" between scans
    for(int i=0;i<Store::count();i++){ Store::lock(); Host* h=Store::at(i);
      if(h && h->last>=0) h->last++; Store::unlock(); }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Static stack + control block for the check task. Reserving the stack in BSS
// instead of the dynamic heap means task creation CANNOT fail because internal
// heap got fragmented during boot — that was the recurring "sched create-fail"
// (e.g. ih=22k lb=12k: enough free RAM total, but no 16k contiguous block) that
// left every check stuck on "pending…" after a restart. It costs the same 16 KB
// of internal RAM either way; this just makes it deterministic.
static StackType_t  s_checkStack[CHECK_TASK_STACK/sizeof(StackType_t)];
static StaticTask_t s_checkTcb;

void Scheduler::begin(){
  uint32_t ih=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t lb=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  TaskHandle_t th=xTaskCreateStaticPinnedToCore(checkTask,"checks",
      CHECK_TASK_STACK/sizeof(StackType_t),nullptr,1,s_checkStack,&s_checkTcb,CHECK_TASK_CORE);
  if(!th)
    snprintf(g_diag,sizeof(g_diag),"sched STATIC-FAIL ih=%uk lb=%uk",(unsigned)(ih/1024),(unsigned)(lb/1024));
  else
    snprintf(g_diag,sizeof(g_diag),"sched up(static) ih=%uk lb=%uk",(unsigned)(ih/1024),(unsigned)(lb/1024));
}
