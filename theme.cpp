/* theme.cpp — shared LVGL styles + widget factories. */
#include "theme.h"

namespace { lv_style_t s_card; bool s_init=false; }

void Theme::init(){
  if(s_init) return; s_init=true;
  lv_style_init(&s_card);
  lv_style_set_bg_color(&s_card, Theme::panel());
  lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
  lv_style_set_border_color(&s_card, Theme::line());
  lv_style_set_border_width(&s_card, 1);
  lv_style_set_radius(&s_card, 15);
  lv_style_set_pad_all(&s_card, 14);
  lv_style_set_text_color(&s_card, Theme::tx());
}

lv_color_t Theme::statusColor(Status s){
  switch(s){ case Status::Up:return up(); case Status::Warn:return warn();
    case Status::Down:return down(); case Status::Paused:return paused();
    case Status::Ack:return ack(); } return up();
}
lv_color_t Theme::checkColor(CheckState s){
  switch(s){ case CheckState::Up:return up(); case CheckState::Warn:return warn();
    case CheckState::Down:return down(); case CheckState::Off:return faint(); } return faint();
}

lv_obj_t* Theme::card(lv_obj_t* p){
  lv_obj_t* o=lv_obj_create(p);
  lv_obj_add_style(o,&s_card,0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
lv_obj_t* Theme::dot(lv_obj_t* p, lv_color_t c, int size){
  lv_obj_t* d=lv_obj_create(p);
  lv_obj_set_size(d,size,size); lv_obj_set_style_radius(d,size/2,0);
  lv_obj_set_style_bg_color(d,c,0); lv_obj_set_style_bg_opa(d,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(d,0,0); lv_obj_clear_flag(d,LV_OBJ_FLAG_SCROLLABLE);
  return d;
}
lv_obj_t* Theme::pill(lv_obj_t* p, const char* text, lv_color_t c){
  lv_obj_t* b=lv_obj_create(p);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(b,20,0);
  lv_obj_set_style_bg_color(b,c,0); lv_obj_set_style_bg_opa(b,LV_OPA_20,0);
  lv_obj_set_style_border_width(b,0,0);
  lv_obj_set_style_pad_hor(b,9,0); lv_obj_set_style_pad_ver(b,3,0);
  lv_obj_clear_flag(b,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l=lv_label_create(b); lv_label_set_text(l,text);
  lv_obj_set_style_text_color(l,c,0); lv_obj_set_style_text_font(l,&lv_font_montserrat_12,0);
  lv_obj_center(l);
  return b;
}
lv_obj_t* Theme::chip(lv_obj_t* p, const char* text, lv_color_t c, bool filled){
  lv_obj_t* b=lv_obj_create(p);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(b,6,0);
  lv_obj_set_style_bg_color(b, filled?c:Theme::raise(),0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER,0);
  lv_obj_set_style_border_width(b, filled?0:1,0);
  lv_obj_set_style_border_color(b, Theme::line(),0);
  lv_obj_set_style_pad_hor(b,5,0); lv_obj_set_style_pad_ver(b,2,0);
  lv_obj_clear_flag(b,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l=lv_label_create(b); lv_label_set_text(l,text);
  lv_obj_set_style_text_color(l, filled?lv_color_hex(0x06281c):Theme::faint(),0);
  lv_obj_set_style_text_font(l,&lv_font_montserrat_10,0);
  lv_obj_center(l);
  return b;
}
lv_obj_t* Theme::button(lv_obj_t* p, const char* text, bool primary){
  lv_obj_t* b=lv_btn_create(p);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(b,9,0);
  lv_obj_set_style_bg_color(b, primary?Theme::teal():Theme::raise(),0);
  lv_obj_set_style_border_width(b, primary?0:1,0);
  lv_obj_set_style_border_color(b, Theme::line(),0);
  lv_obj_set_style_pad_hor(b,12,0); lv_obj_set_style_pad_ver(b,7,0);
  lv_obj_set_style_shadow_width(b,0,0);
  lv_obj_t* l=lv_label_create(b); lv_label_set_text(l,text);
  lv_obj_set_style_text_color(l, primary?lv_color_hex(0x04231b):Theme::tx(),0);
  lv_obj_set_style_text_font(l,&lv_font_montserrat_12,0); lv_obj_center(l);
  return b;
}
