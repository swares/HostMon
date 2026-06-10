/* overlays.js — Ack/Pause modal, Add/Edit host modal, LCD preview. */
(function(){
  const {h,dot,pill,checks,donut,fmtEvery,freq,CHECK_NAME,CHECK_ABBR,CHECK_ICON,INTERVAL_OPTS}=UI;
  const A=()=>window.APP;
  const DEF={ping:30,dns:300,port:60,http:60,trace:300};
  const CHECK_KEYS=['ping','dns','port','http','trace'];

  function scrim(child,onClose){
    const s=h('div',{cls:'scrim',onClick:onClose}); s.appendChild(child); return s;
  }

  // ---- Ack / Pause modal ----
  function reasonModal(type,hostId){
    const host=STATE.hosts.find(x=>x.id===hostId)||{name:'?'};
    const isPause=type==='pause';
    let reason='', dur='Until I resume';
    const ta=h('textarea',{cls:'input',autofocus:'autofocus',
      placeholder:isPause?'e.g. Replacing PSU — back Saturday, don’t page me':'e.g. Known issue, RMA in progress'});
    const who=h('input',{cls:'input',value:'me (admin)'});
    const submit=h('button',{cls:'btn pri',disabled:'disabled',style:{flex:'1',justifyContent:'center'}},isPause?'Pause checks':'Acknowledge');
    ta.addEventListener('input',()=>{ reason=ta.value; if(reason.trim()) submit.removeAttribute('disabled'); else submit.setAttribute('disabled','disabled'); });
    submit.addEventListener('click',()=>{ if(!reason.trim()) return;
      if(isPause) A().pause(hostId,reason.trim(),dur,who.value); else A().ack(hostId,reason.trim(),who.value); A().closeOverlay(); });
    const durChips=h('div',{cls:'chips'},['1h','4h','24h','Until I resume','Custom…'].map(d=>{
      const c=h('span',{cls:'chip'+(dur===d?' on':'')},d);
      c.addEventListener('click',()=>{ dur=d; durChips.querySelectorAll('.chip').forEach(x=>x.classList.remove('on')); c.classList.add('on'); }); return c; }));
    const modal=h('div',{cls:'modal',onClick:e=>e.stopPropagation()},
      h('h3',{},(isPause?'⏸ Pause checks':'⚑ Acknowledge')+' · '+host.name),
      h('div',{cls:'msub'}, isPause?'Stops all checks & alerts for this host. Logged with your note.'
                                   :'Silences alerts for this failure while you work on it. Logged with your note.'),
      h('div',{cls:'field'},h('label',{},isPause?'Why are you pausing? (required)':'Reason (required)'),ta),
      isPause?h('div',{cls:'field'},h('label',{},'Duration'),durChips):null,
      h('div',{cls:'field',style:{marginBottom:'0'}},h('label',{},'Who'),who),
      h('div',{cls:'mfoot'},submit,h('button',{cls:'btn',onClick:()=>A().closeOverlay()},'Cancel')));
    setTimeout(()=>ta.focus(),30);
    return scrim(modal,()=>A().closeOverlay());
  }

  // ---- Add / Edit host modal ----
  function hostForm(mode,hostId){
    const editing=mode==='edit';
    const host=hostId?STATE.hosts.find(x=>x.id===hostId):null;
    const groups=['Apps','Compute','Storage','Network','IoT','External','Power'];
    const st={ name:host?host.name:'', addr:host?host.addr:'', group:host?host.group:'Apps',
      checks:CHECK_KEYS.map(k=>{ const ex=host&&host.checks.find(c=>c.key===k);
        return {key:k, enabled:ex?ex.enabled:(k==='ping'), every:ex?ex.every:DEF[k], port:ex?ex.port:0, secure:(ex&&ex.secure!==undefined)?ex.secure:true}; }),
      alerts:{down:host&&host.alerts?host.alerts.down:true, warn:host&&host.alerts?host.alerts.warn:false, recovered:host&&host.alerts?host.alerts.recovered:true} };

    const nameI=h('input',{cls:'input',autofocus:'autofocus',value:st.name,placeholder:'e.g. grafana'});
    const addrI=h('input',{cls:'input mono',value:st.addr,placeholder:'192.168.1.x or host.local'});
    const grpSel=h('select',{cls:'input'},groups.map(g=>h('option',{selected:g===st.group?'selected':null},g)));
    const submit=h('button',{cls:'btn pri',style:{flex:'1',justifyContent:'center'}},editing?'Save changes':'Add host');
    function validate(){ if(nameI.value.trim()&&addrI.value.trim()) submit.removeAttribute('disabled'); else submit.setAttribute('disabled','disabled'); }
    nameI.addEventListener('input',validate); addrI.addEventListener('input',validate);

    const fchecks=h('div',{cls:'fchecks'});
    st.checks.forEach(c=>{
      const portI = (c.key==='port'||c.key==='http')
        ? h('input',{cls:'input mono',style:{width:'72px',padding:'5px 8px'},placeholder:'port',value:c.port||'',
            oninput:e=>{ c.port=parseInt(e.target.value)||0; }}) : null;
      const schemeI = (c.key==='http')
        ? h('select',{cls:'input mono',style:{width:'80px',padding:'5px 6px'}},
            h('option',{selected:c.secure?'selected':null},'HTTPS'),
            h('option',{selected:c.secure?null:'selected'},'HTTP')) : null;
      if(schemeI) schemeI.addEventListener('change',()=>{ c.secure = schemeI.value==='HTTPS'; });
      const freqEl = freq(c.every, DEF[c.key], v=>{ c.every=v; });
      freqEl.style.display = c.enabled ? '' : 'none';
      if(portI) portI.style.display = c.enabled ? '' : 'none';
      if(schemeI) schemeI.style.display = c.enabled ? '' : 'none';
      const toggle=h('button',{cls:'toggle'+(c.enabled?' on':'')});
      const row=h('div',{cls:'fcheck'+(c.enabled?'':' off')},
        h('div',{cls:'fci'},CHECK_ICON[c.key]),
        h('div',{cls:'fcn'},h('b',{},CHECK_NAME[c.key]),
          h('div',{cls:'fcd'},CHECK_ABBR[c.key]+' · default '+fmtEvery(DEF[c.key]))),
        schemeI, portI, freqEl, toggle);
      toggle.addEventListener('click',()=>{
        c.enabled=!c.enabled;
        toggle.classList.toggle('on', c.enabled);
        row.classList.toggle('off', !c.enabled);
        freqEl.style.display = c.enabled ? '' : 'none';
        if(portI) portI.style.display = c.enabled ? '' : 'none';
        if(schemeI) schemeI.style.display = c.enabled ? '' : 'none';
      });
      fchecks.appendChild(row);
    });

    submit.addEventListener('click',()=>{
      const payload={ id:host?host.id:undefined, name:nameI.value.trim(), addr:addrI.value.trim(), group:grpSel.value,
        checks: st.checks.map(c=>({key:c.key,enabled:c.enabled,every:c.every,port:c.port||0,secure:!!c.secure})), alerts:st.alerts };
      A().saveHost(payload);
    });
    validate();

    const alertChips=h('div',{cls:'chips'},
      chip('Down','down'),chip('Recovered','recovered'),chip('Warning','warn'));
    function chip(label,key){ const c=h('span',{cls:'chip'+(st.alerts[key]?' on':'')},label);
      c.addEventListener('click',()=>{ st.alerts[key]=!st.alerts[key]; c.classList.toggle('on'); }); return c; }

    const modal=h('div',{cls:'modal wide',onClick:e=>e.stopPropagation()},
      h('h3',{},editing?'✎ Edit host':'＋ Add host', editing?h('span',{cls:'c-mut',style:{fontWeight:'400',fontSize:'14px'}},' · '+host.name):null),
      h('div',{cls:'msub'}, editing?'Changes are saved to the device flash.':'Adds a new entry saved to the device flash.'),
      h('div',{cls:'formgrid'},
        h('div',{cls:'field'},h('label',{},'Name'),nameI),
        h('div',{cls:'field'},h('label',{},'Group'),grpSel)),
      h('div',{cls:'field'},h('label',{},'Address — IP or hostname'),addrI),
      h('div',{cls:'field',style:{marginBottom:'6px'}},h('label',{},'Checks & frequency'),fchecks),
      h('div',{cls:'field',style:{marginBottom:'0'}},h('label',{},'Alert on'),alertChips),
      h('div',{cls:'mfoot'},submit,
        editing?h('button',{cls:'btn',style:{color:'#f87171',borderColor:'#f87171'},
          onClick:()=>{ if(confirm('Delete host "'+host.name+'"?')) A().deleteHost(host.id); }},'Delete'):null,
        h('button',{cls:'btn',onClick:()=>A().closeOverlay()},'Cancel')));
    setTimeout(()=>nameI.focus(),30);
    return scrim(modal,()=>A().closeOverlay());
  }

  // ---- LCD preview ----
  let lcdMode='A';
  function lcdPreview(mode){
    lcdMode=mode||STATE.settings.defaults.lcdHome||'A';
    const screen=h('div',{cls:'lcd-screen'});
    screen.appendChild(lcdMode==='A'?lcdHealth():lcdGrid());
    const stage=h('div',{cls:'lcd-stage'},
      h('button',{cls:'btn lcd-close',onClick:()=>A().closeOverlay()},'✕ Close preview'),
      h('div',{cls:'lcd-ctrls'},h('span',{cls:'lbl'},'On-device · 800×480 touch'),
        h('div',{cls:'seg'},
          h('button',{cls:lcdMode==='A'?'on':'',onClick:()=>A().setOverlay(lcdPreview('A'))},'A · Health'),
          h('button',{cls:lcdMode==='B'?'on':'',onClick:()=>A().setOverlay(lcdPreview('B'))},'B · Grid'))),
      h('div',{cls:'lcd-frame'},screen),
      h('div',{cls:'lbl',style:{color:'var(--faint)'}},'Live data — Ack / Pause here updates the dashboard too'));
    return stage;
  }
  function lcdNav(active){
    return h('div',{style:{display:'flex',gap:'7px',padding:'0 18px 16px'}},
      [['Home','◧'],['Hosts','≣'],['Alerts','◔'],['Setup','⚙']].map(([n,i])=>
        h('div',{style:{flex:'1',textAlign:'center',padding:'11px 0',fontSize:'13px',fontWeight:'600',borderRadius:'12px',
          display:'flex',alignItems:'center',justifyContent:'center',gap:'7px',
          background:n===active?'linear-gradient(180deg,var(--teal),var(--teal2))':'var(--panel)',
          color:n===active?'#04231b':'var(--mut)',border:'1px solid '+(n===active?'transparent':'var(--line)')}},
          h('span',{},i),n)));
  }
  function lcdHealth(){
    const S=STATE.summary;
    const att=[...STATE.hosts].sort((a,b)=>({down:0,warn:1,ack:2,paused:3,up:4})[a.status]-({down:0,warn:1,ack:2,paused:3,up:4})[b.status])
      .filter(x=>x.status==='down'||x.status==='warn').slice(0,3);
    const left=h('div',{style:{width:'280px',flex:'none',display:'flex',flexDirection:'column',gap:'14px'}},
      (()=>{ const c=h('div',{cls:'card',style:{padding:'16px'}});
        const row=h('div',{style:{display:'flex',alignItems:'center',gap:'16px'}}); row.appendChild(donut(S,116,12));
        const leg=h('div',{style:{flex:'1',display:'flex',flexDirection:'column',gap:'8px'}});
        [['up','Up'],['warn','Warning'],['down','Down'],['paused','Paused'],['ack','Ack’d']].forEach(([st,nm])=>
          leg.appendChild(h('div',{style:{display:'flex',alignItems:'center',gap:'8px',fontSize:'12px',color:'var(--mut)'}},
            dot(st),nm,h('b',{cls:'mono',style:{marginLeft:'auto',color:'var(--tx)'}},String(S[st])))));
        row.appendChild(leg); c.appendChild(row); return c; })(),
      h('div',{style:{display:'flex',gap:'9px'}},
        tile((S.uptime30!=null?S.uptime30.toFixed(0):'—')+'%','30D UPTIME',''),
        tile(String(S.attention),'NEED YOU','c-down')));
    function tile(v,k,cls){ return h('div',{style:{flex:'1',background:'var(--raise)',borderRadius:'11px',padding:'9px 10px',textAlign:'center'}},
      h('div',{cls:'mono '+cls,style:{fontSize:'20px',fontWeight:'700'}},v),
      h('div',{style:{fontSize:'9px',letterSpacing:'.6px',color:'var(--mut)',textTransform:'uppercase'}},k)); }
    const right=h('div',{cls:'card',style:{flex:'1',padding:'14px 16px',display:'flex',flexDirection:'column',gap:'9px',minWidth:'0'}},
      h('div',{style:{display:'flex',alignItems:'center',justifyContent:'space-between'}},
        h('span',{cls:'lbl'},'Needs attention'),h('span',{cls:'pill down'},att.length+' active')));
    att.forEach(hh=> right.appendChild(h('div',{style:{background:'var(--raise)',border:'1px solid var(--line)',borderRadius:'13px',padding:'11px 13px',display:'flex',flexDirection:'column',gap:'7px'}},
      h('div',{style:{display:'flex',alignItems:'center',gap:'9px'}},pill(hh.status),
        h('span',{style:{fontSize:'15px',fontWeight:'600'}},hh.name),
        h('span',{cls:'mono',style:{marginLeft:'auto',fontSize:'11px',color:'var(--mut)'}},hh.addr)),
      h('div',{style:{display:'flex',alignItems:'center',gap:'9px'}},checks(hh.checks),
        h('span',{style:{flex:'1',fontSize:'11.5px',color:'var(--mut)',whiteSpace:'nowrap',overflow:'hidden',textOverflow:'ellipsis'}},hh.msg||''),
        h('button',{cls:'btn sm',onClick:()=>A().openModal('ack',hh.id)},'Ack'),
        h('button',{cls:'btn sm pri',onClick:()=>A().openModal('pause',hh.id)},'Pause')))));
    if(!att.length) right.appendChild(h('div',{cls:'c-up',style:{fontSize:'13px'}},'All clear.'));
    return h('div',{style:{height:'100%',display:'flex',flexDirection:'column'}},
      h('div',{style:{display:'flex',alignItems:'center',gap:'12px',padding:'15px 20px'}},
        h('div',{style:{width:'32px',height:'32px',borderRadius:'10px',background:'linear-gradient(150deg,var(--teal),var(--blue))',display:'flex',alignItems:'center',justifyContent:'center',color:'#04231b',fontWeight:'700'}},'◆'),
        h('div',{},h('div',{style:{fontSize:'18px',fontWeight:'600'}},'Host Monitor'),
          h('div',{style:{fontSize:'11px',color:'var(--mut)'}},S.total+' hosts · home-lab')),
        h('div',{style:{marginLeft:'auto',background:'var(--panel)',border:'1px solid var(--line)',borderRadius:'11px',padding:'7px 13px',textAlign:'right'}},
          h('div',{cls:'mono',style:{fontSize:'16px',fontWeight:'600'}},STATE.summary.clock||'--:--'),
          h('div',{style:{fontSize:'9.5px',color:'var(--mut)',letterSpacing:'1px'}},STATE.summary.dateLong||''))),
      h('div',{style:{flex:'1',display:'flex',gap:'14px',padding:'2px 18px 14px',minHeight:'0'}},left,right),
      lcdNav('Home'));
  }
  function lcdGrid(){
    const S=STATE.summary; const tiles=STATE.hosts.slice(0,12);
    const grid=h('div',{style:{display:'grid',gridTemplateColumns:'repeat(4,1fr)',gap:'9px'}});
    tiles.forEach(hh=> grid.appendChild(h('div',{cls:'hcard '+hh.status,style:{padding:'9px 11px',cursor:'pointer'},onClick:()=>{A().closeOverlay();A().openHost(hh.id);}},
      h('span',{cls:'bar'}),
      h('div',{style:{display:'flex',alignItems:'center',justifyContent:'space-between',marginBottom:'2px'}},
        h('span',{style:{fontSize:'14px',fontWeight:'600'}},hh.name),dot(hh.status)),
      h('div',{cls:'mono',style:{fontSize:'10px',color:'var(--mut)',marginBottom:'7px'}},hh.addr),
      checks(hh.checks))));
    return h('div',{style:{height:'100%',display:'flex',flexDirection:'column'}},
      h('div',{style:{display:'flex',alignItems:'center',gap:'12px',padding:'15px 20px',borderBottom:'1px solid var(--hair)'}},
        h('div',{style:{fontSize:'18px',fontWeight:'600'}},'All hosts'),
        h('div',{style:{display:'flex',gap:'6px',marginLeft:'4px'}},
          h('span',{cls:'pill up',style:{fontSize:'10px'}},S.up+' up'),
          h('span',{cls:'pill warn',style:{fontSize:'10px'}},String(S.warn)),
          h('span',{cls:'pill down',style:{fontSize:'10px'}},String(S.down))),
        h('div',{cls:'mono',style:{marginLeft:'auto',fontSize:'14px',color:'var(--mut)'}},S.clock||'--:--')),
      h('div',{style:{flex:'1',padding:'13px 18px',minHeight:'0'}},grid,
        STATE.hosts.length>12?h('div',{style:{textAlign:'center',fontFamily:'var(--mono)',fontSize:'12px',color:'var(--faint)',marginTop:'9px'}},'▾ swipe · '+(STATE.hosts.length-12)+' more'):null),
      lcdNav('Hosts'));
  }

  window.OVERLAYS={reasonModal,hostForm,lcdPreview};
})();
