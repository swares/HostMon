/* api.js — live device API client. No mock data: every view reads /api/*. */
(function(){
  async function jget(path){
    const r = await fetch(path, {cache:'no-store'});
    if(!r.ok) throw new Error('HTTP '+r.status);
    return r.json();
  }
  async function jpost(path, body){
    const r = await fetch(path, {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify(body||{})});
    let j={}; try{ j=await r.json(); }catch(e){}
    if(!r.ok || j.ok===false) throw new Error(j.error || ('HTTP '+r.status));
    return j;
  }
  window.API = {
    summary:  ()=>jget('/api/summary'),
    hosts:    ()=>jget('/api/hosts'),
    host:     (id)=>jget('/api/host?id='+encodeURIComponent(id)),
    alerts:   ()=>jget('/api/alerts'),
    settings: ()=>jget('/api/settings'),
    status:   ()=>jget('/api/status'),
    wifiScan: ()=>jget('/api/wifi/scan'),

    ack:    (id,reason,who)=>jpost('/api/host/ack',{id,reason,who}),
    pause:  (id,reason,until,who)=>jpost('/api/host/pause',{id,reason,until,who}),
    resume: (id)=>jpost('/api/host/resume',{id}),
    clear:  (id)=>jpost('/api/host/clear',{id}),
    setEvery:(id,key,every)=>jpost('/api/host/interval',{id,key,every}),
    saveHost:(p)=>jpost('/api/host',p),
    deleteHost:(id)=>jpost('/api/host/delete',{id}),

    saveWebhook:(p)=>jpost('/api/settings/webhook',p),
    saveDefaults:(p)=>jpost('/api/settings/defaults',p),
    saveAuth:(user,pass)=>jpost('/api/settings/auth',{user,pass}),
    testWebhook:()=>jpost('/api/test/webhook'),
    reloadSD:()=>jpost('/api/sd/reload'),
    wifiJoin:(ssid,pass)=>jpost('/api/wifi/join',{ssid,pass}),
  };
})();
