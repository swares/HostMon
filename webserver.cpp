/* ============================================================================
 *  webserver.cpp — plain-HTTP dashboard + JSON API (Basic Auth), task-pumped.
 *
 *  On-device TLS is NOT viable on this board: an mbedTLS connection needs
 *  ~16-32 KB of contiguous internal RAM, which the chip can't spare alongside
 *  Wi-Fi + the check engine + the web server (the handshake resets even though
 *  the listener starts). So the dashboard serves over plain HTTP only. For real
 *  TLS, terminate it at a reverse proxy (Caddy/nginx/Traefik) in front of the
 *  device, or keep the device on a trusted/isolated VLAN.
 *
 *  The dashboard is embedded in firmware (web_assets.h, gzip) and served by the
 *  API's static handler; every /api/* endpoint is behind HTTP Basic Auth.
 * ========================================================================== */
#include "webserver.h"
#include "config.h"
#include "api.h"
#include "wifi_portal.h"
#include <LittleFS.h>
#include <HTTPServer.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace httpsserver;

namespace {
  HTTPServer* g_http = nullptr;
  char        g_status[96] = "web: not started";   // shown on the LCD Setup card

  void serverTask(void*){
    for(;;){
      if(g_http) g_http->loop();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void WebServer::begin(){
  LittleFS.begin(true);   // persistent storage (hosts.csv)

  g_http = new HTTPServer(HTTP_PORT, 8);
  Api::registerRoutes(g_http);             // /api/* + embedded static dashboard
  bool httpOk = (g_http->start()==1);
  snprintf(g_status, sizeof(g_status), "http=%s port=%d dashboard=embedded",
           httpOk?"OK":"FAIL", HTTP_PORT);
  Serial.printf("[web] HTTP %s on http://%s/ (Basic Auth on /api)\n",
                httpOk?"started":"FAILED", WifiPortal::ipString());

  // Priority 2 — above the check task (priority 1, same core 0) so a dashboard
  // request preempts an in-progress check's CPU-bound crypto. Below WiFi/lwIP.
  xTaskCreatePinnedToCore(serverTask, "web", 16384, nullptr, 2, nullptr, 0);
}

const char* WebServer::status(){ return g_status; }
