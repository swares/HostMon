/* csv.cpp — LittleFS-backed hosts.csv (de)serialisation. */
#include "csv.h"
#include "config.h"
#include "store.h"
#include "settings.h"
#include "validate.h"
#include "display.h"
#include <LittleFS.h>

namespace {
  bool g_sd=false;                 // g_sd == "persistent FS is mounted"
  char g_mount[40]="fs:?";         // on-screen diagnostics (Setup card)
  int  g_loadN=-1;
  char g_save[28]="save:none";
  bool g_beganOnce=false;
}

bool Storage::sdReady(){ return g_sd; }

const char* Storage::diag(){
  static char buf[80];
  snprintf(buf,sizeof(buf),"%s ld=%d %s", g_mount, g_loadN, g_save);
  return buf;
}

bool Storage::beginSD(){
  // Persistent storage is the on-flash LittleFS ("spiffs" partition), NOT an SD
  // card. On this board the SD chip-select falls back to GPIO10, which is an
  // active RGB data line (DATA4) — mounting SD while the panel runs corrupts the
  // parallel bus and faults the chip. LittleFS needs no extra pins. (Function
  // name kept so callers/API don't change.)
  if(g_beganOnce && g_sd) return true;     // already mounted (called from 2 places)

  // Try to mount WITHOUT formatting first, so we can tell a healthy remount apart
  // from a "had to wipe it" event — the latter is what silently drops saved hosts.
  bool mounted = LittleFS.begin(false);
  if(mounted){
    g_sd=true;
    bool ex = LittleFS.exists(HOSTS_CSV_PATH); size_t sz=0;
    if(ex){ File f=LittleFS.open(HOSTS_CSV_PATH, FILE_READ); if(f){ sz=f.size(); f.close(); } }
    snprintf(g_mount,sizeof(g_mount),"fs OK csv=%s %uB", ex?"y":"n", (unsigned)sz);
  } else {
    bool formatted = LittleFS.begin(true);   // mount failed → format (THIS WIPES DATA)
    g_sd=formatted;
    snprintf(g_mount,sizeof(g_mount),"fs WIPED(remount-fail) ok=%d", formatted?1:0);
    Serial.println("[fs] LittleFS mount failed -> formatted (data lost)");
  }
  g_beganOnce=true;
  return g_sd;
}

// ---- tiny CSV field splitter (handles simple unquoted fields) --------------
static int split(char* line, char sep, char** out, int maxN){
  int n=0; char* p=line;
  out[n++]=p;
  while(*p && n<maxN){ if(*p==sep){ *p=0; out[n++]=p+1; } p++; }
  return n;
}
static CheckKind kindFromKey(const char* k){
  if(!strcmp(k,"ping"))return CheckKind::Ping; if(!strcmp(k,"dns"))return CheckKind::Dns;
  if(!strcmp(k,"port"))return CheckKind::Port; if(!strcmp(k,"http"))return CheckKind::Http;
  if(!strcmp(k,"https")||!strcmp(k,"httpsv"))return CheckKind::Http;  // Http check, secure (v=verify)
  return CheckKind::Trace;
}

int Csv::loadHosts(const char* path){
  if(!g_sd){ g_loadN=-2; return 0; }       // -2 = fs not mounted
  File f = LittleFS.open(path, FILE_READ);
  if(!f){ g_loadN=0; return 0; }           // no hosts file yet (fresh flash)
  Store::lock();
  Store::clear();
  bool header=true; int loaded=0; char line[256];
  while(f.available()){
    size_t len=f.readBytesUntil('\n', line, sizeof(line)-1); line[len]=0;
    if(len && line[len-1]=='\r') line[len-1]=0;
    if(!line[0]) continue;
    if(header){ header=false; continue; }     // skip header row

    char* col[6]={0}; int nc=split(line,',',col,6);
    if(nc<3) continue;

    // Legacy compat: the SSL/TLS-expiry check was removed (on-device TLS isn't
    // viable on this board). Strip any ssl|sslv tokens from old hosts.csv rows so
    // the host still loads instead of being rejected as an "unknown check key".
    // On the next save the token is gone for good.
    static char checksClean[64];
    if(nc>=4 && col[3] && *col[3]){
      char tmp[64]; strlcpy(tmp,col[3],sizeof(tmp));
      char* pp[10]={0}; int npp=split(tmp,'|',pp,10);
      checksClean[0]=0; bool f1=true;
      for(int i=0;i<npp;i++){ if(!strcmp(pp[i],"ssl")||!strcmp(pp[i],"sslv")) continue;
        if(!f1) strlcat(checksClean,"|",sizeof(checksClean));
        strlcat(checksClean,pp[i],sizeof(checksClean)); f1=false; }
      col[3]=checksClean;
    }

    // -- validate the row BEFORE allocating a host; skip & log bad rows ----
    char verr[48]={0};
    if(!Valid::hostName(col[0],verr,sizeof(verr)) ||
       !Valid::address (col[1],verr,sizeof(verr)) ||
       !Valid::groupName(col[2],verr,sizeof(verr)) ||
       (nc>=4 && !Valid::checkKeyEnabledList(col[3],verr,sizeof(verr)))){
      Serial.printf("[csv] skipped invalid row '%s': %s\n", col[0]?col[0]:"?", verr);
      continue;
    }

    Host* h=Store::add(); if(!h) break;
    strlcpy(h->name,  col[0], sizeof(h->name));
    strlcpy(h->addr,  col[1], sizeof(h->addr));
    strlcpy(h->group, col[2], sizeof(h->group));
    h->status=Status::Up; strlcpy(h->upStr,"new",sizeof(h->upStr));

    // checks column (which are enabled)
    for(uint8_t i=0;i<kCheckCount;i++){ h->checks[i].enabled=false; h->checks[i].state=CheckState::Off; }
    if(nc>=4 && col[3] && *col[3]){
      char tmp[64]; strlcpy(tmp,col[3],sizeof(tmp));
      char* parts[8]={0}; int np=split(tmp,'|',parts,8);
      for(int i=0;i<np;i++){ CheckKind k=kindFromKey(parts[i]);
        h->checks[(uint8_t)k].enabled=true; h->checks[(uint8_t)k].state=CheckState::Up;
        // https / httpsv (legacy verify variant) both load as HTTPS-insecure now.
        if(k==CheckKind::Http) h->checks[(uint8_t)k].secure = (!strcmp(parts[i],"https")||!strcmp(parts[i],"httpsv"));
        strlcpy(h->checks[(uint8_t)k].detail,"pending…",sizeof(h->checks[(uint8_t)k].detail)); }
    } else {
      for(uint8_t i=0;i<kCheckCount;i++){ if(i==(uint8_t)CheckKind::Trace) continue;
        h->checks[i].enabled=true; h->checks[i].state=CheckState::Up;
        strlcpy(h->checks[i].detail,"pending…",sizeof(h->checks[i].detail)); }
    }
    // interval overrides
    if(nc>=5 && col[4] && *col[4]){
      char tmp[96]; strlcpy(tmp,col[4],sizeof(tmp));
      char* parts[8]={0}; int np=split(tmp,'|',parts,8);
      for(int i=0;i<np;i++){ char* c=strchr(parts[i],':'); if(!c)continue; *c=0;
        CheckKind k; char e2[48];
        if(!Valid::checkKind(parts[i],k,e2,sizeof(e2))) continue;
        long secs=atol(c+1);
        if(!Valid::isAllowedInterval(secs)){
          Serial.printf("[csv] %s: ignoring invalid interval %ld for %s\n", h->name, secs, parts[i]);
          continue;
        }
        h->checks[(uint8_t)k].every=(uint32_t)secs; }
    }
    // alert routing
    if(nc>=6 && col[5] && *col[5]){
      h->alertDown=h->alertWarn=h->alertRecovered=false;
      if(strstr(col[5],"down"))      h->alertDown=true;
      if(strstr(col[5],"warn"))      h->alertWarn=true;
      if(strstr(col[5],"recovered")) h->alertRecovered=true;
    }
    loaded++;
  }
  Store::unlock();
  f.close();
  g_loadN = loaded;
  return loaded;
}

bool Csv::saveHosts(const char* path){
  if(!g_sd){ strlcpy(g_save,"save:NO-FS",sizeof(g_save)); return false; }
  // HARD GUARD: never write before the store has been loaded from flash. Otherwise
  // a request that arrives during boot (web server up, hosts not yet loaded) saves
  // an EMPTY store and clobbers hosts.csv — wiping every saved host. g_loadN stays
  // -1 until loadHosts() has run at least once.
  if(g_loadN < 0){ strlcpy(g_save,"save:BLOCKED(preload)",sizeof(g_save)); return false; }
  // Hold the LVGL lock for the whole write: this flash write disables the cache,
  // and we don't want the panel/LVGL reading PSRAM through a dead cache while it
  // happens (that's what turns the brief glitch into a fault). The RGB DMA still
  // runs but recovers at the next VSYNC.
  Display::lock();
  File f = LittleFS.open(path, FILE_WRITE);   // truncates
  if(!f){ Display::unlock(); strlcpy(g_save,"save:OPEN-FAIL",sizeof(g_save)); return false; }
  f.println("name,address,group,checks,intervals,alerts");
  Store::lock();
  for(int i=0;i<Store::count();i++){
    Host* h=Store::at(i);
    f.printf("%s,%s,%s,", h->name, h->addr, h->group);
    // checks
    bool first=true;
    for(uint8_t c=0;c<kCheckCount;c++) if(h->checks[c].enabled){
      // Encode the HTTP scheme into the key so it round-trips: http | https.
      const Check& ck=h->checks[c]; const char* key=checkMeta((CheckKind)c).key;
      if((CheckKind)c==CheckKind::Http && ck.secure) key = "https";
      f.printf("%s%s", first?"":"|", key); first=false; }
    f.print(',');
    // interval overrides (non-default only)
    first=true;
    for(uint8_t c=0;c<kCheckCount;c++) if(h->checks[c].enabled &&
        h->checks[c].every!=defaultInterval((CheckKind)c)){
      f.printf("%s%s:%u", first?"":"|", checkMeta((CheckKind)c).key, h->checks[c].every); first=false; }
    f.print(',');
    // alerts
    first=true;
    if(h->alertDown){ f.print("down"); first=false; }
    if(h->alertWarn){ f.printf("%swarn", first?"":"|"); first=false; }
    if(h->alertRecovered){ f.printf("%srecovered", first?"":"|"); }
    f.print('\n');
  }
  Store::unlock();
  f.close();
  Display::unlock();
  // Verify the write actually landed on flash: re-open and read back the size.
  size_t sz=0; File rf=LittleFS.open(path, FILE_READ); if(rf){ sz=rf.size(); rf.close(); }
  snprintf(g_save,sizeof(g_save),"save:OK %uB", (unsigned)sz);
  return true;
}
