/* validate.cpp — implementation of the input validation layer. */
#include "validate.h"

static const long kIntervals[] = {10,30,60,120,300,900,3600,21600,43200,86400};

static void setErr(char* e, size_t n, const char* m){ if(e&&n) strlcpy(e,m,n); }

bool Valid::nonEmpty(const char* s){ return s && s[0]; }
bool Valid::lenAtMost(const char* s, size_t max){ return s && strlen(s)<=max; }
bool Valid::inRange(long v, long lo, long hi){ return v>=lo && v<=hi; }

bool Valid::isPrintableAscii(const char* s){
  if(!s) return false;
  for(const char* p=s; *p; ++p) if((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e) return false;
  return true;
}
bool Valid::noCsvSpecials(const char* s){
  if(!s) return false;
  for(const char* p=s; *p; ++p) if(*p==','||*p=='\n'||*p=='\r'||*p=='"') return false;
  return true;
}

bool Valid::isIPv4(const char* s){
  if(!s) return false;
  int parts=0; const char* p=s;
  while(*p){
    if(!isdigit((unsigned char)*p)) return false;
    int v=0,d=0;
    while(isdigit((unsigned char)*p)){ v=v*10+(*p-'0'); d++; p++; if(d>3) return false; }
    if(v>255) return false; parts++;
    if(*p=='.'){ p++; if(!*p) return false; }
    else if(*p) return false;
  }
  return parts==4;
}
bool Valid::isHostname(const char* s){
  // RFC-1123-ish: labels of [a-zA-Z0-9-], 1..63, dot-separated, no leading/trailing -
  if(!s||!*s) return false;
  size_t n=strlen(s); if(n>63) return false;
  int label=0;
  for(size_t i=0;i<n;i++){
    char c=s[i];
    if(c=='.'){ if(label==0) return false; if(s[i-1]=='-') return false; label=0; continue; }
    bool okc = isalnum((unsigned char)c) || c=='-';
    if(!okc) return false;
    if(label==0 && c=='-') return false;        // label can't start with -
    if(++label>63) return false;
  }
  if(s[n-1]=='-'||s[n-1]=='.') return false;
  return true;
}

bool Valid::hostName(const char* s, char* e, size_t en){
  if(!nonEmpty(s)){ setErr(e,en,"name required"); return false; }
  if(!lenAtMost(s,31)){ setErr(e,en,"name too long (max 31)"); return false; }
  if(!isPrintableAscii(s)||!noCsvSpecials(s)){ setErr(e,en,"name has invalid characters"); return false; }
  return true;
}
bool Valid::groupName(const char* s, char* e, size_t en){
  if(!nonEmpty(s)){ setErr(e,en,"group required"); return false; }
  if(!lenAtMost(s,23)){ setErr(e,en,"group too long (max 23)"); return false; }
  if(!isPrintableAscii(s)||!noCsvSpecials(s)){ setErr(e,en,"group has invalid characters"); return false; }
  return true;
}
bool Valid::address(const char* s, char* e, size_t en){
  if(!nonEmpty(s)){ setErr(e,en,"address required"); return false; }
  if(!lenAtMost(s,63)){ setErr(e,en,"address too long (max 63)"); return false; }
  if(!noCsvSpecials(s)){ setErr(e,en,"address has invalid characters"); return false; }
  if(isIPv4(s)||isHostname(s)) return true;
  setErr(e,en,"address must be a valid IPv4 or hostname"); return false;
}
bool Valid::port(long p, char* e, size_t en){
  if(!inRange(p,1,65535)){ setErr(e,en,"port out of range (1-65535)"); return false; } return true;
}
bool Valid::isAllowedInterval(long s){
  for(long v : kIntervals) if(v==s) return true; return false;
}
bool Valid::interval(long secs, char* e, size_t en){
  if(!isAllowedInterval(secs)){ setErr(e,en,"interval must be one of 10s..1d steps"); return false; } return true;
}
bool Valid::checkKind(const char* key, CheckKind& out, char* e, size_t en){
  if(!strcmp(key,"https")||!strcmp(key,"httpsv")){ out=CheckKind::Http; return true; } // secure Http check (+verify)
  for(uint8_t i=0;i<kCheckCount;i++) if(!strcmp(key, checkMeta((CheckKind)i).key)){ out=(CheckKind)i; return true; }
  setErr(e,en,"unknown check key"); return false;
}
bool Valid::checkKeyEnabledList(const char* s, char* e, size_t en){
  if(!s) return true;                              // empty allowed (defaults applied)
  if(strlen(s)>63){ setErr(e,en,"checks list too long"); return false; }
  char tmp[64]; strlcpy(tmp,s,sizeof(tmp));
  char* save=nullptr; char* tok=strtok_r(tmp,"|",&save);
  while(tok){ CheckKind k; if(!checkKind(tok,k,e,en)) return false; tok=strtok_r(nullptr,"|",&save); }
  return true;
}
bool Valid::reason(const char* s, char* e, size_t en){
  if(!nonEmpty(s)){ setErr(e,en,"reason is required"); return false; }
  if(!lenAtMost(s,95)){ setErr(e,en,"reason too long (max 95)"); return false; }
  if(!isPrintableAscii(s)){ setErr(e,en,"reason has invalid characters"); return false; }
  return true;
}
bool Valid::who(const char* s, char* e, size_t en){
  if(!s||!*s) return true;
  if(!lenAtMost(s,23)){ setErr(e,en,"'who' too long (max 23)"); return false; }
  if(!isPrintableAscii(s)){ setErr(e,en,"'who' has invalid characters"); return false; }
  return true;
}
bool Valid::email(const char* s, char* e, size_t en){
  if(!s||!*s) return true;                          // empty = disabled, allowed
  if(!lenAtMost(s,63)){ setErr(e,en,"email too long"); return false; }
  const char* at=strchr(s,'@'); if(!at||at==s){ setErr(e,en,"invalid email"); return false; }
  const char* dot=strchr(at,'.'); if(!dot||dot[1]==0){ setErr(e,en,"invalid email"); return false; }
  return true;
}
bool Valid::url(const char* s, char* e, size_t en){
  if(!s||!*s) return true;                          // empty = disabled, allowed
  if(!lenAtMost(s,159)){ setErr(e,en,"URL too long"); return false; }
  if(strncmp(s,"http://",7)&&strncmp(s,"https://",8)){ setErr(e,en,"URL must start with http(s)://"); return false; }
  return true;
}
bool Valid::ssid(const char* s, char* e, size_t en){
  if(!nonEmpty(s)){ setErr(e,en,"SSID required"); return false; }
  if(!lenAtMost(s,32)){ setErr(e,en,"SSID too long (max 32)"); return false; }
  return true;
}
bool Valid::wifiPass(const char* s, char* e, size_t en){
  if(!s) return true;
  size_t n=strlen(s);
  if(n==0) return true;                             // open network
  if(n<8||n>64){ setErr(e,en,"Wi-Fi password must be 8-64 chars"); return false; }
  return true;
}
