/* ============================================================================
 *  lv_conf.h — LVGL v8 configuration for Host Monitor.
 *
 *  INSTALL: copy this file to your Arduino "libraries" folder, i.e. next to
 *  the "lvgl" library folder (…/Arduino/libraries/lv_conf.h), OR enable
 *  LV_CONF_PATH. Any LVGL option not set here falls back to LVGL's defaults.
 *  Targets LVGL 8.3.x.
 * ========================================================================== */
#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

/* 16-bit colour to match the RGB565 panel. Parallel RGB => no byte swap. */
#define LV_COLOR_DEPTH      16
#define LV_COLOR_16_SWAP    0

/* Memory: route LVGL's pool to PSRAM (heap_caps) instead of a 64KB static array
 * in internal RAM. Internal DRAM is scarce here and WiFi needs contiguous
 * internal DMA blocks for softAP(); keeping LVGL's pool in PSRAM frees ~64KB of
 * internal RAM. The perf-critical RGB draw buffers live in PSRAM already. */
#define LV_MEM_CUSTOM             1
#define LV_MEM_CUSTOM_INCLUDE     <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size)        heap_caps_malloc((size), MALLOC_CAP_SPIRAM)
#define LV_MEM_CUSTOM_FREE(ptr)          heap_caps_free(ptr)
#define LV_MEM_CUSTOM_REALLOC(ptr, size) heap_caps_realloc((ptr), (size), MALLOC_CAP_SPIRAM)

/* Tick is provided manually via lv_tick_inc() in Display::loop(). */
#define LV_TICK_CUSTOM      0
#define LV_DPI_DEF          130

/* Drawing */
#define LV_DRAW_COMPLEX     1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

/* Fonts used by the UI (theme.cpp references these sizes). */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

/* Widgets the UI relies on (most default on in v8; set explicitly). */
#define LV_USE_LABEL    1
#define LV_USE_BTN      1
#define LV_USE_BTNMATRIX 1
#define LV_USE_BAR      1
#define LV_USE_ARC      1
#define LV_USE_LINE     1
#define LV_USE_TABVIEW  1
#define LV_USE_FLEX     1
#define LV_USE_GRID     1

/* Theme: default dark theme as our base; theme.cpp layers tokens on top. */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_THEME_BASIC   1

#endif /* LV_CONF_H */
