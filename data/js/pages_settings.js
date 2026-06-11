/* pages_settings.js — Alerts, Plugins and Settings pages (augments PAGES). */
(function(){
  const {h,dot,pill,fmtEvery,LBL,CHECK_NAME,CHECK_ICON}=UI;
  const A=()=>window.APP;
  const CHECK_KEYS=['ping','dns','port','http','ssl','trace'];

  // ---------- Alerts ----------
  let af='all';
  function alerts(){
    const S=STATE.summary, df=STATE.settings.defaults||{};
    const list=STATE.alerts.filter(a=> af==='all'?true: af==='unack'?(a.state==='firing'): a.sev===af);
    const kpi=(k,n,dotc,nc)=>h('div',{cls:'kpi'},h('div',{cls:'k'}, dotc?h('span',{cls:'dot '+dotc}):null,k),h('div',{cls:'n '+(nc||'')},n));
    const feed=h('div',{cls:'afeed'});
    if(!list.length) feed.appendChild(h('div',{cls:'card empty'},'No alerts match this filter.'));
    list.forEach(a=>{
      const host=STATE.hosts.find(x=>x.name===a.host);
      const aright=h('div',{cls:'aright'});
      const atop=h('div',{cls:'atop'});
      if(a.state==='ack') atop.appendChild(h('span',{cls:'pill ack'},'ack’d'));
      else if(a.state==='resolved') atop.appendChild(h('span',{cls:'tag'},'resolved'));
      else { if(host) atop.appendChild(h('button',{cls:'btn sm',onClick:()=>A().openModal('ack',host.id)},'Ack'));
             atop.appendChild(h('button',{cls:'btn sm ghost'},'Mute')); }
      aright.appendChild(atop);
      const chan=h('div',{cls:'achan',title:'Notified via'});
      (a.channels||[]).forEach(()=>chan.appendChild(h('span',{cls:'chan on'},'↗')));
      aright.appendChild(chan);
      feed.appendChild(h('div',{cls:'arow '+a.sev},
        h('div',{cls:'amain'},
          dot(a.sev),
          h('span',{cls:'pill '+a.sev,style:{minWidth:'92px',justifyContent:'center'}},a.label),
          h('div',{cls:'amid'},h('div',{cls:'ah'},
            h('span',{cls:'ahost',style:{cursor:host?'pointer':'default'},onClick:()=>host&&A().openHost(host.id)},a.host),
            h('span',{cls:'acheck'},'· '+a.check))),
          h('span',{cls:'when'},a.time),
          aright),
        h('div',{cls:'amsg'},a.msg)));
    });
    const left=h('div',{},
      h('div',{cls:'kgrid',style:{gridTemplateColumns:'1fr 1fr 1fr 1fr'}},
        kpi('Firing now',STATE.alerts.filter(a=>a.state==='firing').length,'c-down','c-down'),
        kpi('Warnings',STATE.alerts.filter(a=>a.sev==='warn'&&a.state==='firing').length,'c-warn','c-warn'),
        kpi('Sent today',STATE.alerts.filter(a=>a.channels&&a.channels.length).length,null,''),
        kpi('Ack’d',STATE.alerts.filter(a=>a.state==='ack').length,null,'c-ack')),
      h('div',{cls:'toolbar'},
        h('div',{cls:'seg'},[['all','All'],['unack','Un-ack’d'],['down','Down'],['warn','Warn']].map(([k,l])=>
          h('button',{cls:af===k?'on':'',onClick:()=>{af=k;A().rerender();}},l))),
        h('input',{cls:'search',placeholder:'⌕  Filter alerts…',disabled:'disabled'})),
      feed);

    // right rail
    const renotify = df.renotify!==false;
    const everyChips=['15m','30m','1h','2h','4h'];
    const curEvery = fmtEvery(df.renotifyEvery||1800);
    const delivery=h('div',{cls:'card'},h('h3',{},'Delivery'),
      h('div',{style:{display:'flex',flexDirection:'column',gap:'11px'}},
        h('div',{style:{display:'flex',alignItems:'center',gap:'10px'}},h('span',{style:{fontSize:'18px'}},'↗'),
          h('div',{style:{flex:'1'}},h('b',{style:{fontWeight:'600'}},'Webhook'),
            h('div',{cls:'c-mut',style:{fontSize:'11.5px',fontFamily:'var(--mono)'}},'JSON · '+(STATE.settings.webhook.ok?'✓ ':'')+STATE.settings.webhook.last)),
          STATE.settings.webhook.enabled?h('span',{cls:'pill up'},dot('up'),'On'):h('span',{cls:'tag'},'Off'))),
      h('button',{cls:'btn sm',style:{marginTop:'13px',width:'100%'},onClick:()=>A().go('settings')},'Configure notifications →'));
    const esc=h('div',{cls:'card',style:{flex:'1'}},h('h3',{},'Escalation'),
      h('div',{style:{fontSize:'12.5px',color:'var(--mut)',marginBottom:'14px',display:'flex',justifyContent:'space-between',alignItems:'center'}},
        h('span',{},'Alert after consecutive fails'),
        h('span',{cls:'chips'},[1,3,5].map(n=>h('span',{cls:'chip'+(df.fails===n?' on':''),onClick:()=>A().saveDefaults({fails:n})},String(n))))),
      h('div',{style:{borderTop:'1px solid var(--hair)',paddingTop:'13px',marginBottom:'13px'}},
        h('div',{style:{display:'flex',alignItems:'center',gap:'10px'}},
          h('button',{cls:'toggle'+(renotify?' on':''),onClick:()=>A().saveDefaults({renotify:!renotify})}),
          h('div',{style:{flex:'1'}},h('b',{style:{fontWeight:'600',fontSize:'13px'}},'Re-notify while firing'),
            h('div',{cls:'c-mut',style:{fontSize:'11px'}},'Repeat the alert until acknowledged or resolved'))),
        renotify?h('div',{style:{marginTop:'11px',marginLeft:'50px'}},
          h('div',{cls:'lbl',style:{marginBottom:'6px'}},'Every'),
          h('div',{cls:'chips'},everyChips.map(t=>h('span',{cls:'chip'+(curEvery===t?' on':''),
            onClick:()=>A().saveDefaults({renotifyEvery:parseEvery(t)})},t)))):null),
      h('div',{style:{borderTop:'1px solid var(--hair)',paddingTop:'13px'}},
        h('div',{cls:'lbl',style:{marginBottom:'8px'}},'Notify on'),
        h('div',{cls:'chips'},[['down','Down'],['recovered','Recovered'],['warn','Warning']].map(([k,l])=>{
          const on=(STATE.settings.webhook.when||[]).includes(k);
          return h('span',{cls:'chip'+(on?' on':''),onClick:()=>toggleNotifyOn(k)},l);})),
        h('div',{cls:'c-mut',style:{fontSize:'11px',marginTop:'9px'}},'Cert expiry warns at ',h('b',{style:{color:'var(--tx)'}},'14 days'))));
    return h('div',{},h('div',{cls:'dcols',style:{gridTemplateColumns:'1fr 320px'}},left,
      h('div',{style:{display:'flex',flexDirection:'column',gap:'16px'}},delivery,esc)));
  }
  function parseEvery(t){ if(t.endsWith('h')) return parseInt(t)*3600; return parseInt(t)*60; }
  function toggleNotifyOn(k){
    const cur=new Set(STATE.settings.webhook.when||[]);
    cur.has(k)?cur.delete(k):cur.add(k);
    A().saveWebhook({when:[...cur]});
  }

  // ---------- Plugins ----------
  function plugins(){
    const df=STATE.settings.defaults||{interval:[30,300,60,60,43200,300]};
    const card=h('div',{cls:'card',style:{maxWidth:'760px'}});
    const desc={ping:'ICMP reachability + packet loss %',dns:'Resolve the hostname to an IP (and time it)',
      port:'TCP connect to a port',http:'Expect 2xx/3xx from a URL',ssl:'Warn N days before cert expires',trace:'Path + per-hop latency (hop estimate)'};
    CHECK_KEYS.forEach((k,i)=> card.appendChild(h('div',{cls:'checkrow'},
      h('div',{cls:'ci',style:{color:'var(--teal)'}},CHECK_ICON[k]),
      h('div',{cls:'cmid'},h('div',{cls:'cn'},CHECK_NAME[k]),h('div',{cls:'cd',style:{fontFamily:'var(--ui)'}},desc[k])),
      h('div',{cls:'tag',style:{marginRight:'8px'}},'default every '+fmtEvery(df.interval[i])),
      h('button',{cls:'toggle on',title:'Enabled per-host on the Hosts page'}))));
    return h('div',{},
      h('div',{cls:'pagehead'},h('div',{},h('h2',{},'Check plugins'),
        h('div',{cls:'sub'},'Six built-in checks · default cadence applies unless a host overrides it · enable per host'))),
      card);
  }

  // ---------- Settings ----------
  function settings(){
    const s=STATE.settings, df=s.defaults, wh=s.webhook;
    const head=h('div',{cls:'pagehead'},h('div',{},h('h2',{},'Settings'),h('div',{cls:'sub'},'Device · host list · notifications · defaults')));

    // Default intervals (editable, click to cycle through allowed steps)
    const intvl=h('div',{cls:'card',style:{marginBottom:'18px'}},
      h('h3',{},'Default check intervals',h('span',{cls:'right'},'applied to every host unless overridden')));
    const grid=h('div',{cls:'intvl-grid'});
    CHECK_KEYS.forEach((k,i)=>{
      const opts=UI.INTERVAL_OPTS;
      const tile=h('div',{cls:'intvl',style:{cursor:'pointer'},title:'click to change'},
        h('div',{cls:'k'},CHECK_NAME[k]),h('div',{cls:'v c-teal'},fmtEvery(df.interval[i])));
      tile.addEventListener('click',()=>{ const cur=df.interval[i]; const idx=opts.indexOf(cur);
        const next=opts[(idx+1)%opts.length]; const arr=df.interval.slice(); arr[i]=next; A().saveDefaults({interval:arr}); });
      grid.appendChild(tile);
    });
    intvl.appendChild(grid);

    // Webhook card
    const whInputs={}; const whWhen=new Set(wh.when||[]); let whMethod=wh.method||'POST';
    const methodChips=h('div',{cls:'chips'},
      h('span',{cls:'chip'+(whMethod==='POST'?' on':''),onClick:e=>{whMethod='POST';mark(e);}},'POST'),
      h('span',{cls:'chip'+(whMethod==='PUT'?' on':''),onClick:e=>{whMethod='PUT';mark(e);}},'PUT'));
    function mark(e){ methodChips.querySelectorAll('.chip').forEach(c=>c.classList.remove('on')); e.target.classList.add('on'); }
    const whCard=h('div',{cls:'card'},h('h3',{},'↗ Webhook',h('span',{cls:'right'},'JSON POST')),
      field('Endpoint URL', whInputs.url=inp('mono',wh.url)),
      h('div',{cls:'row2'}, h('div',{cls:'field',style:{flex:'0 0 110px'}},h('label',{},'Method'),methodChips),
        field('Custom header', whInputs.header=inp('mono',wh.header),'1')),
      whenChips(['down','warn','recovered','ack','paused'],whWhen),
      h('div',{cls:'field'},h('label',{},'JSON payload · sent on each event'),
        h('pre',{cls:'payload',html:payloadPreview()})),
      h('div',{style:{display:'flex',gap:'9px',alignItems:'center'}},
        h('button',{cls:'btn pri sm',onClick:()=>A().saveWebhook({enabled:true,url:whInputs.url.value,method:whMethod,header:whInputs.header.value,when:[...whWhen]})},'Save'),
        h('button',{cls:'btn sm',onClick:()=>A().testWebhook()},'Send test ↗'),
        h('span',{cls:'c-mut',style:{marginLeft:'auto',fontSize:'11.5px'}}, (wh.ok?'✓ ':'')+wh.last)));

    // Host list (SD)
    const sdTbl=h('div',{cls:'tbl sd-tbl'},h('div',{cls:'thead'},h('div',{style:{flex:'1'}},'Name'),h('div',{style:{width:'120px'}},'Address'),h('div',{style:{width:'80px'}},'Group')));
    STATE.hosts.slice(0,5).forEach(hh=> sdTbl.appendChild(h('div',{cls:'trow'},
      h('div',{style:{flex:'1'},cls:'nm'},hh.name),h('div',{style:{width:'120px'},cls:'ip'},hh.addr),
      h('div',{style:{width:'80px'}},h('span',{cls:'tag'},hh.group)))));
    const sdCard=h('div',{cls:'card'},h('h3',{},'≣ Host list',h('span',{cls:'right'},'SD · '+(s.sd.file||'/hosts.csv'))),
      h('div',{cls:'c-mut',style:{fontSize:'12px',marginBottom:'12px'}},'Source of truth is the CSV on the SD card. Edits here write back.'),
      sdTbl,
      h('div',{style:{display:'flex',gap:'8px',marginTop:'12px'}},
        h('button',{cls:'btn sm pri',onClick:()=>A().openHostForm('add')},'+ Add host'),
        h('button',{cls:'btn sm',onClick:()=>A().reloadSD()},'Re-read SD'),
        h('span',{cls:'c-mut',style:{marginLeft:'auto',fontSize:'11.5px'}}, s.sd.count+' hosts · '+(s.sd.synced?'synced ✓':'no SD'))));

    // Device & defaults
    const devCard=h('div',{cls:'card'},h('h3',{},'⚙ Device & defaults'));
    [['Network',(s.device.ap?'● AP mode':'● On LAN — '+(s.device.ssid||'?'))],['IP address',s.device.ip],
     ['AP fallback','HostMon (captive)'],['Firmware','v'+s.device.fw]].forEach(([k,v])=>
      devCard.appendChild(h('div',{style:{display:'flex',justifyContent:'space-between',padding:'8px 0',borderBottom:'1px solid var(--hair)',fontSize:'13px'}},
        h('span',{cls:'c-mut'},k),h('b',{cls:'mono',style:{fontWeight:'500'}},v))));
    devCard.appendChild(h('div',{cls:'field',style:{marginTop:'14px'}},h('label',{},'Fails before alert'),
      h('div',{cls:'chips'},[1,3,5].map(n=>h('span',{cls:'chip'+(df.fails===n?' on':''),onClick:()=>A().saveDefaults({fails:n})},String(n))))));
    devCard.appendChild(h('div',{cls:'field',style:{marginBottom:'0'}},h('label',{},'LCD home screen'),
      h('div',{cls:'chips'},
        h('span',{cls:'chip'+(df.lcdHome==='A'?' on':''),onClick:()=>A().saveDefaults({lcdHome:'A'})},'A · Health'),
        h('span',{cls:'chip'+(df.lcdHome==='B'?' on':''),onClick:()=>A().saveDefaults({lcdHome:'B'})},'B · Grid'))));

    // Web access (HTTPS Basic Auth) card
    const userI=h('input',{cls:'input mono',value:(s.device&&s.device.user)||'admin'});
    const passI=h('input',{cls:'input mono',type:'password',placeholder:'new password (8-39 chars)'});
    const authCard=h('div',{cls:'card'},h('h3',{},'🔒 Web access',h('span',{cls:'right'},'HTTPS · Basic Auth')),
      h('div',{cls:'c-mut',style:{fontSize:'12px',marginBottom:'12px'}},'Credentials for the dashboard login prompt. The connection is served over HTTPS.'),
      field('Username',userI),
      h('div',{cls:'field'},h('label',{},'New password'),passI),
      h('div',{style:{display:'flex',gap:'9px',alignItems:'center'}},
        h('button',{cls:'btn pri sm',onClick:()=>A().saveAuth(userI.value, passI.value)},'Update login'),
        h('span',{cls:'c-mut',style:{marginLeft:'auto',fontSize:'11.5px'}},'leave password blank to keep current')));

    return h('div',{},head,intvl,h('div',{cls:'sgrid'},whCard,sdCard,devCard,authCard));

    function field(label,input,flex){ return h('div',{cls:'field',style:flex?{flex:flex}:null},h('label',{},label),input); }
    function inp(cls,val){ return h('input',{cls:'input '+(cls||''),value:val==null?'':val}); }
    function whenChips(list,set){ return h('div',{cls:'field'},h('label',{},'Send when'),
      h('div',{cls:'chips'},list.map(w=>{ const c=h('span',{cls:'chip'+(set.has(w)?' on':'')},w);
        c.addEventListener('click',()=>{ set.has(w)?set.delete(w):set.add(w); c.classList.toggle('on'); }); return c; }))); }
    function payloadPreview(){
      const ex=STATE.hosts.find(x=>x.status==='down')||STATE.hosts[0]||{name:'host',addr:'0.0.0.0'};
      return '{\n  <span class="k">"event"</span>:   <span class="s">"host.down"</span>,\n'+
        '  <span class="k">"host"</span>:    <span class="s">"'+ex.name+'"</span>,\n'+
        '  <span class="k">"address"</span>: <span class="s">"'+ex.addr+'"</span>,\n'+
        '  <span class="k">"status"</span>:  <span class="s">"DOWN"</span>,\n'+
        '  <span class="k">"check"</span>:   <span class="s">"ping,port"</span>,\n'+
        '  <span class="k">"fails"</span>:   <span class="num">3</span>,\n'+
        '  <span class="k">"device"</span>:  <span class="s">"hostmon-01"</span>\n}';
    }
  }

  Object.assign(window.PAGES,{alerts,plugins,settings});
})();
