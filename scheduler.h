/* ============================================================================
 *  scheduler.h — background check engine.
 *  Runs each enabled check on its own interval, derives host status, and
 *  triggers alerts (respecting fails-before-alert and re-notify settings).
 * ========================================================================== */
#pragma once
#include <Arduino.h>

namespace Scheduler {
  void begin();           // spawns the check task
  void runOnce();         // one scan pass (also callable directly for testing)
  const char* status();   // live diag for the LCD Setup card
  const char* perf();     // load metrics: queue depth / timeouts / scan time
}
