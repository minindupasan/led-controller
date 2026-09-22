/* WebPage.h - single-page UI (black & white), served from flash. */
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>INNOV+IOT SIGN</title>
<style>
:root{
  --bg:#000; --fg:#fff; --dim:#8a8a8a; --line:#242424; --panel:#0b0b0b;
  --hi:#fff; --hi-fg:#000; --warn:#fff;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
     font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
     -webkit-font-smoothing:antialiased}
a{color:var(--fg)}
header{display:flex;align-items:center;gap:14px;padding:14px 18px;border-bottom:1px solid var(--line);
       position:sticky;top:0;background:var(--bg);z-index:10;flex-wrap:wrap}
h1{font-size:15px;letter-spacing:.32em;margin:0;font-weight:600}
.chip{border:1px solid var(--line);padding:3px 9px;font-size:11px;color:var(--dim);letter-spacing:.08em}
.dot{width:8px;height:8px;border-radius:50%;background:#333;display:inline-block;margin-right:6px}
.dot.on{background:#fff;box-shadow:0 0 8px #fff}
nav{display:flex;border-bottom:1px solid var(--line);overflow-x:auto}
nav button{flex:0 0 auto;background:none;border:none;border-right:1px solid var(--line);color:var(--dim);
      padding:12px 22px;font:inherit;letter-spacing:.18em;cursor:pointer}
nav button.active{background:var(--hi);color:var(--hi-fg)}
nav button:hover:not(.active){color:var(--fg)}
main{padding:18px;max-width:1080px}
section{display:none} section.active{display:block}
.panel{border:1px solid var(--line);background:var(--panel);padding:16px;margin-bottom:16px}
.panel h2{font-size:11px;letter-spacing:.24em;color:var(--dim);margin:0 0 14px;font-weight:500;
          text-transform:uppercase}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:10px}
.grow{flex:1}
label.lbl{width:104px;color:var(--dim);font-size:11px;letter-spacing:.12em;text-transform:uppercase}
button,select,input{font:inherit;background:#000;color:var(--fg);border:1px solid var(--line);
                    padding:7px 10px;outline:none}
button{cursor:pointer;letter-spacing:.1em}
button:hover{background:#151515}
button.primary{background:var(--hi);color:var(--hi-fg);border-color:var(--hi);font-weight:600}
button.primary:hover{background:#ddd}
button.sm{padding:4px 8px;font-size:11px}
input:focus,select:focus{border-color:#666}
input[type=number]{width:78px}
input[type=color]{padding:0;width:38px;height:28px;background:none;border:1px solid var(--line)}
input[type=range]{-webkit-appearance:none;height:2px;background:#333;flex:1;min-width:140px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:14px;background:#fff;
      border-radius:50%;cursor:pointer}
.val{width:44px;text-align:right;color:var(--dim)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(112px,1fr));gap:8px}
.grid button.on{background:var(--hi);color:var(--hi-fg);border-color:var(--hi)}
table{width:100%;border-collapse:collapse;font-size:12px}
th{text-align:left;color:var(--dim);font-weight:500;font-size:10px;letter-spacing:.16em;
   padding:6px 6px;border-bottom:1px solid var(--line);text-transform:uppercase}
td{padding:5px 6px;border-bottom:1px solid #131313;vertical-align:middle}
tr.off{opacity:.42}
td input[type=text]{width:82px} td input[type=number]{width:70px}
.bar{display:flex;height:34px;border:1px solid var(--line);margin-bottom:12px;overflow:hidden}
.bar div{display:flex;align-items:center;justify-content:center;font-size:10px;border-right:1px solid #000;
         color:#000;overflow:hidden;cursor:pointer;letter-spacing:.1em}
.bar div.gap{background:#111;color:#444}
#log{height:290px;overflow:auto;background:#000;border:1px solid var(--line);padding:10px;
     font-size:11px;white-space:pre-wrap;color:#c8c8c8}
.kv{display:grid;grid-template-columns:repeat(auto-fill,minmax(168px,1fr));gap:10px}
.kv div{border:1px solid var(--line);padding:9px 11px}
.kv b{display:block;font-size:17px;font-weight:600;margin-top:3px}
.kv span{font-size:10px;color:var(--dim);letter-spacing:.14em;text-transform:uppercase}
.issue{border:1px solid #fff;padding:8px 11px;margin-bottom:8px;font-size:12px}
.hint{color:var(--dim);font-size:11px;margin:6px 0 0;line-height:1.7}
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);background:#fff;color:#000;
       padding:9px 18px;font-size:12px;letter-spacing:.1em;opacity:0;transition:.25s;pointer-events:none}
.toast.show{opacity:1}
hr{border:none;border-top:1px solid var(--line);margin:14px 0}
</style>
</head>
<body>

<header>
  <h1>INNOV+IOT</h1>
  <span class="chip"><i class="dot" id="conn"></i><span id="connTxt">connecting</span></span>
  <span class="chip" id="cFps">-- fps</span>
  <span class="chip" id="cMa">-- mA</span>
  <span class="chip" id="cHeap">-- kB</span>
  <span class="grow"></span>
  <button class="sm" id="btnSave">SAVE TO FLASH</button>
</header>

<nav>
  <button class="active" data-tab="control">CONTROL</button>
  <button data-tab="segments">SEGMENTS</button>
  <button data-tab="mapping">MAPPING</button>
  <button data-tab="debug">DEBUG</button>
</nav>

<main>

<!-- ============================ CONTROL ============================ -->
<section id="control" class="active">
  <div class="panel">
    <h2>Master</h2>
    <div class="row">
      <button id="btnPower" class="primary grow">POWER</button>
      <button class="sm" onclick="cmd('test off')">CLEAR TEST</button>
    </div>
    <div class="row"><label class="lbl">Brightness</label>
      <input type="range" id="gBright" min="0" max="255"><span class="val" id="vBright">-</span></div>
    <div class="row"><label class="lbl">Speed</label>
      <input type="range" id="gSpeed" min="1" max="255"><span class="val" id="vSpeed">-</span></div>
    <div class="row"><label class="lbl">Intensity</label>
      <input type="range" id="gInt" min="0" max="255"><span class="val" id="vInt">-</span></div>
  </div>

  <div class="panel">
    <h2>Animation</h2>
    <div class="grid" id="animGrid"></div>
    <hr>
    <div class="row">
      <label class="lbl">Play mode</label>
      <select id="gMode">
        <option value="0">PARALLEL - all letters together</option>
        <option value="1">STAGGER - phase-shift per letter</option>
        <option value="2">SEQUENCE - one letter at a time</option>
      </select>
      <label class="lbl">Stagger ms</label>
      <input type="number" id="gStagger" min="0" max="5000">
      <label><input type="checkbox" id="gTint"> rainbow tint</label>
    </div>
    <p class="hint">Stagger/sequence timing uses the letter order in the SEGMENTS tab.</p>
  </div>
</section>

<!-- =========================== SEGMENTS ============================ -->
<section id="segments">
  <div class="panel">
    <h2>Layout preview</h2>
    <div class="bar" id="bar"></div>
    <p class="hint">Click a block to blink that segment on the real board.</p>
  </div>

  <div class="panel">
    <h2>Segment table</h2>
    <div id="issues"></div>
    <div style="overflow-x:auto">
    <table>
      <thead><tr>
        <th>#</th><th>Name</th><th>Start</th><th>End</th><th>Len</th>
        <th>Colour</th><th>2nd</th><th>Animation</th><th>Bri</th>
        <th>On</th><th>Rev</th><th></th>
      </tr></thead>
      <tbody id="segBody"></tbody>
    </table>
    </div>
    <hr>
    <div class="row">
      <input type="text" id="nName" placeholder="name" size="8">
      <input type="number" id="nStart" placeholder="start">
      <input type="number" id="nEnd" placeholder="end">
      <button onclick="addSeg()">ADD SEGMENT</button>
      <button class="sm" onclick="post('/api/segments/sort',{}).then(refresh)">SORT BY START</button>
      <span class="grow"></span>
      <label class="lbl">LED count</label>
      <input type="number" id="gCount" min="1" max="1200">
      <button class="sm" onclick="setCount()">APPLY</button>
    </div>
    <p class="hint">Ranges are inclusive: I = 20-100 means LEDs 20 through 100.
       Changing LED count re-initialises the strip.</p>
  </div>
</section>

<!-- ============================ MAPPING ============================ -->
<section id="mapping">
  <div class="panel">
    <h2>Step 1 - find the physical LEDs</h2>
    <div class="row">
      <button onclick="test({mode:'walk',delay:+wDelay.value})">START WALK</button>
      <label class="lbl">Step ms</label><input type="number" id="wDelay" value="400">
      <button onclick="test({mode:'off'})">STOP</button>
      <span class="grow"></span>
      <button class="sm" onclick="test({mode:'all'})">ALL WHITE</button>
    </div>
    <div class="row">
      <label class="lbl">Single LED</label>
      <input type="number" id="tIdx" value="0">
      <button onclick="test({mode:'index',index:+tIdx.value,color:tCol.value})">LIGHT IT</button>
      <button class="sm" onclick="tIdx.value=Math.max(0,+tIdx.value-1);test({mode:'index',index:+tIdx.value,color:tCol.value})">&#8592;</button>
      <button class="sm" onclick="tIdx.value=+tIdx.value+1;test({mode:'index',index:+tIdx.value,color:tCol.value})">&#8594;</button>
      <input type="color" id="tCol" value="#ffffff">
    </div>
    <div class="row">
      <label class="lbl">Range</label>
      <input type="number" id="rA" value="0"><span>to</span><input type="number" id="rB" value="29">
      <button onclick="test({mode:'range',index:+rA.value,end:+rB.value,color:tCol.value})">LIGHT RANGE</button>
    </div>
    <p class="hint">Walk mode marches one LED along the strip, dim markers every 10 LEDs.
       Note the index where a letter starts and ends, then assign it below.</p>
  </div>

  <div class="panel">
    <h2>Step 2 - assign the range to a letter</h2>
    <div class="row">
      <label class="lbl">Segment</label>
      <select id="mSeg" class="grow"></select>
      <button class="sm" onclick="grabWalk('start')">USE CURRENT AS START</button>
      <button class="sm" onclick="grabWalk('end')">USE CURRENT AS END</button>
    </div>
    <div class="row">
      <label class="lbl">Start / End</label>
      <input type="number" id="mA"><input type="number" id="mB">
      <button class="primary" onclick="assign()">ASSIGN RANGE</button>
      <button class="sm" onclick="test({mode:'range',index:+mA.value,end:+mB.value,color:'#ffffff'})">PREVIEW</button>
      <button class="sm" onclick="identifySel()">BLINK SEGMENT</button>
    </div>
    <p class="hint">Live walk index: <b id="walkIdx">-</b> &nbsp;|&nbsp; current test mode: <b id="testMode">none</b></p>
  </div>
</section>

<!-- ============================= DEBUG ============================= -->
<section id="debug">
  <div class="panel">
    <h2>Telemetry</h2>
    <div class="kv" id="tele"></div>
  </div>

  <div class="panel">
    <h2>Console</h2>
    <div class="row">
      <input type="text" id="cmdIn" class="grow" placeholder="type a command, e.g.  seg range I 20 100     (h = help)">
      <button onclick="runCmd()">RUN</button>
      <button class="sm" onclick="cmd('h')">HELP</button>
      <button class="sm" onclick="cmd('s')">STATUS</button>
      <button class="sm" onclick="cmd('l')">LIST</button>
      <button class="sm" onclick="log.textContent=''">CLEAR</button>
    </div>
    <div id="log"></div>
    <p class="hint">Same command set as the USB serial console - anything you can type over
       serial works here, and the output streams back live.</p>
  </div>

  <div class="panel">
    <h2>Config</h2>
    <div class="row">
      <button onclick="post('/api/save',{}).then(()=>toast('saved to flash'))">SAVE</button>
      <button onclick="post('/api/load',{}).then(refresh)">RELOAD</button>
      <button onclick="location.href='/api/export'">EXPORT JSON</button>
      <button onclick="importJson()">IMPORT JSON</button>
      <span class="grow"></span>
      <button onclick="if(confirm('Restore factory defaults?'))post('/api/defaults',{}).then(refresh)">FACTORY DEFAULTS</button>
      <button onclick="if(confirm('Reboot the controller?'))post('/api/reboot',{})">REBOOT</button>
    </div>
    <div class="row">
      <label class="lbl">Power budget</label>
      <input type="number" id="gMa" min="100" max="60000" step="100">
      <button class="sm" onclick="patchGlobal({maxMilliamps:+gMa.value})">APPLY mA</button>
      <span class="hint">FastLED caps total draw to this value.</span>
    </div>
  </div>
</section>

</main>
<div class="toast" id="toast"></div>

<script>
let S={global:{},segments:[],animations:[],issues:[]}, ws, sel=-1;
const $=id=>document.getElementById(id);
const toast=m=>{const t=$('toast');t.textContent=m;t.classList.add('show');
  clearTimeout(t._t);t._t=setTimeout(()=>t.classList.remove('show'),1600)};

/* ---- transport ---- */
async function post(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(b)});return r.json().catch(()=>({}))}
async function refresh(){S=await (await fetch('/api/state')).json();render()}
function patchGlobal(p){Object.assign(S.global,p);post('/api/global',p)}
function patchSeg(i,p){post('/api/segment?i='+i,p).then(refresh)}
function test(p){post('/api/test',p)}
function cmd(c){post('/api/cmd',{cmd:c}).then(r=>{if(r.out)appendLog(r.out)})}
function runCmd(){const v=$('cmdIn').value.trim();if(!v)return;appendLog('> '+v);cmd(v);$('cmdIn').value=''}
$('cmdIn').addEventListener('keydown',e=>{if(e.key==='Enter')runCmd()});

function appendLog(t){const l=$('log');l.textContent+=t+'\n';l.scrollTop=l.scrollHeight}

/* ---- websocket ---- */
function connect(){
  ws=new WebSocket('ws://'+location.host+'/ws');
  ws.onopen =()=>{$('conn').classList.add('on');$('connTxt').textContent='live'};
  ws.onclose=()=>{$('conn').classList.remove('on');$('connTxt').textContent='offline';setTimeout(connect,1500)};
  ws.onmessage=e=>{
    let m;try{m=JSON.parse(e.data)}catch(_){return appendLog(e.data)}
    if(m.type==='log')   return appendLog(m.line);
    if(m.type==='status')return status(m);
  };
}
function status(m){
  $('cFps').textContent=m.fps.toFixed(0)+' fps';
  $('cMa').textContent=m.ma+' mA';
  $('cHeap').textContent=(m.heap/1024|0)+' kB';
  $('walkIdx').textContent=m.testIndex;
  $('testMode').textContent=m.testMode;
  $('tele').innerHTML=[
    ['fps',m.fps.toFixed(1)],['frames',m.frames],['est. current',m.ma+' mA'],
    ['free heap',(m.heap/1024|0)+' kB'],['uptime',fmt(m.uptime)],['wifi',m.mode+' '+m.ip],
    ['rssi',m.rssi+' dBm'],['leds',m.leds],['segments',m.segments],
    ['test mode',m.testMode],['test index',m.testIndex],['unsaved',m.dirty?'YES':'no']
  ].map(([k,v])=>`<div><span>${k}</span><b>${v}</b></div>`).join('');
}
const fmt=s=>`${s/3600|0}h ${(s%3600)/60|0}m ${s%60}s`;

/* ---- render ---- */
function render(){
  const g=S.global;
  $('gBright').value=g.brightness;$('vBright').textContent=g.brightness;
  $('gSpeed').value=g.speed;$('vSpeed').textContent=g.speed;
  $('gInt').value=g.intensity;$('vInt').textContent=g.intensity;
  $('gMode').value=g.playMode;$('gStagger').value=g.stagger;
  $('gTint').checked=g.rainbowTint;$('gCount').value=g.ledCount;$('gMa').value=g.maxMilliamps;
  $('btnPower').textContent=g.power?'POWER ON':'POWER OFF';
  $('btnPower').className=g.power?'primary grow':'grow';

  $('animGrid').innerHTML=S.animations.map(a=>
    `<button class="${a.id==g.anim?'on':''}" onclick="patchGlobal({anim:${a.id}});render()">${a.name}</button>`).join('');

  const opts=S.animations.map(a=>`<option value="${a.id}">${a.name}</option>`).join('');
  $('segBody').innerHTML=S.segments.map(s=>`
    <tr class="${s.enabled?'':'off'}">
      <td>${s.i}</td>
      <td><input type="text" value="${s.name}" onchange="patchSeg(${s.i},{name:this.value})"></td>
      <td><input type="number" value="${s.start}" onchange="patchSeg(${s.i},{start:+this.value})"></td>
      <td><input type="number" value="${s.end}" onchange="patchSeg(${s.i},{end:+this.value})"></td>
      <td>${s.len}</td>
      <td><input type="color" value="${s.color}" onchange="patchSeg(${s.i},{color:this.value})"></td>
      <td><input type="color" value="${s.color2}" onchange="patchSeg(${s.i},{color2:this.value})"></td>
      <td><select onchange="patchSeg(${s.i},{anim:+this.value})">
          <option value="255" ${s.anim==255?'selected':''}>INHERIT</option>
          ${opts.replace('value="'+s.anim+'"','value="'+s.anim+'" selected')}</select></td>
      <td><input type="number" value="${s.brightness}" min="0" max="255" style="width:60px"
           onchange="patchSeg(${s.i},{brightness:+this.value})"></td>
      <td><input type="checkbox" ${s.enabled?'checked':''} onchange="patchSeg(${s.i},{enabled:this.checked})"></td>
      <td><input type="checkbox" ${s.reversed?'checked':''} onchange="patchSeg(${s.i},{reversed:this.checked})"></td>
      <td><button class="sm" onclick="post('/api/identify',{i:${s.i}})">BLINK</button>
          <button class="sm" onclick="delSeg(${s.i})">DEL</button></td>
    </tr>`).join('');

  $('issues').innerHTML=S.issues.map(i=>
    `<div class="issue">! segment ${i.a}${i.b>=0?' and '+i.b:''} - ${i.what}</div>`).join('');

  /* preview bar, proportional to the strip */
  const n=g.ledCount||1;let html='',cursor=0;
  [...S.segments].sort((a,b)=>a.start-b.start).forEach(s=>{
    if(s.start>cursor)html+=`<div class="gap" style="flex:${s.start-cursor}">&middot;</div>`;
    html+=`<div style="flex:${Math.max(1,s.len)};background:${s.enabled?s.color:'#333'}"
             title="${s.name} ${s.start}-${s.end}" onclick="post('/api/identify',{i:${s.i}})">${s.name}</div>`;
    cursor=s.end+1;
  });
  if(cursor<n)html+=`<div class="gap" style="flex:${n-cursor}">&middot;</div>`;
  $('bar').innerHTML=html;

  $('mSeg').innerHTML=S.segments.map(s=>
    `<option value="${s.i}">${s.i} - ${s.name}  (${s.start}-${s.end})</option>`).join('');
  if(sel>=0)$('mSeg').value=sel;
  syncMapFields();
}

function syncMapFields(){
  const s=S.segments[+$('mSeg').value];
  if(s){$('mA').value=s.start;$('mB').value=s.end}
}
$('mSeg').addEventListener('change',()=>{sel=+$('mSeg').value;syncMapFields()});

/* ---- actions ---- */
$('btnPower').onclick=()=>{patchGlobal({power:!S.global.power});render()};
['gBright:brightness:vBright','gSpeed:speed:vSpeed','gInt:intensity:vInt'].forEach(spec=>{
  const [id,key,out]=spec.split(':');
  $(id).addEventListener('input',e=>{$(out).textContent=e.target.value;patchGlobal({[key]:+e.target.value})});
});
$('gMode').onchange=e=>patchGlobal({playMode:+e.target.value});
$('gStagger').onchange=e=>patchGlobal({stagger:+e.target.value});
$('gTint').onchange=e=>patchGlobal({rainbowTint:e.target.checked});
$('btnSave').onclick=()=>post('/api/save',{}).then(()=>toast('saved to flash'));

function addSeg(){
  post('/api/segment/add',{name:$('nName').value||'SEG',start:+$('nStart').value||0,end:+$('nEnd').value||0})
    .then(refresh);
}
function delSeg(i){if(confirm('Delete segment '+i+'?'))post('/api/segment/del',{i}).then(refresh)}
function setCount(){patchGlobal({ledCount:+$('gCount').value});setTimeout(refresh,400)}
function identifySel(){post('/api/identify',{i:+$('mSeg').value})}
function grabWalk(which){
  const v=parseInt($('walkIdx').textContent)||0;
  $(which==='start'?'mA':'mB').value=v;
}
function assign(){
  const i=+$('mSeg').value;
  patchSeg(i,{start:+$('mA').value,end:+$('mB').value});
  toast('range assigned');
}
function importJson(){
  const t=prompt('Paste exported config JSON:');
  if(!t)return;
  fetch('/api/import',{method:'POST',headers:{'Content-Type':'application/json'},body:t})
    .then(()=>refresh()).then(()=>toast('imported'));
}

/* ---- tabs ---- */
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.remove('active'));
  document.querySelectorAll('section').forEach(x=>x.classList.remove('active'));
  b.classList.add('active');$(b.dataset.tab).classList.add('active');
});

connect();refresh();
setInterval(()=>{if(!ws||ws.readyState!==1)refresh()},5000);
</script>
</body></html>
)HTMLPAGE";
