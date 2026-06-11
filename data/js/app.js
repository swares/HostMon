/* app.js — application shell: state, navigation, polling, overlays. */
(function(){
  const {h,clear}=UI;
  window.STATE={summary:{total:0,up:0,warn:0,down:0,paused:0,ack:0,attention:0,uptime30:0},
    hosts:[], alerts:[], settings:{defaults:{interval:[30,300,60,60,43200,300],fails:3,lcdHome:'A'},webhook:{when:[]},sd:{},device:{}},
    page:'dashboard', selectedId:null, overlay:null};

  const NAV=[['dashboard','◧','Dashboard'],['hosts','≣','Hosts'],['alerts','◔','Alerts'],['plugins','◇','Plugins'],['settings','⚙','Settings']];
  const TITLES={dashboard:['Dashboard','Fleet overview · live'],hosts:['Hosts','All monitored hosts'],
    alerts:['Alerts','Notifications & delivery'],plugins:['Plugins','Check types'],settings:['Settings','Device & notifications']};

  let root, overlayLayer, toastEl;

  function toast(msg, ok){
    if(!toastEl){ toastEl=h('div',{}); Object.assign(toastEl.style,{position:'fixed',bottom:'22px',left:'50%',transform:'translateX(-50%)',
      zIndex:'200',padding:'10px 16px',borderRadius:'10px',fontSize:'13px',boxShadow:'0 10px 34px rgba(0,0,0,.4)'}); document.body.appendChild(toastEl); }
    toastEl.textContent=msg;
    toastEl.style.background=ok?'#13312a':'#3a1414';
    toastEl.style.color=ok?'#34d399':'#f87171';
    toastEl.style.border='1px solid '+(ok?'#1f5145':'#5e2424');
    toastEl.style.opacity='1';
    clearTimeout(toast._t); toast._t=setTimeout(()=>{toastEl.style.opacity='0';},2600);
  }

  async function act(promise, okMsg){
    try{ const r=await promise; await refresh(); rerender(); if(okMsg) toast(okMsg,true); return r; }
    catch(e){ toast(e.message||'Action failed', false); }
  }

  const APP={
    go(p){ STATE.page=p; STATE.selectedId=null; rerender(); },
    openHost(id){ STATE.selectedId=id; APP.refreshHost(id); rerender(); },
    back(){ STATE.selectedId=null; rerender(); },
    rerender, refresh, toast,
    openModal(type,id){ APP.setOverlay(OVERLAYS.reasonModal(type,id)); },
    openHostForm(mode,id){ APP.setOverlay(OVERLAYS.hostForm(mode,id)); },
    openLcd(mode){ APP.setOverlay(OVERLAYS.lcdPreview(mode)); },
    openSetup(){ window.location.href='/setup.html'; },
    setOverlay(node){ STATE.overlay=node; clear(overlayLayer); if(node) overlayLayer.appendChild(node); },
    closeOverlay(){ STATE.overlay=null; clear(overlayLayer); },

    ack:(id,reason,who)=>act(API.ack(id,reason,who),'Acknowledged'),
    pause:(id,reason,until,who)=>act(API.pause(id,reason,until,who),'Checks paused'),
    resume:(id)=>act(API.resume(id),'Resumed'),
    clear:(id)=>act(API.clear(id),'Ack cleared'),
    setEvery:(id,key,every)=>act(API.setEvery(id,key,every),'Interval updated'),
    saveHost:(p)=>act(API.saveHost(p).then(()=>APP.closeOverlay()),'Host saved'),
    deleteHost:(id)=>act(API.deleteHost(id).then(()=>APP.closeOverlay()),'Host removed'),
    saveWebhook:(p)=>act(API.saveWebhook(p),'Webhook settings saved'),
    saveDefaults:(p)=>act(API.saveDefaults(p),'Defaults saved'),
    saveAuth:(u,p)=>act(API.saveAuth(u,p),'Web login updated'),
    testWebhook:()=>act(API.testWebhook(),'Test webhook sent'),
    reloadSD:()=>act(API.reloadSD(),'Reloaded hosts from flash'),

    async refreshHost(id){ try{ const hh=await API.host(id);
      const i=STATE.hosts.findIndex(x=>x.id===id); if(i>=0) STATE.hosts[i]=hh; else STATE.hosts.push(hh); }catch(e){} },
  };
  window.APP=APP;

  function sidebar(){
    const d=STATE.settings.device||{}, s=STATE.summary;
    const nav=h('nav',{cls:'nav'});
    NAV.forEach(([k,ic,lbl])=>{
      const a=h('a',{cls:(STATE.page===k&&!STATE.selectedId)?'on':'',onClick:()=>APP.go(k)},
        h('span',{cls:'i'},ic),lbl);
      if(k==='alerts') a.appendChild(h('span',{cls:'ct'},String((s.down||0)+(s.warn||0))));
      nav.appendChild(a);
    });
    nav.appendChild(h('div',{cls:'grp'},'Device'));
    const online=!d.ap;
    return h('aside',{cls:'side'},
      h('div',{cls:'brand'},h('div',{cls:'mk'},'◆'),
        h('div',{cls:'nm'},'Host Monitor',h('small',{},d.name||'hostmon-01'))),
      nav,
      h('div',{cls:'devcard'},
        h('div',{cls:'dr'},h('span',{},'Network'),h('b',{cls:online?'c-up':'c-warn'}, online?'● On LAN':'● AP mode')),
        h('div',{cls:'dr'},h('span',{},'Address'),h('b',{},d.ip||'—')),
        h('div',{cls:'dr'},h('span',{},'Host list'),h('b',{},'Flash · '+(s.total||0))),
        h('div',{cls:'dr'},h('span',{},'Webhook'),h('b',{cls:STATE.settings.webhook.enabled?'c-up':'c-mut'}, STATE.settings.webhook.enabled?'On':'Off')),
        h('button',{cls:'btn sm lcdbtn',style:{marginTop:'7px'},onClick:()=>APP.openSetup()},'⛭ Run setup wizard')));
  }

  function rerender(){
    if(!root) return;
    const active=document.activeElement;
    const typing = active && /^(INPUT|TEXTAREA|SELECT)$/.test(active.tagName);
    clear(root);
    root.appendChild(sidebar());
    const sel=STATE.selectedId?STATE.hosts.find(x=>x.id===STATE.selectedId):null;
    const [tt,ts]= sel?['Host detail',sel.addr]:(TITLES[STATE.page]||['','']);
    const top=h('div',{cls:'topbar'});
    if(sel) top.appendChild(h('button',{cls:'btn sm ghost',onClick:()=>APP.back()},'← Back'));
    top.appendChild(h('div',{},h('h1',{},tt),h('div',{cls:'sub'},ts)));
    top.appendChild(h('div',{cls:'right'},h('span',{cls:'live'},h('span',{cls:'pulse'}),'live · '+(STATE.summary.clock||'--:--'))));
    const scroll=h('div',{cls:'scroll'});
    let content;
    try{
      if(sel) content=PAGES.detail(sel);
      else content=PAGES[STATE.page]?PAGES[STATE.page]():h('div',{cls:'empty'},'…');
    }catch(e){ content=h('div',{cls:'card empty'},'Render error: '+e.message); console.error(e); }
    scroll.appendChild(content);
    root.appendChild(h('div',{cls:'main'},top,scroll));
    // keep overlay on top
    if(STATE.overlay && !overlayLayer.firstChild) overlayLayer.appendChild(STATE.overlay);
    void typing;
  }

  async function refresh(){
    try{
      const [sum,hs,al,st]=await Promise.all([API.summary(),API.hosts(),API.alerts(),API.settings()]);
      STATE.summary=sum; STATE.hosts=hs.hosts||[]; STATE.alerts=al.alerts||[]; STATE.settings=st;
    }catch(e){ /* keep last good state; device may be busy */ }
  }

  function autoPoll(){
    setInterval(async ()=>{
      await refresh();
      const active=document.activeElement;
      const busy = STATE.overlay || (active && /^(INPUT|TEXTAREA|SELECT)$/.test(active.tagName));
      if(!busy) rerender();
      else { // still refresh the live clock in the topbar
        const live=document.querySelector('.live'); if(live) live.lastChild.textContent='live · '+(STATE.summary.clock||'--:--');
      }
    }, 5000);
  }

  async function boot(){
    root=document.getElementById('app');
    overlayLayer=h('div',{}); document.body.appendChild(overlayLayer);
    root.innerHTML='<div class="side"></div><div class="main"><div class="scroll"><div class="empty">Loading device…</div></div></div>';
    await refresh(); rerender(); autoPoll();
  }
  document.addEventListener('DOMContentLoaded',boot);
})();
