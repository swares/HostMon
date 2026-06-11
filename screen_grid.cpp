/* screen_grid.cpp — LCD home screen B · Grid (4-col host tiles). */
#include "ui.h"
#include "config.h"
#include "theme.h"
#include "store.h"
#include "settings.h"

static void tileEvent(lv_event_t* e){ (void)e; /* tap could open detail on a future screen */ }

// ---- in-place update support ----------------------------------------------
// buildGrid() records the widgets whose appearance tracks host state (the status
// dot + check chips per card, and the header count pills) plus a hash of the host
// *structure* (set/names/enabled-checks). UI::loop() then repaints just those in
// place via updateGrid() on a state change, and only does a full rebuild when the
// structure hash changes (host added/removed/renamed/checks toggled). Updating a
// handful of small objects avoids the full-content PSRAM flush that shows as green
// RGB-underrun lines on the left edge.
namespace {
  struct GridCard { lv_obj_t* dot; lv_obj_t* chip[kCheckCount]; };
  GridCard  g_cards[MAX_HOSTS];
  int       g_cardN = 0;
  lv_obj_t* g_pUp=nullptr; lv_obj_t* g_pWarn=nullptr; lv_obj_t* g_pDown=nullptr;
  uint32_t  g_sig = 0;

  uint32_t gridSig(){                       // FNV-1a over the structural fields only
    uint32_t h = 2166136261u;
    Store::lock();
    int n = Store::count();
    h = (h ^ (uint32_t)n) * 16777619u;
    for(int i=0;i<n;i++){ Host* ho=Store::at(i); if(!ho) continue;
      for(const char* p=ho->name; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
      for(const char* p=ho->addr; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
      uint32_t mask=0; for(uint8_t c=0;c<kCheckCount;c++) if(ho->checks[c].enabled) mask|=(1u<<c);
      h = (h ^ mask) * 16777619u;
    }
    Store::unlock();
    return h;
  }
}

bool UI::gridStructureChanged(){ return gridSig() != g_sig; }

void UI::updateGrid(){
  Store::recount();
  Summary S=Store::summary();
  char b[12];
  if(g_pUp)  { snprintf(b,sizeof(b),"%u up",S.up); lv_label_set_text(lv_obj_get_child(g_pUp,0),b); }
  if(g_pWarn){ snprintf(b,sizeof(b),"%u",S.warn);  lv_label_set_text(lv_obj_get_child(g_pWarn,0),b); }
  if(g_pDown){ snprintf(b,sizeof(b),"%u",S.down);  lv_label_set_text(lv_obj_get_child(g_pDown,0),b); }
  Store::lock();
  int n=Store::count(); if(n>g_cardN) n=g_cardN;
  for(int i=0;i<n;i++){ Host* h=Store::at(i); if(!h) continue;
    if(g_cards[i].dot) lv_obj_set_style_bg_color(g_cards[i].dot, Theme::statusColor(h->status), 0);
    for(uint8_t c=0;c<kCheckCount;c++) if(g_cards[i].chip[c])
      lv_obj_set_style_bg_color(g_cards[i].chip[c], Theme::checkColor(h->checks[c].state), 0);
  }
  Store::unlock();
}

void UI::buildGrid(lv_obj_t* parent){
  Store::recount();
  Summary S=Store::summary();

  // header
  lv_obj_t* head=lv_obj_create(parent); lv_obj_remove_style_all(head);
  lv_obj_set_size(head,LCD_W,52); lv_obj_align(head,LV_ALIGN_TOP_MID,0,0);
  lv_obj_set_flex_flow(head,LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(head,20,0); lv_obj_set_style_pad_column(head,8,0);
  lv_obj_set_style_border_color(head,Theme::line(),0); lv_obj_set_style_border_width(head,0,0);
  lv_obj_set_style_border_side(head,LV_BORDER_SIDE_BOTTOM,0);
  lv_obj_clear_flag(head,LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title=lv_label_create(head); lv_label_set_text(title,"All hosts");
  lv_obj_set_style_text_font(title,&lv_font_montserrat_18,0); lv_obj_set_style_text_color(title,Theme::tx(),0);
  char ub[12]; snprintf(ub,sizeof(ub),"%u up",S.up); g_pUp  =Theme::pill(head,ub,Theme::up());
  char wb[12]; snprintf(wb,sizeof(wb),"%u",S.warn);   g_pWarn=Theme::pill(head,wb,Theme::warn());
  char db[12]; snprintf(db,sizeof(db),"%u",S.down);   g_pDown=Theme::pill(head,db,Theme::down());
  lv_obj_t* clk=lv_label_create(head); lv_label_set_text(clk,Settings::clockHHMM());
  lv_obj_set_style_text_color(clk,Theme::mut(),0); lv_obj_set_style_text_font(clk,&lv_font_montserrat_16,0);
  UI::registerClock(clk);   // in-place minute updates (no full rebuild)
  lv_obj_set_flex_grow(clk,1); lv_obj_set_style_text_align(clk,LV_TEXT_ALIGN_RIGHT,0);

  // grid body (4 columns)
  lv_obj_t* grid=lv_obj_create(parent); lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid,LCD_W,LCD_H-60-52); lv_obj_align(grid,LV_ALIGN_TOP_MID,0,52);
  lv_obj_set_style_pad_all(grid,14,0); lv_obj_set_style_pad_gap(grid,8,0);
  lv_obj_set_style_pad_right(grid,18,0);         // leave a gutter for the scrollbar
  lv_obj_set_flex_flow(grid,LV_FLEX_FLOW_ROW_WRAP);
  // Vertical scroll for the full host list. remove_style_all() above stripped the
  // default scrollbar style, so style it explicitly here — otherwise the container
  // scrollbar is zero-width/invisible and the only scrollbars you see are the
  // tiles' own (which is what made it look "attached to the cards").
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_width(grid, 6, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(grid, Theme::mut(), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(grid, LV_OPA_70, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(grid, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(grid, 5, LV_PART_SCROLLBAR);

  Store::lock();
  int n=Store::count();
  g_cardN = n;                        // for the in-place updater
  int tileW=(LCD_W-14-18-8*2)/3;     // 3 columns (wider — fits longer hostnames)
  for(int i=0;i<n;i++){
    Host* h=Store::at(i);
    for(uint8_t c=0;c<kCheckCount;c++) g_cards[i].chip[c]=nullptr;   // reset card slot
    lv_obj_t* t=lv_obj_create(grid); lv_obj_set_size(t,tileW,LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(t,Theme::panel(),0); lv_obj_set_style_border_color(t,Theme::line(),0);
    lv_obj_set_style_border_width(t,1,0); lv_obj_set_style_radius(t,11,0); lv_obj_set_style_pad_all(t,8,0);
    lv_obj_set_flex_flow(t,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(t,4,0);
    lv_obj_clear_flag(t,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(t,LV_SCROLLBAR_MODE_OFF);   // never a per-card scrollbar
    lv_obj_add_event_cb(t,tileEvent,LV_EVENT_CLICKED,(void*)h->id);

    // row 1: status dot + name on its own full-width line. The name uses the flex
    // idiom width=1 + flex_grow(1): that makes its flex basis tiny so it GROWS to
    // the row width and LV_LABEL_LONG_DOT can truncate with "…". Without the
    // width=1, the basis is the (long) content width and the label wraps instead.
    lv_obj_t* r1=lv_obj_create(t); lv_obj_remove_style_all(r1);
    lv_obj_set_size(r1,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r1,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r1,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r1,6,0); lv_obj_clear_flag(r1,LV_OBJ_FLAG_SCROLLABLE);
    g_cards[i].dot = Theme::dot(r1, Theme::statusColor(h->status), 9);
    lv_obj_t* nm=lv_label_create(r1); lv_label_set_text(nm,h->name);
    lv_label_set_long_mode(nm,LV_LABEL_LONG_DOT); lv_obj_set_width(nm,1); lv_obj_set_flex_grow(nm,1);
    lv_obj_set_style_text_font(nm,&lv_font_montserrat_14,0); lv_obj_set_style_text_color(nm,Theme::tx(),0);

    // row 2: address (grows + truncates) on the left, check chips on the right.
    lv_obj_t* r2=lv_obj_create(t); lv_obj_remove_style_all(r2);
    lv_obj_set_size(r2,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r2,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r2,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r2,6,0); lv_obj_clear_flag(r2,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* ip=lv_label_create(r2); lv_label_set_text(ip,h->addr);
    lv_label_set_long_mode(ip,LV_LABEL_LONG_DOT); lv_obj_set_width(ip,1); lv_obj_set_flex_grow(ip,1);
    lv_obj_set_style_text_color(ip,Theme::mut(),0); lv_obj_set_style_text_font(ip,&lv_font_montserrat_10,0);
    lv_obj_t* chips=lv_obj_create(r2); lv_obj_remove_style_all(chips);
    lv_obj_set_size(chips,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips,LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(chips,2,0); lv_obj_clear_flag(chips,LV_OBJ_FLAG_SCROLLABLE);
    for(uint8_t c=0;c<kCheckCount;c++){
      if(!h->checks[c].enabled) continue;
      g_cards[i].chip[c] = Theme::chip(chips, checkMeta((CheckKind)c).abbr, Theme::checkColor(h->checks[c].state), true);
    }
  }
  Store::unlock();
  g_sig = gridSig();                  // baseline structure hash for UI::loop's dispatch
}
