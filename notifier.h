/* ============================================================================
 *  notifier.h — webhook (HTTPS/HTTP) alert delivery. (Email/SMTP removed — §10.)
 * ========================================================================== */
#pragma once
#include "model.h"

namespace Notifier {
  void begin();
  // event one of: host.down, host.warn, host.up (recovered), cert.expiring
  void notify(const Host& h, const char* event, const char* msg);

  // Manual "Send test" action from Settings. Returns true on success.
  bool testWebhook(char* err, size_t en);

  // Build the canonical JSON payload (also used by the webhook preview).
  void buildPayload(const Host& h, const char* event, const char* msg, char* out, size_t n);
}
