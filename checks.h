/* ============================================================================
 *  checks.h — the five pluggable host checks.
 * ========================================================================== */
#pragma once
#include "model.h"

struct CheckResult {
  CheckState state = CheckState::Off;
  char       detail[40] = {0};
  uint32_t   rttMs = 0;     // representative latency for sparkline (0 if n/a)
};

namespace Checks {
  // Runs one check against a host. Network-blocking; call from the check task.
  CheckResult run(const Host& h, CheckKind k);

  // One-shot on-device TLS viability probe (insecure handshake to TLS_SELFTEST_HOST).
  // Network-blocking + needs the deep check-task stack; call once from the check task.
  // Fills a result string (largest-free-block + setup/handshake outcome) for the LCD.
  void        tlsSelfTest();
  const char* tlsSelfTestResult();   // "TLS …" line shown on the Alerts/Setup card
}
