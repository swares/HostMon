/* ============================================================================
 *  tls_gate.h — single global "one outbound TLS session at a time" gate.
 *
 *  On this board an mbedTLS session needs ~16-32 KB of contiguous internal RAM,
 *  and there's only headroom for ONE at a time (§7/§16 of the README). Outbound
 *  TLS happens from two different tasks:
 *    - the CHECK task     — HTTPS host checks, the cert-expiry probe, the TLS
 *                           self-test, and alert-driven webhook sends;
 *    - the WEB SERVER task — the dashboard's "Send test" webhook.
 *  Those can overlap, so two ~32 KB sessions could try to allocate at once and
 *  collide. This gate serializes them: every TLS site takes the mutex for the
 *  duration of its session.
 *
 *  The mutex has priority inheritance, so a low-priority holder (check task) is
 *  briefly boosted if the higher-priority web task is waiting on it.
 * ========================================================================== */
#pragma once
#include <Arduino.h>

namespace TlsGate {
  void begin();                          // create the mutex once, at boot
  bool acquire(uint32_t timeoutMs);      // true if the single TLS slot was obtained
  void release();                        // release it (same task that acquired)
  bool heapOk();                         // true if there's enough contiguous internal
                                         // RAM (>= TLS_MIN_FREE_BLOCK) to start a session

  // Scoped guard. `engage=false` makes it a no-op (for the non-TLS code paths, e.g.
  // a plain-HTTP host check or http:// webhook), so those never wait on the gate.
  // When engaged it first checks free heap (heapOk) and skips entirely if RAM is too
  // tight — so a TLS session can't drive internal RAM into an OOM fault. ok() is true
  // if the slot wasn't needed OR was obtained; lowMem() distinguishes a heap skip from
  // a busy-slot timeout. The slot is released automatically on scope exit.
  struct Lock {
    bool engaged, held, lowmem;
    Lock(bool engage, uint32_t timeoutMs) : engaged(engage), held(false), lowmem(false){
      if(engage){
        if(!heapOk()){ lowmem = true; return; }   // too little RAM — don't even try
        held = acquire(timeoutMs);
      }
    }
    ~Lock(){ if(held) release(); }
    bool ok() const { return !engaged || held; }
    bool lowMem() const { return lowmem; }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };
}
