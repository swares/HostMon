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
}
