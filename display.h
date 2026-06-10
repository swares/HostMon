/* ============================================================================
 *  display.h — panel + LVGL bring-up for the Waveshare ESP32-S3-Touch-LCD-4.3
 * ========================================================================== */
#pragma once
#include <Arduino.h>

namespace Display {
  bool begin();    // CH422G -> RGB panel -> GT911 -> LVGL. false if PSRAM FB fails.
  void loop();     // LVGL tick + lv_timer_handler(); call frequently from loop()
  void lock();     // take the LVGL mutex before touching lv_* from another task
  void unlock();
}
