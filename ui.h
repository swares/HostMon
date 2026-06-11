/* ============================================================================
 *  ui.h — on-device LCD UI (LVGL v8). Two home screens: A·Health, B·Grid.
 * ========================================================================== */
#pragma once
#include <lvgl.h>

namespace UI {
  void begin();        // build root + bottom nav, show default home screen
  void loop();         // refresh data when the store changes (~1s)
  void show(char screen);   // 'A' Health, 'B' Grid

  // Implemented in screen_health.cpp / screen_grid.cpp.
  void buildHealth(lv_obj_t* parent);
  void buildGrid(lv_obj_t* parent);

  // Hosts-grid incremental repaint: update just the status dots / check chips / count
  // pills in place (no full rebuild → no RGB-underrun flush). gridStructureChanged()
  // is true when the host set/names/enabled-checks changed and a full rebuild is needed.
  void updateGrid();
  bool gridStructureChanged();

  // Shared: a touch handler that acks/pauses a host by id (default reason).
  void ackHost(const char* id);
  void pauseHost(const char* id);

  // A screen registers its clock label here so UI::loop() can update just the time
  // in place each minute instead of rebuilding the whole screen.
  void registerClock(lv_obj_t* lbl);
}
