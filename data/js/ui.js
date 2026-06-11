/* ui.js — shared constants, a tiny DOM builder, and SVG/widget render helpers. */
(function(){
  const CHECK_NAME={ping:'Ping',dns:'DNS resolution',port:'Port open',http:'HTTP code',ssl:'SSL/TLS expiry',trace:'Traceroute'};
  const CHECK_ABBR={ping:'PNG',dns:'DNS',port:'PRT',http:'HTP',ssl:'SSL',trace:'TRC'};
  const CHECK_ICON={ping:'◌',dns:'⊙',port:'⊟',http:'⤓',ssl:'⛨',trace:'⋯'};
  const LBL={up:'Up',warn:'Warning',down:'Down',paused:'Paused',ack:'Ack’d',off:'Off'};
  const INTERVAL_OPTS=[10,30,60,120,300,900,3600,21600,43200,86400];
  const COL={up:'#34d399',warn:'#fbbf24',down:'#f87171',paused:'#a78bfa',ack:'#38bdf8'};

  function fmtEvery(s){
    if(s>=86400 && s%86400===0) return (s/86400)+'d';
    if(s>=3600 && s%3600===0)   return (s/3600)+'h';
    if(s>=60 && s%60===0)       return (s/60)+'m';
    if(s>=3600) return (s/3600)+'h';
    if(s>=60)   return (s/60)+'m';
    return s+'s';
  }

  // DOM builder: h('div',{cls,html,style,onClick,...attrs}, ...children)
  function h(tag, props){
    const e=document.createElement(tag);
    if(props) for(const k in props){
      const v=props[k];
      if(v==null||v===false) continue;
      if(k==='cls') e.className=v;
      else if(k==='html') e.innerHTML=v;
      else if(k==='style' && typeof v==='object') Object.assign(e.style,v);
      else if(k.startsWith('on') && typeof v==='function') e.addEventListener(k.slice(2).toLowerCase(),v);
      else e.setAttribute(k,v);
    }
    for(let i=2;i<arguments.length;i++) append(e, arguments[i]);
    return e;
  }
  function append(e, kid){
    if(kid==null||kid===false) return;
    if(Array.isArray(kid)){ kid.forEach(k=>append(e,k)); return; }
    e.appendChild(typeof kid==='object'?kid:document.createTextNode(String(kid)));
  }
  function svg(str){ const d=document.createElement('div'); d.innerHTML=str.trim(); return d.firstChild; }
  function clear(el){ while(el.firstChild) el.removeChild(el.firstChild); return el; }

  // ---- widgets ----
  function dot(status){ return h('span',{cls:'dot',style:{color:COL[status]}}); }
  function pill(status,lg){ const p=h('span',{cls:'pill '+status+(lg?' lg':'')}); p.appendChild(dot(status)); p.appendChild(document.createTextNode(LBL[status])); return p; }
  function checks(arr){
    const w=h('div',{cls:'cks'});
    arr.forEach(c=> w.appendChild(h('span',{cls:'ck '+c.state, title:CHECK_NAME[c.key]+' · '+c.detail}, CHECK_ABBR[c.key])));
    return w;
  }

  function donut(S,size,stroke){
    size=size||120; stroke=stroke||12;
    const segs=[['up',S.up],['warn',S.warn],['down',S.down],['ack',S.ack],['paused',S.paused]];
    const r=(size-stroke)/2, c=2*Math.PI*r, cx=size/2; let off=0, circles='';
    segs.forEach(([st,v])=>{ const len=c*((v||0)/(S.total||1));
      circles+='<circle cx="'+cx+'" cy="'+cx+'" r="'+r+'" fill="none" stroke="'+COL[st]+'" stroke-width="'+stroke+
        '" stroke-dasharray="'+len+' '+(c-len)+'" stroke-dashoffset="'+(-off)+'" transform="rotate(-90 '+cx+' '+cx+')"/>';
      off+=len; });
    const wrap=h('div',{style:{position:'relative',width:size+'px',height:size+'px',flex:'none'}});
    wrap.innerHTML='<svg width="'+size+'" height="'+size+'" viewBox="0 0 '+size+' '+size+'">'+
      '<circle cx="'+cx+'" cy="'+cx+'" r="'+r+'" fill="none" stroke="#26313f" stroke-width="'+stroke+'"/>'+circles+'</svg>'+
      '<div style="position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center">'+
      '<div style="font-size:'+(size*0.26)+'px;font-weight:700;line-height:1">'+S.up+
      '<span style="font-size:'+(size*0.12)+'px;color:var(--mut)">/'+S.total+'</span></div>'+
      '<div style="font-size:9px;letter-spacing:1.2px;color:var(--mut);text-transform:uppercase">healthy</div></div>';
    return wrap;
  }

  function spark(data,w,hh,color,fill,sw){
    w=w||200; hh=hh||34; color=color||'#2dd4bf'; sw=sw||1.6;
    const vals=data.map(d=>Array.isArray(d)?d[0]:d);
    if(!vals.length) vals.push(0,0);
    const max=Math.max.apply(null,vals), min=Math.min.apply(null,vals);
    const pts=vals.map((v,i)=>[(i/(vals.length-1||1))*w, hh-1.5-((v-min)/((max-min)||1))*(hh-3)]);
    const line=pts.map((p,i)=>(i?'L':'M')+p[0].toFixed(1)+' '+p[1].toFixed(1)).join(' ');
    const area=line+' L '+w+' '+hh+' L 0 '+hh+' Z';
    const id='sg'+Math.random().toString(36).slice(2,7);
    let s='<svg width="100%" height="'+hh+'" viewBox="0 0 '+w+' '+hh+'" preserveAspectRatio="none" style="display:block">';
    if(fill) s+='<defs><linearGradient id="'+id+'" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="'+color+'" stop-opacity="0.32"/><stop offset="1" stop-color="'+color+'" stop-opacity="0"/></linearGradient></defs><path d="'+area+'" fill="url(#'+id+')"/>';
    s+='<path d="'+line+'" fill="none" stroke="'+color+'" stroke-width="'+sw+'" vector-effect="non-scaling-stroke"/></svg>';
    return svg(s);
  }

  function uptimeBars(days,mini){
    const w=h('div',{cls:mini?'uptime-mini':'uptime-bars'});
    days.forEach(s=> w.appendChild(h('i',{cls:s, style:s==='up'?{height:'100%'}:null})));
    return w;
  }

  // Per-check frequency control (matches the design popover).
  function freq(value, def, onChange){
    const isOver=value!==def;
    const wrap=h('div',{cls:'freq',style:{position:'relative'}});
    const col=h('div',{style:{display:'flex',flexDirection:'column',alignItems:'flex-end',gap:'3px'}});
    const btn=h('button',{cls:'fbtn'},
      h('span',{cls:'fc'},'every'), h('span',{cls:'fv'},fmtEvery(value)), h('span',{cls:'fc'},'▾'));
    const lbl=h('span',{cls:'fdefault'+(isOver?' over':'')}, isOver?'custom override':'default');
    col.appendChild(btn); col.appendChild(lbl); wrap.appendChild(col);
    let pop=null;
    function close(){ if(pop){ pop.remove(); pop=null; document.removeEventListener('pointerdown',out,true); } }
    function out(e){ if(pop && !wrap.contains(e.target)) close(); }
    btn.addEventListener('click',(e)=>{ e.stopPropagation(); if(pop){close();return;}
      pop=h('div',{cls:'freqpop',style:{top:'calc(100% + 6px)',right:'0'}, onClick:e=>e.stopPropagation()},
        h('div',{cls:'ph'},'check every'),
        h('div',{cls:'opts'}, INTERVAL_OPTS.map(s=>
          h('button',{cls:(s===value?'on ':'')+(s===def?'def':''), onClick:()=>{close();onChange(s);}}, fmtEvery(s)))),
        h('div',{cls:'foot'},
          h('span',{cls:'ph',style:{padding:'0'}},'default '+fmtEvery(def)),
          isOver? h('button',{cls:'reset',onClick:()=>{close();onChange(def);}},'↺ reset to default')
                : h('span',{cls:'fdefault'},'using default')));
      wrap.appendChild(pop);
      setTimeout(()=>document.addEventListener('pointerdown',out,true),0);
    });
    return wrap;
  }

  window.UI={CHECK_NAME,CHECK_ABBR,CHECK_ICON,LBL,INTERVAL_OPTS,COL,fmtEvery,
    h,append,svg,clear,dot,pill,checks,donut,spark,uptimeBars,freq};
})();
