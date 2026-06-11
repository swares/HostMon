/* tls_gate.cpp — implementation of the single outbound-TLS mutex (see tls_gate.h). */
#include "tls_gate.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>

namespace { SemaphoreHandle_t g_mtx = nullptr; }

bool TlsGate::heapOk(){
  // Need both a big enough CONTIGUOUS block (for the ~16 KB session buffers) and enough
  // TOTAL free that the whole ~44 KB session can fit without faulting. Either too low
  // => skip the session this cycle.
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= (size_t)TLS_MIN_FREE_BLOCK
      && heap_caps_get_free_size(MALLOC_CAP_INTERNAL)          >= (size_t)TLS_MIN_FREE_TOTAL;
}

void TlsGate::begin(){
  if(!g_mtx) g_mtx = xSemaphoreCreateMutex();   // priority-inheriting mutex
}

bool TlsGate::acquire(uint32_t timeoutMs){
  if(!g_mtx) begin();                 // lazy fallback if begin() wasn't called yet
  if(!g_mtx) return true;             // creation failed: degrade to "no gate" rather
                                      // than block notifications/checks entirely
  return xSemaphoreTake(g_mtx, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void TlsGate::release(){
  if(g_mtx) xSemaphoreGive(g_mtx);
}
