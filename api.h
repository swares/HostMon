/* ============================================================================
 *  api.h — JSON REST API + static file serving over HTTPS (esp32_https_server).
 *  All routes require HTTP Basic Auth. POST bodies are validated (validate.h).
 *
 *  GET  /api/summary /hosts /host?id= /alerts /settings /status /wifi/scan
 *  POST /api/host/{ack,pause,resume,clear,interval,delete}  /api/host
 *  POST /api/settings/{email,webhook,defaults,auth}
 *  POST /api/test/{email,webhook}  /api/sd/reload  /api/wifi/{join,ap}
 * ========================================================================== */
#pragma once
namespace httpsserver { class HTTPServer; class HTTPRequest; class HTTPResponse; }

namespace Api {
  // Registers JSON API routes + the embedded static dashboard as the default
  // node. Takes the base HTTPServer so it works for the plain-HTTP server.
  void registerRoutes(httpsserver::HTTPServer* server);
  // Basic-Auth gate used by the /api/* data endpoints.
  bool authed(httpsserver::HTTPRequest* req, httpsserver::HTTPResponse* res);
}
