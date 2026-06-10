/* ============================================================================
 *  display.cpp — Waveshare ESP32-S3-Touch-LCD-4.3 bring-up.
 *
 *  Target stack (see README for exact versions):
 *    - ESP32_Display_Panel  (esp_panel namespace, v1.0.x)
 *    - LVGL v8.x  (lv_conf.h provided in this sketch folder; copy to libraries)
 *    - arduino-esp32 board package 3.0.7
 *
 *  Why prior attempts saw a black screen:
 *    1) The CH422G I/O expander must be initialised FIRST. It drives LCD reset,
 *       the backlight (EXIO2) and the GT911 touch reset. ESP32_Display_Panel's
 *       Board::init()/begin() does this automatically *for the selected board*,
 *       which is why selecting the Waveshare board in the library config is
 *       mandatory (README step). Without it the RGB data is correct but nothing
 *       is lit and touch never resets.
 *
 *  NOTE on the LVGL glue below: the flush/touch callbacks use the public LCD /
 *  Touch driver methods of ESP32_Display_Panel v1.0.x (drawBitmap / getPoints).
 *  If your installed library version names these differently, the library ships
 *  a ready LVGL v8 port at examples/.../lvgl_v8_port.[ch]pp you can drop in and
 *  call instead of this port — the Board init above stays the same.
 *    2) The RGB framebuffer is large (800*480*2 = 768 KB). It only allocates
 *       with PSRAM enabled (Tools > PSRAM: "OPI PSRAM"). We additionally place
 *       the LVGL draw buffers in PSRAM via heap_caps_malloc(MALLOC_CAP_SPIRAM).
 * ========================================================================== */
#include "display.h"
#include "config.h"
#include <lvgl.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {
  Board*  g_board = nullptr;
  LCD*    g_lcd   = nullptr;
  Touch*  g_touch = nullptr;

  lv_disp_draw_buf_t g_draw_buf;
  lv_color_t*        g_buf1 = nullptr;
  lv_color_t*        g_buf2 = nullptr;
  lv_disp_drv_t      g_disp_drv;
  lv_indev_drv_t     g_indev_drv;

  SemaphoreHandle_t  g_lvglMtx = nullptr;
  uint32_t           g_lastTick = 0;

  // ---- LVGL flush: copy the rendered area into the RGB framebuffer ----------
  void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p){
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    if(g_lcd) g_lcd->drawBitmap(area->x1, area->y1, w, h, (const uint8_t*)color_p);
    lv_disp_flush_ready(drv);
  }

  // ---- LVGL touch read: poll GT911 via the panel's touch driver -------------
  void touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data){
    data->state = LV_INDEV_STATE_RELEASED;
    if(!g_touch) return;
    TouchPoint pts[1];
    // readPoints() = readRawData() + getPoints() in one call. getPoints() alone
    // only returns the *previously* read buffer, so it never sees a new touch.
    int n = g_touch->readPoints(pts, 1);    // ESP32_Display_Panel v1.0.4 API
    if(n > 0){
      data->state   = LV_INDEV_STATE_PRESSED;
      data->point.x = pts[0].x;
      data->point.y = pts[0].y;
    }
  }
}

bool Display::begin(){
  g_lvglMtx = xSemaphoreCreateRecursiveMutex();

  // 1) Hardware: this performs CH422G init, RGB bus + backlight, GT911 reset.
  g_board = new Board();
  if(!g_board->init()){ Serial.println("[display] Board::init() failed"); return false; }
  if(!g_board->begin()){ Serial.println("[display] Board::begin() failed"); return false; }
  g_lcd   = g_board->getLCD();
  g_touch = g_board->getTouch();
  if(!g_lcd){ Serial.println("[display] no LCD driver"); return false; }

  // 2) LVGL core
  lv_init();

  // 3) Draw buffers in PSRAM (40 lines double-buffered ≈ 128 KB). These MUST stay
  //    in PSRAM, NOT internal SRAM: internal RAM is scarce, and the monitoring
  //    check task needs a 16 KB *internal* stack. Putting the draw buffers in
  //    internal heap (an earlier RGB-artifact experiment) dropped free internal
  //    RAM to ~22 KB and made xTaskCreate for the check task fail outright, so no
  //    checks ran and every host sat on "pending…". The green RGB-underrun
  //    artifacts are instead solved by not repainting the whole screen every
  //    second — see the scheduler's conditional Store::markDirty().
  const size_t pxPerBuf = (size_t)LCD_W * 40;
  g_buf1 = (lv_color_t*) heap_caps_malloc(pxPerBuf * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  g_buf2 = (lv_color_t*) heap_caps_malloc(pxPerBuf * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!g_buf1 || !g_buf2){
    Serial.println("[display] FATAL: LVGL draw buffer alloc failed — is PSRAM (OPI 8MB) enabled?");
    return false;
  }
  lv_disp_draw_buf_init(&g_draw_buf, g_buf1, g_buf2, pxPerBuf);

  // 4) Display driver
  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res  = LCD_W;
  g_disp_drv.ver_res  = LCD_H;
  g_disp_drv.flush_cb = flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  g_disp_drv.full_refresh = 0;
  lv_disp_drv_register(&g_disp_drv);

  // 5) Touch input device
  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type    = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = touch_cb;
  lv_indev_drv_register(&g_indev_drv);

  g_lastTick = millis();
  Serial.println("[display] LVGL ready (800x480 RGB, PSRAM buffers).");
  return true;
}

void Display::loop(){
  uint32_t now = millis();
  lv_tick_inc(now - g_lastTick);
  g_lastTick = now;
  lock();
  lv_timer_handler();
  unlock();
}

void Display::lock(){ if(g_lvglMtx) xSemaphoreTakeRecursive(g_lvglMtx, portMAX_DELAY); }
void Display::unlock(){ if(g_lvglMtx) xSemaphoreGiveRecursive(g_lvglMtx); }
