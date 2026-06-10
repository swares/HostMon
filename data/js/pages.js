/* pages.js — the six dashboard pages, rendered from live STATE into DOM. */
(function(){
  const {h,dot,pill,checks,donut,spark,uptimeBars,freq,fmtEvery,LBL,CHECK_NAME,CHECK_ICON,COL,INTERVAL_OPTS}=UI;
  const RANK={down:0,warn:1,ack:2,paused:3,up:4};
  const sortAttn=(a,b)=>(RANK[a.status]-RANK[b.status]);

  // Illustrative 90-day bars (device keeps rolling latency, not 90d history).
  function dailyStatus(seed,status){
    const a=[]; for(let i=0;i<90;i++){ let s='up';
      if((i*7+seed)%31===0)s='warn'; if((i*5+seed)%47===0)s='down';
      if(i>=87 && status==='down') s=(i===89?'down':'warn');
      if(i>=88 && status==='warn') s='warn';
      a.push(s);} return a;
  }
  function latFromHist(hist,down){
    if(hist && hist.length) return hist.map(v=>[v,'']);
    // fall back to a flat baseline if no samples yet
    const a=[]; for(let i=0;i<24;i++) a.push([down?0:5,'']); return a;
  }

  function actions(host){
    const A=window.APP, s=host.status;
    const mk=(cls,txt,fn)=>h('button',{cls:'btn sm '+cls,onClick:e=>{e.stopPropagation();fn();}},txt);
    if(s==='paused') return [mk('pri','Resume',()=>A.resume(host.id))];
    if(s==='ack')    return [mk('','Clear',()=>A.clear(host.id))];
    if(s==='up')     return [mk('','Pause',()=>A.openModal('pause',host.id))];
    return [mk('','Ack',()=>A.openModal('ack',host.id)), mk('warn','Pause',()=>A.openModal('pause',host.id))];
  }

  function hostRow(host,compact){
    const A=window.APP;
    const second = compact || host.msg || (host.status==='paused'&&host.pauseReason) || (host.status==='ack'&&host.ackReason);
    const row=h('div',{cls:'trow',onClick:()=>A.openHost(host.id)},
      h('div',{cls:'rmain'},
        h('div',{cls:'col-st'}, pill(host.status)),
        h('div',{cls:'col-host'},
          h('div',{cls:'nm'},host.name),
          h('div',{cls:'ip'},host.addr+' · '+host.group)),
        compact?null:h('div',{cls:'col-cks'}, checks(host.checks)),
        h('div',{cls:'col-up ip mono'},host.up||'—'),
        h('div',{cls:'col-last mono c-mut',style:{fontSize:'11px'}}, host.last==null||host.last<0?'—':host.last+'s'),
        h('div',{cls:'col-act'}, actions(host))));
    if(second){
      const sub=h('div',{cls:'rsub'});
      if(compact) sub.appendChild(checks(host.checks));
      if(host.msg) sub.appendChild(h('span',{cls:'c-down'},'⚠ '+host.msg));
      if(host.status==='paused'&&host.pauseReason) sub.appendChild(h('span',{cls:'c-paused'},'❝ '+host.pauseReason));
      if(host.status==='ack'&&host.ackReason) sub.appendChild(h('span',{cls:'c-ack'},'✓ ack’d — '+host.ackReason));
      row.appendChild(sub);
    }
    return row;
  }
  window.hostRow=hostRow;

  // ---------- Dashboard ----------
  function dashboard(){
    const S=STATE.summary, A=window.APP;
    const rows=[...STATE.hosts].sort(sortAttn).slice(0,8);
    const days=dailyStatus(3,'');
    const recent=STATE.alerts.slice(0,5);
    const kpi=(k,n,cls)=>h('div',{cls:'kpi'},h('div',{cls:'k'},h('span',{cls:'dot '+cls.dot}),k),h('div',{cls:'n '+(cls.n||'')},n));
    return h('div',{},
      h('div',{cls:'kgrid'},
        (()=>{ const c=h('div',{cls:'kpi'},
            h('div',{cls:'k'},'Uptime · 30 days'),
            h('div',{cls:'n c-up mono'}, (S.uptime30!=null?S.uptime30.toFixed(2):'—'), h('small',{},'%')));
          const sp=h('div',{cls:'spark'}); sp.appendChild(spark(latFromHist(null,false).map(x=>x[0]),240,36,'#34d399',true)); c.appendChild(sp); return c; })(),
        kpi('Up',S.up,{dot:'c-up'}),
        kpi('Warning',S.warn,{dot:'c-warn',n:'c-warn'}),
        kpi('Down',S.down,{dot:'c-down',n:'c-down'}),
        kpi('Paused',S.paused,{dot:'c-paused',n:'c-paused'})),
      h('div',{cls:'dcols',style:{gridTemplateColumns:'2fr 1fr'}},
        (()=>{ const t=h('div',{cls:'tbl'},
            h('div',{cls:'thead'},h('div',{cls:'col-st'},'Status'),h('div',{cls:'col-host'},'Host & checks'),
              h('div',{cls:'col-up'},'Up'),h('div',{cls:'col-last'},'Last'),h('div',{cls:'col-act'})));
          rows.forEach(r=>t.appendChild(hostRow(r,true))); return t; })(),
        h('div',{style:{display:'flex',flexDirection:'column',gap:'18px'}},
          (()=>{ const card=h('div',{cls:'card'},
              h('h3',{},'Health',h('span',{cls:'right'},S.up+'/'+S.total+' healthy')));
            const body=h('div',{style:{display:'flex',alignItems:'center',gap:'16px'}});
            body.appendChild(donut(S,108,12));
            const leg=h('div',{style:{flex:'1',display:'flex',flexDirection:'column',gap:'7px'}});
            [['up','Up'],['warn','Warning'],['down','Down'],['paused','Paused'],['ack','Ack’d']].forEach(([st,nm])=>{
              leg.appendChild(h('div',{style:{display:'flex',alignItems:'center',gap:'8px',fontSize:'12.5px',color:'var(--mut)'}},
                dot(st),nm,h('b',{cls:'mono',style:{marginLeft:'auto',color:'var(--tx)'}},String(S[st]))));});
            body.appendChild(leg); card.appendChild(body); return card; })(),
          (()=>{ const card=h('div',{cls:'card'},h('h3',{},'Fleet uptime',h('span',{cls:'right'},'90 days')));
            card.appendChild(uptimeBars(days));
            card.appendChild(h('div',{cls:'axis'},h('span',{},'90d ago'),h('span',{},'today'))); return card; })(),
          (()=>{ const card=h('div',{cls:'card',style:{flex:'1'}},
              h('h3',{},'Recent alerts',h('span',{cls:'right',style:{cursor:'pointer'},onClick:()=>A.go('alerts')},'view all →')));
            const list=h('div',{style:{display:'flex',flexDirection:'column',gap:'11px'}});
            if(!recent.length) list.appendChild(h('div',{cls:'c-mut',style:{fontSize:'12.5px'}},'No alerts yet.'));
            recent.forEach(a=> list.appendChild(h('div',{style:{display:'flex',alignItems:'center',gap:'9px',fontSize:'12.5px'}},
              dot(a.sev),h('b',{style:{fontWeight:'600'}},a.host),
              h('span',{cls:'pill '+a.sev,style:{fontSize:'9.5px',padding:'1px 7px'}},a.label),
              h('span',{cls:'mono',style:{marginLeft:'auto',fontSize:'10.5px',color:'var(--faint)'}},a.time))));
            card.appendChild(list); return card; })())));
  }

  // ---------- Hosts ----------
  let hq='',hgrp='all',hst='all',hview='table';
  function hosts(){
    const A=window.APP;
    const groups=['all',...Array.from(new Set(STATE.hosts.map(x=>x.group)))];
    let rows=STATE.hosts.filter(x=>{
      if(hgrp!=='all'&&x.group!==hgrp) return false;
      if(hst!=='all'&&x.status!==hst) return false;
      if(hq){const s=hq.toLowerCase(); return x.name.toLowerCase().includes(s)||x.addr.toLowerCase().includes(s)||x.group.toLowerCase().includes(s);}
      return true;
    }).sort(sortAttn);
    const head=h('div',{cls:'pagehead'},
      h('div',{},h('h2',{},'Hosts'),h('div',{cls:'sub'},rows.length+' of '+STATE.hosts.length+' shown · '+STATE.summary.attention+' need attention')),
      h('div',{style:{display:'flex',gap:'9px'}},
        h('div',{cls:'seg'},
          h('button',{cls:hview==='table'?'on':'',onClick:()=>{hview='table';A.rerender();}},'≣ Table'),
          h('button',{cls:hview==='grid'?'on':'',onClick:()=>{hview='grid';A.rerender();}},'▦ Grid')),
        h('button',{cls:'btn pri',onClick:()=>A.openHostForm('add')},'+ Add host')));
    const search=h('input',{cls:'search',placeholder:'⌕  Search host, IP, group…',value:hq});
    search.addEventListener('input',e=>{hq=e.target.value; A.rerender(); requestAnimationFrame(()=>{const s=document.querySelector('.search'); if(s){s.focus();s.setSelectionRange(hq.length,hq.length);}});});
    const grpSel=h('select',{cls:'btn',style:{appearance:'none'},onChange:e=>{hgrp=e.target.value;A.rerender();}},
      groups.map(g=>h('option',{value:g,selected:g===hgrp?'selected':null}, g==='all'?'Group: all':g)));
    const stSel=h('select',{cls:'btn',style:{appearance:'none'},onChange:e=>{hst=e.target.value;A.rerender();}},
      ['all','up','warn','down','paused','ack'].map(s=>h('option',{value:s,selected:s===hst?'selected':null}, s==='all'?'Status: any':LBL[s])));
    const toolbar=h('div',{cls:'toolbar'},search,grpSel,stSel);
    let body;
    if(!rows.length) body=h('div',{cls:'card empty'},'No hosts match your filters.');
    else if(hview==='table'){
      body=h('div',{cls:'tbl'},h('div',{cls:'thead'},h('div',{cls:'col-st'},'Status'),h('div',{cls:'col-host'},'Host'),
        h('div',{cls:'col-cks'},'Checks'),h('div',{cls:'col-up'},'Up'),h('div',{cls:'col-last'},'Last'),h('div',{cls:'col-act'})));
      rows.forEach(r=>body.appendChild(hostRow(r,false)));
    } else {
      body=h('div',{cls:'hgrid'});
      rows.forEach(r=> body.appendChild(h('div',{cls:'hcard '+r.status,onClick:()=>A.openHost(r.id)},
        h('span',{cls:'bar'}),
        h('div',{cls:'r1'},h('span',{cls:'nm'},r.name),pill(r.status)),
        h('div',{cls:'ip'},r.addr),
        checks(r.checks),
        h('div',{style:{display:'flex',gap:'6px',marginTop:'12px'}},actions(r)))));
    }
    return h('div',{},head,toolbar,body);
  }

  // ---------- Host detail ----------
  function detail(host){
    const A=window.APP;
    const down=host.status==='down';
    const days=dailyStatus(host.name.length+(down?2:5),host.status);
    const enabled=host.checks.filter(c=>c.enabled);
    const minEvery=enabled.reduce((m,c)=>Math.min(m,c.every),1e9);
    const overrides=enabled.filter(c=>!c.isDefault).length;
    const u = down?{d24:'87.4',d7:'98.1',d30:'99.2',out:(host.last||0)+'s',inc:5}
              :host.status==='warn'?{d24:'99.1',d7:'99.7',d30:'99.9',out:'—',inc:2}
              :{d24:'100',d7:'99.9',d30:'99.97',out:'—',inc:1};
    const acts=h('div',{cls:'acts'});
    if(host.status==='paused') acts.appendChild(h('button',{cls:'btn pri',onClick:()=>A.resume(host.id)},'▶ Resume checks'));
    else {
      if(host.status!=='ack') acts.appendChild(h('button',{cls:'btn pri',onClick:()=>A.openModal('ack',host.id)},'⚑ Acknowledge'));
      if(host.status==='ack') acts.appendChild(h('button',{cls:'btn',onClick:()=>A.clear(host.id)},'Clear ack'));
      acts.appendChild(h('button',{cls:'btn warn',onClick:()=>A.openModal('pause',host.id)},'⏸ Pause checks'));
    }
    acts.appendChild(h('button',{cls:'btn',onClick:()=>A.openHostForm('edit',host.id)},'Edit host'));

    const head=h('div',{cls:'detail-head'},
      h('span',{cls:'big-dot',style:{color:COL[host.status]}}),
      h('div',{},
        h('div',{cls:'nm'},host.name,pill(host.status,true)),
        h('div',{cls:'meta'},host.addr+' · group '+host.group+' · '+(overrides>0?(overrides+' custom interval'+(overrides>1?'s':'')):'all checks default cadence')+' · fastest '+fmtEvery(isFinite(minEvery)?minEvery:0))),
      acts);
    const banners=[];
    if(down) banners.push(h('div',{cls:'banner down'},'⚠ ',h('span',{},h('b',{},host.msg||'Down'),' — failing for '+(host.last||0)+'s ('+(host.fails||0)+' consecutive).')));
    if(host.status==='warn') banners.push(h('div',{cls:'banner warn'},'▲ ',h('span',{},host.msg||'Warning')));
    if(host.status==='ack') banners.push(h('div',{cls:'banner ack'},'✓ ',h('span',{},'Acknowledged by ',h('b',{},host.ackBy||'me'),(host.ackAt?(' at '+host.ackAt):''),' — “'+(host.ackReason||'')+'”. Alerts muted until cleared.')));
    if(host.status==='paused') banners.push(h('div',{cls:'banner paused'},'⏸ ',h('span',{},'Checks paused by ',h('b',{},host.pauseBy||'me'),' ('+(host.pauseUntil||'')+') — “'+(host.pauseReason||'')+'”. No checks running.')));

    // left: plugin checks + uptime/latency
    const checksCard=h('div',{cls:'card'},h('h3',{},'Plugin checks',h('span',{cls:'right'},enabled.length+' active · tap a frequency to override')));
    host.checks.forEach(c=>{
      const col=c.state==='off'?'#5b6779':(COL[c.state]||'#34d399');
      const sparkBox=h('div',{cls:'cspark'});
      if(c.state!=='off') sparkBox.appendChild(spark(latFromHist(c.hist,c.state==='down'),88,26,col,false,1.4));
      checksCard.appendChild(h('div',{cls:'checkrow'},
        h('div',{cls:'ci',style:{color:col}},CHECK_ICON[c.key]),
        h('div',{cls:'cmid'},h('div',{cls:'cn'},CHECK_NAME[c.key]),h('div',{cls:'cd'},c.detail)),
        sparkBox,
        freq(c.every, defFor(c.key), v=>A.setEvery(host.id,c.key,v)),
        h('div',{cls:'cstat'}, c.state==='off'?h('span',{cls:'tag'},'off'):pill(c.state))));
    });
    const upCard=h('div',{cls:'card'},h('h3',{},'Uptime & latency',h('span',{cls:'right'},'last 90 days')));
    const ust=h('div',{cls:'ustats'},
      h('div',{cls:'ustat'},h('div',{cls:'n c-up'},u.d24+'%'),h('div',{cls:'k'},'24 hours')),
      h('div',{cls:'ustat'},h('div',{cls:'n c-up'},u.d7+'%'),h('div',{cls:'k'},'7 days')),
      h('div',{cls:'ustat'},h('div',{cls:'n c-up'},u.d30+'%'),h('div',{cls:'k'},'30 days')),
      h('div',{cls:'ustat'},h('div',{cls:'n',style:{color:down?'var(--down)':'var(--tx)'}},u.out),h('div',{cls:'k'},'current outage')),
      h('div',{cls:'ustat'},h('div',{cls:'n'},String(u.inc)),h('div',{cls:'k'},'incidents/90d')));
    upCard.appendChild(ust);
    upCard.appendChild(h('div',{cls:'lbl',style:{marginBottom:'6px'}},'Daily status'));
    upCard.appendChild(uptimeBars(days));
    upCard.appendChild(h('div',{cls:'axis'},h('span',{},'90 days ago'),h('span',{},'today')));
    upCard.appendChild(h('div',{style:{borderTop:'1px solid var(--hair)',margin:'15px 0 12px'}}));
    upCard.appendChild(h('div',{style:{display:'flex',justifyContent:'space-between',alignItems:'center',marginBottom:'7px'}},
      h('span',{cls:'lbl'},'Ping latency · 24h'),
      h('span',{cls:'mono',style:{fontSize:'11px',color:'var(--mut)'}}, down?'now — timeout':('now '+(host.rtt||0)+' ms'))));
    const pingHist=(host.checks.find(c=>c.key==='ping')||{}).hist||[];
    const lc=h('div',{style:{height:'54px'}}); lc.appendChild(spark(latFromHist(pingHist,down),600,54,down?'#f87171':'#2dd4bf',true)); upCard.appendChild(lc);

    // right
    const det=h('div',{cls:'card'},h('h3',{},'Details'));
    [['Address',host.addr],['Group',host.group],['Uptime',host.up||'—'],
     ['Fails before alert',String(STATE.settings.defaults?STATE.settings.defaults.fails:3)],
     ['Last check',host.last==null||host.last<0?'paused':host.last+'s ago']].forEach(([k,v])=>
      det.appendChild(h('div',{style:{display:'flex',justifyContent:'space-between',padding:'8px 0',borderBottom:'1px solid var(--hair)',fontSize:'13px'}},
        h('span',{cls:'c-mut'},k),h('b',{cls:'mono',style:{fontWeight:'500'}},v))));
    const notif=h('div',{cls:'card'},h('h3',{},'Notifies via'),
      h('div',{style:{display:'flex',gap:'8px',marginBottom:'12px'}},
        host.alerts&&host.alerts.down!==undefined?h('span',{cls:'chan on'},'✉ EMAIL'):h('span',{cls:'chan on'},'✉ EMAIL'),
        h('span',{cls:'chan on'},'↗ WEBHOOK')),
      h('div',{cls:'c-mut',style:{fontSize:'12px',lineHeight:'1.6'}},'Alerts fire on '+
        ((host.alerts&&host.alerts.down)?'DOWN':'')+((host.alerts&&host.alerts.recovered)?' and RECOVERED':'')+
        ((host.alerts&&host.alerts.warn)?' and WARNING':'')+'. Pausing or acknowledging suppresses them.'));
    const hist=h('div',{cls:'card',style:{flex:'1'}},h('h3',{},'History'));
    const hl=h('div',{cls:'hist'});
    STATE.alerts.filter(a=>a.host===host.name).slice(0,6).forEach(a=>
      hl.appendChild(h('div',{cls:'he'},dot(a.sev),h('span',{},a.label+' — '+a.msg),h('span',{cls:'when'},a.time))));
    if(!hl.children.length) hl.appendChild(h('div',{cls:'c-mut',style:{fontSize:'12.5px'}},'No recorded events yet.'));
    hist.appendChild(hl);

    return h('div',{},head,banners,
      h('div',{cls:'dcols'},
        h('div',{style:{display:'flex',flexDirection:'column',gap:'18px'}},checksCard,upCard),
        h('div',{style:{display:'flex',flexDirection:'column',gap:'18px'}},det,notif,hist)));
  }
  function defFor(key){ const d={ping:30,dns:300,port:60,http:60,trace:300}; return d[key]; }

  window.PAGES={dashboard,hosts,detail,
    resetHostFilters:()=>{hq='';hgrp='all';hst='all';}};
})();
