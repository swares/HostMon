/* ui.cpp — root layout, bottom navigation and refresh loop for the LCD. */
#include "ui.h"
#include "config.h"
#include "theme.h"
#include "store.h"
#include "settings.h"
#include "display.h"
#include "wifi_portal.h"
#include "webserver.h"
#include "scheduler.h"
#include "checks.h"        // Checks::tlsSelfTestResult() for the on-screen TLS probe
#include "csv.h"           // Storage::diag() for the on-screen FS readout
#include <esp_heap_caps.h> // live internal-heap readout (TLS-viability gauge)

// Defined in HostMonitor.ino (RTC_NOINIT). Declared at file scope so it resolves
// to the global symbol, not one inside this translation unit's anon namespace.
extern volatile int g_bootStage;

namespace {
  lv_obj_t* g_content=nullptr;     // screen content area (above the nav)
  lv_obj_t* g_nav=nullptr;
  lv_obj_t* g_clockLbl=nullptr;    // current screen's clock label (for in-place time updates)
  lv_obj_t* g_heapLbl=nullptr;     // Setup card's live heap readout (updated each refresh tick)
  lv_obj_t* g_tlsLbl=nullptr;      // Setup card's one-shot TLS self-test result line
  char      g_active='A';
  uint32_t  g_lastRefresh=0;
  uint32_t  g_lastRebuild=0;       // throttles full screen rebuilds (RGB-underrun fix)
  char      g_lastClock[8]={0};

  // Live internal-RAM readout. The number that decides whether on-device TLS is even
  // worth attempting is `largest` — mbedtls_ssl_setup() needs ~16 KB *contiguous* twice
  // (IN+OUT buffers), so a largest free block below ~16 KB means TLS can't even start,
  // and ~32 KB+ is needed for comfortable headroom. `free` is total internal heap;
  // `min` is the low-water mark since boot (worst case seen under load).
  void heapLine(char* buf, size_t n){
    unsigned freeI = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    unsigned lblk  = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    unsigned minI  = (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    snprintf(buf, n, "RAM free %uk · largest %uk · min %uk  (TLS needs ~16-32k largest)",
             freeI/1024, lblk/1024, minI/1024);
  }

  void navEvent(lv_event_t* e){
    intptr_t which=(intptr_t)lv_event_get_user_data(e);
    if(which==0) UI::show('A');
    else if(which==1) UI::show('B');
    else UI::show('D');                // combined Alerts / Setup tab
  }

  // Alerts/Setup aren't built as on-device screens — they're managed in the web
  // dashboard. Show a card pointing the user there instead of a dead button.
  void buildPlaceholder(lv_obj_t* parent, const char* title){
    lv_obj_t* card=lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LCD_W-160, LCD_H-180);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, Theme::panel(),0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER,0);
    lv_obj_set_style_border_width(card,1,0);
    lv_obj_set_style_border_color(card, Theme::line(),0);
    lv_obj_set_style_radius(card,16,0);
    lv_obj_set_style_pad_all(card,24,0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t=lv_label_create(card);
    lv_label_set_text(t,title);
    lv_obj_set_style_text_color(t, Theme::tx(),0);
    lv_obj_set_style_text_font(t,&lv_font_montserrat_24,0);

    lv_obj_t* s=lv_label_create(card);
    lv_label_set_text(s,"Managed in the web dashboard");
    lv_obj_set_style_text_color(s, Theme::mut(),0);
    lv_obj_set_style_text_font(s,&lv_font_montserrat_16,0);
    lv_obj_set_style_pad_top(s,10,0);

    // HTTP is the always-reachable URL (HTTPS is best-effort on this board).
    char buf[72];
    lv_obj_t* u=lv_label_create(card);
    snprintf(buf,sizeof(buf),"http://%s.local", MDNS_HOSTNAME);
    lv_label_set_text(u,buf);
    lv_obj_set_style_text_color(u, Theme::teal(),0);
    lv_obj_set_style_text_font(u,&lv_font_montserrat_20,0);
    lv_obj_set_style_pad_top(u,18,0);

    lv_obj_t* ln=lv_label_create(card);
    if(WifiPortal::isAP())
      snprintf(buf,sizeof(buf),"Join Wi-Fi '%s', then http://%s", AP_SSID, WifiPortal::ipString());
    else if(WifiPortal::isOnline())
      snprintf(buf,sizeof(buf),"or  http://%s", WifiPortal::ipString());
    else
      snprintf(buf,sizeof(buf),"Wi-Fi offline");
    lv_label_set_text(ln,buf);
    lv_obj_set_style_text_color(ln, Theme::faint(),0);
    lv_obj_set_style_text_font(ln,&lv_font_montserrat_14,0);
    lv_obj_set_style_pad_top(ln,8,0);

    // First-boot auto-generated web login: show it here so the user can read it off
    // the screen. Disappears once they set their own password in the dashboard.
    if(Settings::auth().autoGen){
      lv_obj_t* cr=lv_label_create(card);
      char cbuf[72]; snprintf(cbuf,sizeof(cbuf),"Web login:  %s  /  %s",
                              Settings::auth().user, Settings::auth().pass);
      lv_label_set_text(cr,cbuf);
      lv_obj_set_style_text_color(cr, Theme::teal(),0);
      lv_obj_set_style_text_font(cr,&lv_font_montserrat_16,0);
      lv_obj_set_style_pad_top(cr,8,0);
    }

    // WiFi bring-up diagnostics (serial is unavailable at runtime on this board).
    lv_obj_t* dg=lv_label_create(card);
    lv_label_set_text(dg, WifiPortal::diag());
    lv_obj_set_style_text_color(dg, Theme::faint(),0);
    lv_obj_set_style_text_font(dg,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(dg,4,0);

    // Init-task stage (survives resets): tells us which step faults in a loop.
    static const char* kStage[]={"boot","sd","wifi","web/TLS","notify","sched","?","?","?","ready"};
    int st=g_bootStage; const char* nm=(st>=0&&st<=9)?kStage[st]:"?";
    char sbuf[44]; snprintf(sbuf,sizeof(sbuf),"init stage %d: %s", st, nm);
    lv_obj_t* sg=lv_label_create(card);
    lv_label_set_text(sg, sbuf);
    lv_obj_set_style_text_color(sg, Theme::faint(),0);
    lv_obj_set_style_text_font(sg,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(sg,4,0);

    // Web server start results (https/http listen, FS mount, cert source).
    lv_obj_t* wb=lv_label_create(card);
    lv_label_set_text(wb, WebServer::status());
    lv_obj_set_style_text_color(wb, Theme::faint(),0);
    lv_obj_set_style_text_font(wb,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(wb,4,0);

    // Scheduler/check-engine diagnostics.
    lv_obj_t* sd=lv_label_create(card);
    lv_label_set_text(sd, Scheduler::status());
    lv_obj_set_style_text_color(sd, Theme::faint(),0);
    lv_obj_set_style_text_font(sd,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(sd,4,0);

    // Check-engine load: queue depth (overdue checks), cumulative timeouts, last
    // scan duration — to spot the checks overloading the system.
    lv_obj_t* pf=lv_label_create(card);
    lv_label_set_text(pf, Scheduler::perf());
    lv_obj_set_style_text_color(pf, Theme::faint(),0);
    lv_obj_set_style_text_font(pf,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(pf,4,0);

    // Storage diagnostics + build-time marker. The build time confirms whether the
    // flashed binary is actually the latest one (stale-build check); the fs line
    // shows whether persistent storage mounted cleanly, the saved file size, how
    // many hosts loaded, and the last save result.
    lv_obj_t* fg=lv_label_create(card);
    char fbuf[104]; snprintf(fbuf,sizeof(fbuf),"%s | built %s", Storage::diag(), __TIME__);
    lv_label_set_text(fg, fbuf);
    lv_obj_set_style_text_color(fg, Theme::teal(),0);
    lv_obj_set_style_text_font(fg,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(fg,4,0);

    // Live internal-RAM gauge — updated in place every refresh tick (see UI::loop).
    // `largest` is the figure that decides whether on-device TLS is worth retrying.
    char hbuf[88]; heapLine(hbuf,sizeof(hbuf));
    lv_obj_t* hg=lv_label_create(card);
    lv_label_set_text(hg, hbuf);
    lv_obj_set_style_text_color(hg, Theme::tx(),0);
    lv_obj_set_style_text_font(hg,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(hg,4,0);
    g_heapLbl=hg;                    // register for in-place live updates

    // One-shot TLS self-test result (insecure handshake to TLS_SELFTEST_HOST).
    // "pending…" until the probe runs ~3 s after boot; then OK/FAIL + largest-block.
    lv_obj_t* tg=lv_label_create(card);
    lv_label_set_text(tg, Checks::tlsSelfTestResult());
    lv_obj_set_style_text_color(tg, Theme::teal(),0);
    lv_obj_set_style_text_font(tg,&lv_font_montserrat_12,0);
    lv_obj_set_style_pad_top(tg,4,0);
    g_tlsLbl=tg;                     // refreshed in place until the probe completes
  }

  void buildNav(lv_obj_t* parent){
    g_nav=lv_obj_create(parent);
    lv_obj_remove_style_all(g_nav);
    lv_obj_set_size(g_nav, LCD_W, 60);
    lv_obj_align(g_nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(g_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(g_nav,18,0);
    lv_obj_set_style_pad_bottom(g_nav,12,0);
    lv_obj_set_style_pad_column(g_nav,7,0);
    lv_obj_clear_flag(g_nav, LV_OBJ_FLAG_SCROLLABLE);

    const char* names[]={"Home","Hosts","Alerts / Setup"};
    for(int i=0;i<3;i++){
      lv_obj_t* b=lv_btn_create(g_nav);
      lv_obj_set_flex_grow(b,1); lv_obj_set_height(b,38);
      bool on = (i==0 && g_active=='A') || (i==1 && g_active=='B') || (i==2 && g_active=='D');
      lv_obj_set_style_bg_color(b, on?Theme::teal():Theme::panel(),0);
      lv_obj_set_style_border_width(b, on?0:1,0);
      lv_obj_set_style_border_color(b, Theme::line(),0);
      lv_obj_set_style_radius(b,12,0); lv_obj_set_style_shadow_width(b,0,0);
      lv_obj_t* l=lv_label_create(b); lv_label_set_text(l,names[i]);
      lv_obj_set_style_text_color(l, on?lv_color_hex(0x04231b):Theme::mut(),0);
      lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0); lv_obj_center(l);
      lv_obj_add_event_cb(b, navEvent, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
  }

  void rebuild(){
    if(!g_content) return;
    lv_obj_clean(g_content);
    g_clockLbl=nullptr;              // old label is gone; the screen re-registers its own
    g_heapLbl=nullptr;              // ditto: only the Setup card recreates + registers it
    g_tlsLbl=nullptr;
    if(g_active=='A')      UI::buildHealth(g_content);
    else if(g_active=='B') UI::buildGrid(g_content);
    else                   buildPlaceholder(g_content, "Alerts / Setup");
    buildNav(lv_obj_get_parent(g_content));  // nav reflects active tab
  }
}

void UI::begin(){
  Display::lock();
  Theme::init();
  lv_obj_t* scr=lv_scr_act();
  lv_obj_set_style_bg_color(scr, Theme::bg2(),0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER,0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  g_content=lv_obj_create(scr);
  lv_obj_remove_style_all(g_content);
  lv_obj_set_size(g_content, LCD_W, LCD_H-60);
  lv_obj_align(g_content, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);

  g_active = Settings::defaults().lcdHome=='B' ? 'B' : 'A';
  rebuild();
  Display::unlock();
}

void UI::show(char screen){
  if(screen!=g_active){ g_active=screen; }
  Display::lock(); rebuild(); Display::unlock();
}

void UI::registerClock(lv_obj_t* lbl){ g_clockLbl=lbl; }

void UI::loop(){
  uint32_t now=millis();
  if(now-g_lastRefresh < LCD_REFRESH_MS) return;
  g_lastRefresh=now;

  // A full rebuild (tear down + recreate every widget) is the expensive event — its
  // PSRAM draw-buffer -> PSRAM framebuffer copy momentarily starves the RGB DMA and
  // shows as green underrun lines on the left edge. So:
  //   • On the Hosts grid, when only host/check STATES changed (the host set is the
  //     same), repaint just the affected dots/chips/count-pills IN PLACE — small flush,
  //     no artifact, and it can run every tick.
  //   • A structural change (host added/removed/renamed/checks toggled), or the
  //     Home/Setup tab, still needs a full rebuild — coalesced to LCD_REBUILD_MS.
  if(Store::consumeDirty()){
    if(g_active=='B' && !UI::gridStructureChanged()){
      Display::lock(); UI::updateGrid(); Display::unlock();
    } else if(now - g_lastRebuild >= LCD_REBUILD_MS){
      g_lastRebuild = now;
      strlcpy(g_lastClock, Settings::clockHHMM(), sizeof(g_lastClock));
      Display::lock(); rebuild(); Display::unlock();
      return;                         // fresh widgets — skip the in-place updates this tick
    } else {
      Store::markDirty();             // rebuild needed but throttled — re-arm for next window
    }
  }

  // Cheap in-place updates every tick (NO rebuild): clock + the Setup-card gauges.
  // These repaint only their own small label area, so they don't trigger the artifact.
  const char* clk=Settings::clockHHMM();
  if(strcmp(clk,g_lastClock)!=0){
    strlcpy(g_lastClock,clk,sizeof(g_lastClock));
    Display::lock(); if(g_clockLbl) lv_label_set_text(g_clockLbl, clk); Display::unlock();
  }
  if(g_heapLbl){                      // null on Home/Hosts tabs — no-op there
    char hbuf[88]; heapLine(hbuf,sizeof(hbuf));
    Display::lock(); lv_label_set_text(g_heapLbl, hbuf); Display::unlock();
  }
  if(g_tlsLbl){                       // pick up the one-shot TLS self-test result
    Display::lock(); lv_label_set_text(g_tlsLbl, Checks::tlsSelfTestResult()); Display::unlock();
  }
}

void UI::ackHost(const char* id){ Store::ack(id,"Acknowledged on device","device"); }
void UI::pauseHost(const char* id){ Store::pause(id,"Paused on device","Until resumed","device"); }
