/* screen_health.cpp — LCD home screen A · Health (donut + needs-attention). */
#include "ui.h"
#include "config.h"
#include "theme.h"
#include "store.h"
#include "settings.h"

static void addCheckChips(lv_obj_t* parent, Host* h){
  lv_obj_t* row=lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row,4,0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  for(uint8_t i=0;i<kCheckCount;i++){
    bool filled = h->checks[i].state==CheckState::Up||h->checks[i].state==CheckState::Warn||h->checks[i].state==CheckState::Down;
    Theme::chip(row, checkMeta((CheckKind)i).abbr, Theme::checkColor(h->checks[i].state), filled);
  }
}

static void ackCb(lv_event_t* e){ UI::ackHost((const char*)lv_event_get_user_data(e)); }
static void pauseCb(lv_event_t* e){ UI::pauseHost((const char*)lv_event_get_user_data(e)); }

void UI::buildHealth(lv_obj_t* parent){
  Store::recount();
  Summary S=Store::summary();

  // ---- header ----
  lv_obj_t* head=lv_obj_create(parent);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LCD_W, 56);
  lv_obj_align(head, LV_ALIGN_TOP_MID,0,0);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(head,20,0); lv_obj_set_style_pad_column(head,12,0);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* mark=lv_obj_create(head); lv_obj_set_size(mark,32,32);
  lv_obj_set_style_radius(mark,10,0); lv_obj_set_style_bg_color(mark,Theme::teal(),0);
  lv_obj_set_style_border_width(mark,0,0); lv_obj_clear_flag(mark,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* mk=lv_label_create(mark); lv_label_set_text(mk,LV_SYMBOL_GPS); lv_obj_center(mk);
  lv_obj_set_style_text_color(mk,lv_color_hex(0x04231b),0);

  lv_obj_t* tcol=lv_obj_create(head); lv_obj_remove_style_all(tcol);
  lv_obj_set_flex_flow(tcol,LV_FLEX_FLOW_COLUMN); lv_obj_set_size(tcol,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
  lv_obj_t* t1=lv_label_create(tcol); lv_label_set_text(t1,"Host Monitor");
  lv_obj_set_style_text_font(t1,&lv_font_montserrat_18,0); lv_obj_set_style_text_color(t1,Theme::tx(),0);
  lv_obj_t* t2=lv_label_create(tcol); char sub[40];
  snprintf(sub,sizeof(sub),"%u hosts - home-lab",S.total);
  lv_label_set_text(t2,sub); lv_obj_set_style_text_color(t2,Theme::mut(),0);
  lv_obj_set_style_text_font(t2,&lv_font_montserrat_12,0);

  lv_obj_t* clk=Theme::card(head); lv_obj_set_style_pad_all(clk,8,0);
  lv_obj_set_flex_flow(clk,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(clk,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_END,LV_FLEX_ALIGN_END);
  lv_obj_add_flag(clk, LV_OBJ_FLAG_FLOATING); lv_obj_align(clk, LV_ALIGN_RIGHT_MID, -16, 0);
  lv_obj_t* ct=lv_label_create(clk); lv_label_set_text(ct,Settings::clockHHMM());
  lv_obj_set_style_text_font(ct,&lv_font_montserrat_16,0); lv_obj_set_style_text_color(ct,Theme::tx(),0);
  UI::registerClock(ct);   // allow in-place minute updates without a full rebuild
  lv_obj_t* cd=lv_label_create(clk); lv_label_set_text(cd,Settings::dateLongUpper());
  lv_obj_set_style_text_font(cd,&lv_font_montserrat_10,0); lv_obj_set_style_text_color(cd,Theme::mut(),0);

  // ---- body: left donut/stat column + right attention list ----
  lv_obj_t* body=lv_obj_create(parent); lv_obj_remove_style_all(body);
  lv_obj_set_size(body, LCD_W, LCD_H-60-56);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_hor(body,18,0); lv_obj_set_style_pad_column(body,14,0);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  // left column
  lv_obj_t* left=lv_obj_create(body); lv_obj_remove_style_all(left);
  lv_obj_set_size(left,280,LV_PCT(100)); lv_obj_set_flex_flow(left,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(left,12,0); lv_obj_clear_flag(left,LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* donutCard=Theme::card(left); lv_obj_set_width(donutCard,LV_PCT(100));
  lv_obj_set_flex_flow(donutCard,LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(donutCard,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(donutCard,16,0);

  lv_obj_t* arc=lv_arc_create(donutCard); lv_obj_set_size(arc,116,116);
  lv_arc_set_rotation(arc,135); lv_arc_set_bg_angles(arc,0,270);
  int healthy = S.total? (int)(100.0*S.up/S.total) : 0;
  lv_arc_set_value(arc, healthy);
  lv_obj_remove_style(arc,NULL,LV_PART_KNOB);
  lv_obj_clear_flag(arc,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc,Theme::line(),LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc,Theme::up(),LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc,12,LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc,12,LV_PART_INDICATOR);
  lv_obj_t* cen=lv_label_create(arc); char ct2[16];
  snprintf(ct2,sizeof(ct2),"%u/%u",S.up,S.total); lv_label_set_text(cen,ct2);
  lv_obj_set_style_text_font(cen,&lv_font_montserrat_24,0); lv_obj_center(cen);

  lv_obj_t* legend=lv_obj_create(donutCard); lv_obj_remove_style_all(legend);
  lv_obj_set_flex_grow(legend,1); lv_obj_set_flex_flow(legend,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(legend,7,0); lv_obj_clear_flag(legend,LV_OBJ_FLAG_SCROLLABLE);
  struct LG{const char* n;lv_color_t c;uint16_t v;} lgs[]={
    {"Up",Theme::up(),S.up},{"Warning",Theme::warn(),S.warn},{"Down",Theme::down(),S.down},
    {"Paused",Theme::paused(),S.paused},{"Ack'd",Theme::ack(),S.ack}};
  for(auto& g:lgs){
    lv_obj_t* r=lv_obj_create(legend); lv_obj_remove_style_all(r);
    lv_obj_set_width(r,LV_PCT(100)); lv_obj_set_flex_flow(r,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r,8,0); lv_obj_clear_flag(r,LV_OBJ_FLAG_SCROLLABLE);
    Theme::dot(r,g.c,8);
    lv_obj_t* nm=lv_label_create(r); lv_label_set_text(nm,g.n);
    lv_obj_set_style_text_color(nm,Theme::mut(),0); lv_obj_set_style_text_font(nm,&lv_font_montserrat_12,0);
    lv_obj_t* vv=lv_label_create(r); char vb[8]; snprintf(vb,sizeof(vb),"%u",g.v);
    lv_label_set_text(vv,vb); lv_obj_set_style_text_color(vv,Theme::tx(),0);
    lv_obj_set_flex_grow(vv,1); lv_obj_set_style_text_align(vv,LV_TEXT_ALIGN_RIGHT,0);
  }

  // stat tiles
  lv_obj_t* tiles=lv_obj_create(left); lv_obj_remove_style_all(tiles);
  lv_obj_set_width(tiles,LV_PCT(100)); lv_obj_set_flex_flow(tiles,LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(tiles,9,0); lv_obj_clear_flag(tiles,LV_OBJ_FLAG_SCROLLABLE);
  struct ST{const char* k;char v[12];lv_color_t c;} sts[2];
  snprintf(sts[0].v,sizeof(sts[0].v),"%.2f%%",S.uptime30); sts[0].k="30D UPTIME"; sts[0].c=Theme::tx();
  snprintf(sts[1].v,sizeof(sts[1].v),"%u",S.attention); sts[1].k="NEED YOU"; sts[1].c=Theme::down();
  for(auto& s:sts){
    lv_obj_t* t=lv_obj_create(tiles); lv_obj_set_flex_grow(t,1); lv_obj_set_height(t,LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(t,Theme::raise(),0); lv_obj_set_style_border_width(t,0,0);
    lv_obj_set_style_radius(t,11,0); lv_obj_set_style_pad_all(t,9,0);
    lv_obj_set_flex_flow(t,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(t,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* v=lv_label_create(t); lv_label_set_text(v,s.v);
    lv_obj_set_style_text_font(v,&lv_font_montserrat_20,0); lv_obj_set_style_text_color(v,s.c,0);
    lv_obj_t* k=lv_label_create(t); lv_label_set_text(k,s.k);
    lv_obj_set_style_text_font(k,&lv_font_montserrat_10,0); lv_obj_set_style_text_color(k,Theme::mut(),0);
  }

  // right: needs attention
  lv_obj_t* right=Theme::card(body); lv_obj_set_flex_grow(right,1); lv_obj_set_height(right,LV_PCT(100));
  lv_obj_set_flex_flow(right,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(right,9,0);
  lv_obj_add_flag(right, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hr=lv_obj_create(right); lv_obj_remove_style_all(hr);
  lv_obj_set_width(hr,LV_PCT(100)); lv_obj_set_flex_flow(hr,LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hr,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(hr,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* hl=lv_label_create(hr); lv_label_set_text(hl,"NEEDS ATTENTION");
  lv_obj_set_style_text_color(hl,Theme::faint(),0); lv_obj_set_style_text_font(hl,&lv_font_montserrat_10,0);
  char ab[16]; snprintf(ab,sizeof(ab),"%u active",S.attention);
  Theme::pill(hr, ab, Theme::down());

  Store::lock();
  int shown=0;
  for(int i=0;i<Store::count() && shown<3;i++){
    Host* h=Store::at(i);
    if(h->status!=Status::Down && h->status!=Status::Warn) continue;
    shown++;
    lv_obj_t* sc=lv_obj_create(right); lv_obj_set_width(sc,LV_PCT(100)); lv_obj_set_height(sc,LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(sc,Theme::raise(),0); lv_obj_set_style_border_color(sc,Theme::line(),0);
    lv_obj_set_style_border_width(sc,1,0); lv_obj_set_style_radius(sc,12,0); lv_obj_set_style_pad_all(sc,9,0);
    lv_obj_set_flex_flow(sc,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(sc,5,0);
    lv_obj_clear_flag(sc,LV_OBJ_FLAG_SCROLLABLE);

    // Explicit content height on the rows — without it the remove_style_all()
    // containers keep a large default height and the card fills with whitespace.
    lv_obj_t* r1=lv_obj_create(sc); lv_obj_remove_style_all(r1); lv_obj_set_size(r1,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r1,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r1,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r1,9,0); lv_obj_clear_flag(r1,LV_OBJ_FLAG_SCROLLABLE);
    Theme::pill(r1, statusKey(h->status), Theme::statusColor(h->status));
    lv_obj_t* nm=lv_label_create(r1); lv_label_set_text(nm,h->name);
    lv_obj_set_style_text_font(nm,&lv_font_montserrat_16,0); lv_obj_set_style_text_color(nm,Theme::tx(),0);
    lv_obj_t* ip=lv_label_create(r1); lv_label_set_text(ip,h->addr);
    lv_obj_set_style_text_color(ip,Theme::mut(),0); lv_obj_set_style_text_font(ip,&lv_font_montserrat_12,0);
    lv_obj_set_flex_grow(ip,1); lv_obj_set_style_text_align(ip,LV_TEXT_ALIGN_RIGHT,0);

    lv_obj_t* r2=lv_obj_create(sc); lv_obj_remove_style_all(r2); lv_obj_set_size(r2,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r2,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r2,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r2,9,0); lv_obj_clear_flag(r2,LV_OBJ_FLAG_SCROLLABLE);
    addCheckChips(r2,h);
    lv_obj_t* msg=lv_label_create(r2); lv_label_set_text(msg, h->msg[0]?h->msg:"");
    lv_label_set_long_mode(msg,LV_LABEL_LONG_DOT); lv_obj_set_flex_grow(msg,1);
    lv_obj_set_style_text_color(msg,Theme::mut(),0); lv_obj_set_style_text_font(msg,&lv_font_montserrat_12,0);
    lv_obj_t* ackb=Theme::button(r2,"Ack",false);
    lv_obj_add_event_cb(ackb, ackCb, LV_EVENT_CLICKED, (void*)h->id);
    lv_obj_t* pb=Theme::button(r2,"Pause",true);
    lv_obj_add_event_cb(pb, pauseCb, LV_EVENT_CLICKED, (void*)h->id);
  }
  if(shown==0){
    lv_obj_t* ok=lv_label_create(right); lv_label_set_text(ok,"All clear - nothing needs attention.");
    lv_obj_set_style_text_color(ok,Theme::up(),0);
  }
  Store::unlock();
}
