/* ============================================================================
 *  theme.h — LVGL design tokens (colours + shared styles) from the spec.
 * ========================================================================== */
#pragma once
#include <lvgl.h>
#include "model.h"

namespace Theme {
  void init();

  // Palette (hex from the design tokens)
  static inline lv_color_t bg()    { return lv_color_hex(0x0d1117); }
  static inline lv_color_t bg2()   { return lv_color_hex(0x0a0e13); }
  static inline lv_color_t panel() { return lv_color_hex(0x151b23); }
  static inline lv_color_t raise() { return lv_color_hex(0x1a2230); }
  static inline lv_color_t line()  { return lv_color_hex(0x26313f); }
  static inline lv_color_t tx()    { return lv_color_hex(0xe8eef6); }
  static inline lv_color_t mut()   { return lv_color_hex(0x8b99ac); }
  static inline lv_color_t faint() { return lv_color_hex(0x5b6779); }
  static inline lv_color_t teal()  { return lv_color_hex(0x2dd4bf); }
  static inline lv_color_t up()    { return lv_color_hex(0x34d399); }
  static inline lv_color_t warn()  { return lv_color_hex(0xfbbf24); }
  static inline lv_color_t down()  { return lv_color_hex(0xf87171); }
  static inline lv_color_t paused(){ return lv_color_hex(0xa78bfa); }
  static inline lv_color_t ack()   { return lv_color_hex(0x38bdf8); }

  lv_color_t statusColor(Status s);
  lv_color_t checkColor(CheckState s);

  // Reusable widget factories.
  lv_obj_t* card(lv_obj_t* parent);                 // panel bg, rounded, padded
  lv_obj_t* pill(lv_obj_t* parent, const char* text, lv_color_t c);
  lv_obj_t* chip(lv_obj_t* parent, const char* text, lv_color_t c, bool filled);
  lv_obj_t* dot(lv_obj_t* parent, lv_color_t c, int size=8);
  lv_obj_t* button(lv_obj_t* parent, const char* text, bool primary);
}
