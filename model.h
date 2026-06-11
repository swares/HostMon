/* ============================================================================
 *  model.h — core data types shared across the firmware.
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include "config.h"

// Host-level status (worst-first ordering used for sorting/triage).
enum class Status : uint8_t { Down=0, Warn=1, Ack=2, Paused=3, Up=4 };

// Per-check result status.
enum class CheckState : uint8_t { Down=0, Warn=1, Up=2, Off=3 };

// The built-in check kinds. Order is fixed and mirrors the UI.
// The SSL/TLS cert-expiry check runs an INSECURE handshake (reads the cert notAfter,
// accepts any cert) — one outbound TLS session at a time, behind the TLS gate. On-device
// TLS is viable here only because RAM was reclaimed; see README §7/§16.
enum class CheckKind : uint8_t { Ping=0, Dns=1, Port=2, Http=3, Ssl=4, Trace=5 };
static const uint8_t kCheckCount = 6;

struct Check {
  CheckKind  kind;
  CheckState state   = CheckState::Off;
  bool       enabled = false;
  uint32_t   every   = 0;      // seconds; 0 => use default for kind
  uint32_t   lastRun = 0;      // millis()/1000 of last execution
  uint16_t   port    = 0;      // for Port check (0 => derive/none)
  bool       secure  = false;  // Http check: true => HTTPS (always insecure), false => HTTP
  uint32_t   lastRttMs = 0;    // last measured latency (ms)
  char       detail[40] = {0}; // human string e.g. "0.4 ms", "200 OK"
  // Small rolling latency history for the on-detail sparkline.
  uint16_t   hist[24] = {0};
  uint8_t    histLen = 0;
};

struct Host {
  char     id[12]   = {0};
  char     name[32] = {0};
  char     addr[64] = {0};     // IP or hostname
  char     group[24]= {0};
  Status   status   = Status::Up;
  Check    checks[kCheckCount];

  // Derived / governance state
  char     msg[80]  = {0};     // current problem message
  uint16_t fails    = 0;       // consecutive host-level fails
  uint32_t sinceMs  = 0;       // when current outage started (millis)
  char     upStr[12]= {0};     // uptime string e.g. "62d"
  float    rtt      = 0;       // representative latency (ms), -1 = n/a
  int32_t  last     = -1;      // seconds since last check (-1 = n/a)

  char     ackBy[24]={0};   char ackReason[96]={0}; char ackAt[8]={0};
  char     pauseBy[24]={0}; char pauseReason[96]={0}; char pauseUntil[24]={0};
  Status   prevStatus = Status::Up;  // restore target after resume

  // Per-host alert routing
  bool alertDown=true, alertRecovered=true, alertWarn=false;
};

struct Summary {
  uint16_t total=0, up=0, warn=0, down=0, paused=0, ack=0;
  float    uptime30=99.91f;
  uint16_t attention=0;        // down + warn
};

struct AlertEvt {
  char     time[6]  = {0};     // "14:52"
  char     host[32] = {0};
  char     check[40]= {0};
  CheckState sev    = CheckState::Down;
  char     label[12]= {0};     // DOWN / WARN / RECOVERED
  char     msg[96]  = {0};
  uint8_t  state    = 0;       // 0 firing,1 ack,2 resolved
  bool     viaEmail=false, viaWebhook=false;
  uint32_t at=0;               // millis
};

// ---- Static metadata for the six checks -----------------------------------
struct CheckMeta { const char* key; const char* name; const char* abbr; uint32_t def; };
const CheckMeta& checkMeta(CheckKind k);
const char* statusKey(Status s);     // "up","warn","down","paused","ack"
const char* checkStateKey(CheckState s);
uint32_t    defaultInterval(CheckKind k);
void        fmtEvery(uint32_t secs, char* out, size_t n);  // 30s/5m/12h/1d
