/* ============================================================================
 *  api.h — JSON REST API + static file serving over HTTP (esp32_https_server's
 *  HTTP server). All routes require HTTP Basic Auth. POST bodies validated (validate.h).
 *
 *  GET  /api/summary /hosts /host?id= /alerts /settings /status /wifi/scan
 *  POST /api/host/{ack,pause,resume,clear,interval,delete}  /api/host
 *  POST /api/settings/{webhook,defaults,auth}
 *  POST /api/test/webhook  /api/sd/reload  /api/wifi/join
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
