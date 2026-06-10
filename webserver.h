/* ============================================================================
 *  webserver.h — HTTPS dashboard/API + plain-HTTP redirect/captive.
 * ========================================================================== */
#pragma once
#include <Arduino.h>

namespace WebServer {
  void begin();
  const char* status();   // short start-result string for the LCD Setup card
}
