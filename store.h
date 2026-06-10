/* ============================================================================
 *  store.h — in-memory host store + summary + governance mutations.
 *  All access is guarded by a FreeRTOS mutex (UI task, web task and check
 *  task all touch this state).
 * ========================================================================== */
#pragma once
#include "model.h"

namespace Store {
  void    begin();
  void    lock();
  void    unlock();

  // Host collection
  int     count();
  Host*   at(int i);                 // null if out of range (call under lock)
  Host*   byId(const char* id);
  Host*   byName(const char* name);
  Host*   add();                     // appends a blank host, returns it (or null if full)
  bool    removeById(const char* id);
  void    clear();

  // Summary
  Summary summary();
  void    recount();

  // Governance / mutations (each locks internally)
  bool    ack(const char* id, const char* reason, const char* who);
  bool    pause(const char* id, const char* reason, const char* until, const char* who);
  bool    resume(const char* id);
  bool    clearAck(const char* id);
  bool    setEvery(const char* id, CheckKind k, uint32_t every);
  void    recomputeStatus(Host* h);  // derive host Status from check states

  // Alert ring buffer
  void      pushAlert(const AlertEvt& a);
  int       alertCount();
  AlertEvt  alertAt(int i);          // 0 = most recent

  // Mark store dirty so the LCD UI refreshes.
  bool      consumeDirty();
  void      markDirty();
}
