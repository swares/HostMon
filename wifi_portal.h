/* ============================================================================
 *  wifi_portal.h — STA join with first-run captive AP fallback.
 * ========================================================================== */
#pragma once
#include <Arduino.h>

namespace WifiPortal {
  void  begin();
  void  loop();
  bool  isAP();                 // true => captive 'HostMon' AP is active
  bool  isOnline();             // STA connected to LAN
  const char* ipString();       // current dashboard IP
  const char* diag();           // on-screen WiFi bring-up diagnostics

  // Used by the API/setup flow.
  String scanJson();            // JSON array of nearby networks
  bool   joinNetwork(const char* ssid, const char* pass); // saves + reboots
}
