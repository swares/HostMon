/* ============================================================================
 *  validate.h — centralised input validation.
 *
 *  Every external input (SD CSV rows, web API JSON, WiFi/SMTP/webhook config,
 *  host add/edit forms) is validated here before it is allowed to mutate state.
 *  Functions return true on success; on failure they write a short reason into
 *  `err` (caller-supplied buffer) so the API can return a 400 with a message.
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include "model.h"

namespace Valid {
  // Primitives -------------------------------------------------------------
  bool nonEmpty(const char* s);
  bool lenAtMost(const char* s, size_t max);
  bool isPrintableAscii(const char* s);          // rejects control chars / CSV-breaking bytes
  bool noCsvSpecials(const char* s);             // rejects , \n \r " in a field
  bool inRange(long v, long lo, long hi);

  // Domain-specific --------------------------------------------------------
  bool hostName(const char* s, char* err, size_t en);   // 1..31, printable, no commas
  bool groupName(const char* s, char* err, size_t en);  // 1..23
  bool address(const char* s, char* err, size_t en);    // IPv4 or RFC-1123 hostname, <=63
  bool isIPv4(const char* s);
  bool isHostname(const char* s);
  bool port(long p, char* err, size_t en);              // 1..65535
  bool interval(long secs, char* err, size_t en);       // one of the allowed steps
  bool checkKeyEnabledList(const char* s, char* err, size_t en); // "ping|dns|.."
  bool reason(const char* s, char* err, size_t en);     // required, 1..95, printable
  bool who(const char* s, char* err, size_t en);        // optional, <=23
  bool email(const char* s, char* err, size_t en);      // basic x@y.z
  bool url(const char* s, char* err, size_t en);        // http(s)://...
  bool ssid(const char* s, char* err, size_t en);       // 1..32
  bool wifiPass(const char* s, char* err, size_t en);   // "" or 8..64
  bool checkKind(const char* key, CheckKind& out, char* err, size_t en);

  // Allowed per-check interval steps (seconds).
  bool isAllowedInterval(long secs);
}
