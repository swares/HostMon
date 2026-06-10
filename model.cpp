/* model.cpp — static check metadata + small formatting helpers. */
#include "model.h"

static const CheckMeta META[kCheckCount] = {
  { "ping",  "Ping",           "PNG", DEF_INT_PING  },
  { "dns",   "DNS resolution", "DNS", DEF_INT_DNS   },
  { "port",  "Port open",      "PRT", DEF_INT_PORT  },
  { "http",  "HTTP code",      "HTP", DEF_INT_HTTP  },
  { "trace", "Traceroute",     "TRC", DEF_INT_TRACE },
};

const CheckMeta& checkMeta(CheckKind k){ return META[(uint8_t)k]; }
uint32_t defaultInterval(CheckKind k){ return META[(uint8_t)k].def; }

const char* statusKey(Status s){
  switch(s){ case Status::Up:return "up"; case Status::Warn:return "warn";
    case Status::Down:return "down"; case Status::Paused:return "paused";
    case Status::Ack:return "ack"; } return "up";
}
const char* checkStateKey(CheckState s){
  switch(s){ case CheckState::Up:return "up"; case CheckState::Warn:return "warn";
    case CheckState::Down:return "down"; case CheckState::Off:return "off"; } return "off";
}
void fmtEvery(uint32_t s, char* out, size_t n){
  if(s>=86400 && s%86400==0)      snprintf(out,n,"%ud", s/86400);
  else if(s>=3600 && s%3600==0)   snprintf(out,n,"%uh", s/3600);
  else if(s>=60 && s%60==0)       snprintf(out,n,"%um", s/60);
  else if(s>=3600)                snprintf(out,n,"%uh", s/3600);
  else if(s>=60)                  snprintf(out,n,"%um", s/60);
  else                            snprintf(out,n,"%us", s);
}
