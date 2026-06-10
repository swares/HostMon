/* ============================================================================
 *  csv.h — SD card mount + hosts.csv read/write.
 *
 *  CSV format (header required), one host per row:
 *    name,address,group,checks,intervals,alerts
 *  where:
 *    checks    = pipe list of enabled check keys, e.g. ping|dns|port|https|trace
 *                (http key variants: http | https | httpsv where v = verify cert)
 *    intervals = pipe list of key:seconds overrides, e.g. ping:10|http:30 (optional)
 *    alerts    = pipe list from {down,warn,recovered} (optional, default down|recovered)
 *
 *  Example:
 *    name,address,group,checks,intervals,alerts
 *    nas-01,192.168.1.10,Storage,ping|dns|port|http|trace,,down|recovered
 *    pihole,192.168.1.12,Network,ping|dns|port|http,http:30,down|warn|recovered
 * ========================================================================== */
#pragma once
#include <Arduino.h>

namespace Storage { bool beginSD(); bool sdReady(); const char* diag(); }

namespace Csv {
  int  loadHosts(const char* path);   // returns number of hosts loaded
  bool saveHosts(const char* path);   // writes the current store back to SD
}
