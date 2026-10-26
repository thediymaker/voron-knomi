// Web UI pages (served from flash).
#pragma once
#include <pgmspace.h>

static const char STYLE_CSS[] PROGMEM = R"CSS(
:root{--bg:#0b0d10;--card:#16191e;--line:#262a31;--text:#e8eaed;--muted:#8e939b;--accent:#38d9f5;--bad:#ff453a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:760px;margin:0 auto;padding:16px}h1{font-size:22px;margin:8px 0 16px}h2{font-size:15px;margin:0 0 12px;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.06em}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
label{display:block;margin:10px 0 4px;color:var(--muted);font-size:13px}input,select{width:100%;padding:9px 10px;border-radius:8px;border:1px solid var(--line);background:#0f1115;color:var(--text);font:inherit}
input[type=checkbox]{width:auto;margin-right:6px}input[type=color]{height:40px;padding:2px}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
button,.btn{display:inline-block;background:var(--accent);color:#001318;border:0;border-radius:8px;padding:9px 14px;font:600 14px system-ui;cursor:pointer;text-decoration:none;margin:4px 6px 0 0}
button.ghost,.btn.ghost{background:transparent;color:var(--text);border:1px solid var(--line)}button.bad{background:var(--bad);color:#fff}
.kv{display:grid;grid-template-columns:max-content 1fr;gap:4px 14px;font-size:14px}.kv b{color:var(--muted);font-weight:500}
.msg{margin-top:10px;font-size:14px;color:var(--muted)}pre{background:#0f1115;border-radius:8px;padding:10px;max-height:320px;overflow:auto;font-size:12px;white-space:pre-wrap}
.screen{display:flex;gap:16px;align-items:center;flex-wrap:wrap}.screen img{width:240px;height:240px;border-radius:50%;background:#000;border:6px solid #222}
progress{width:100%;height:8px}small{color:var(--muted)}
@media(max-width:520px){.row{grid-template-columns:1fr}}
)CSS";

static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>KNOMI setup</title><link rel="stylesheet" href="/style.css"></head>
<body><main><h1>KNOMI-NG</h1><section><h2>Set an admin password</h2>
<p>Settings and firmware updates need this password from now on (user <b>admin</b>).</p>
<form id="f"><label>Password (8+ characters)</label><input type="password" name="password" minlength="8" required autocomplete="new-password">
<label>Repeat</label><input type="password" id="p2" required autocomplete="new-password"><button>Save</button></form>
<div class="msg" id="m"></div></section></main>
<script>
f.onsubmit=async e=>{e.preventDefault();if(f.password.value!==p2.value){m.textContent='Passwords differ';return}
const r=await fetch('/api/setup',{method:'POST',body:new URLSearchParams(new FormData(f))});
m.textContent=r.ok?'Saved. Reloading...':(await r.json()).error;if(r.ok)setTimeout(()=>location.reload(),800)}
</script></body></html>)HTML";

static const char LOGIN_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>KNOMI login</title><link rel="stylesheet" href="/style.css"></head>
<body><main><h1>KNOMI-NG</h1><section><h2>Log in</h2>
<form id="f"><label>Admin password</label><input type="password" name="password" required autocomplete="current-password" autofocus>
<button>Log in</button></form><div class="msg" id="m"></div></section></main>
<script>
f.onsubmit=async e=>{e.preventDefault();m.textContent='...';
const r=await fetch('/api/login',{method:'POST',body:new URLSearchParams(new FormData(f))});
if(r.ok)location.reload();else m.textContent=(await r.json()).error}
</script></body></html>)HTML";

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>KNOMI-NG</title><link rel="stylesheet" href="/style.css"></head>
<body><main><h1>KNOMI-NG <button class="ghost" style="float:right" onclick="fetch('/api/logout',{method:'POST'}).then(()=>location.reload())">Log out</button></h1>

<section><h2>Screen</h2><div class="screen"><img id="shot" alt="screen">
<div><button class="ghost" onclick="tile(0)">Face</button><button class="ghost" onclick="tile(1)">Print</button>
<button class="ghost" onclick="tile(2)">Temps</button><button class="ghost" onclick="tile(3)">Box Turtle</button><br>
<button class="ghost" onclick="shoot()">Refresh</button><label><input type="checkbox" id="live">live</label></div></div></section>

<section><h2>Status</h2><div class="kv" id="st"></div></section>

<section><h2>Settings</h2><form id="cf">
<div class="row"><div><label>Wi-Fi network</label><input name="wifi_ssid" list="nets"><datalist id="nets"></datalist></div>
<div><label>Wi-Fi password</label><input type="password" name="wifi_pass" autocomplete="new-password"></div></div>
<button type="button" class="ghost" onclick="scan()">Scan networks</button>
<div class="row"><div><label>Moonraker host</label><input name="mr_host"></div><div><label>Port</label><input name="mr_port" type="number"></div></div>
<div class="row"><div><label>Moonraker API key (optional)</label><input type="password" name="mr_key" autocomplete="off"></div>
<div><label>&nbsp;</label><label><input type="checkbox" name="mr_key_clear" value="1">remove stored key</label></div></div>
<div class="row"><div><label>Theme</label><select name="theme" id="theme"></select></div>
<div><label>Eyes</label><div class="row"><select name="eyes" id="eyes"><option value="white">White</option><option value="theme">Theme colour</option><option value="custom">Custom</option></select><input type="color" name="eyes_color"></div></div></div>
<div class="row"><div><label>Eyes react to</label><select name="gaze"><option value="0">Toolhead motion (accelerometer)</option><option value="1">Nozzle position</option><option value="2">Nothing, just wander</option></select></div>
<div><label>Hostname</label><input name="hostname"></div></div>
<div class="row"><div><label>Time zone (POSIX)</label><input name="tz" list="tzs"><datalist id="tzs">
<option value="MST7">Arizona</option><option value="PST8PDT,M3.2.0,M11.1.0">US Pacific</option><option value="MST7MDT,M3.2.0,M11.1.0">US Mountain</option>
<option value="CST6CDT,M3.2.0,M11.1.0">US Central</option><option value="EST5EDT,M3.2.0,M11.1.0">US Eastern</option><option value="UTC0">UTC</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0">UK</option><option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option></datalist></div>
<div><label>&nbsp;</label><label><input type="checkbox" name="clock24" value="1">24-hour clock</label></div></div>
<div class="row"><div><label>Fall asleep after (seconds idle)</label><input name="sleep_s" type="number" min="30"></div>
<div><label>Brightness awake / asleep (%)</label><div class="row"><input name="brightness" type="number" min="5" max="100"><input name="sleep_brightness" type="number" min="0" max="100"></div></div></div>
<label><input type="checkbox" name="afc_screen" value="1">Box Turtle screen (shown when Klipper has an AFC unit)</label>
<label><input type="checkbox" name="inv_x" value="1">Flip eye movement left/right</label>
<label><input type="checkbox" name="inv_y" value="1">Flip eye movement up/down</label>
<label>New admin password (leave empty to keep)</label><input type="password" name="admin_pass" autocomplete="new-password">
<button>Save</button><small>Empty password fields keep the stored value; stored passwords are never shown.</small></form>
<div class="msg" id="cm"></div></section>

<section><h2>Firmware</h2><div class="kv" id="fw"></div>
<label>Install firmware (.bin)</label><input type="file" id="bin" accept=".bin"><progress id="pr" value="0" max="100" hidden></progress>
<button onclick="upload()">Install</button>
<p><small>Backups (download these before the first update over KNOMI-NG; it overwrites the other slot):</small><br>
<a class="btn ghost" href="/api/backup?part=app">Other firmware slot</a><a class="btn ghost" href="/api/backup?part=fs">GIF filesystem</a><a class="btn ghost" href="/api/backup?part=nvs">Settings (NVS)</a></p>
<button class="ghost" onclick="post('/api/restart')">Restart</button><button class="bad" onclick="if(confirm('Boot the firmware in the other slot?'))post('/api/boot-other')">Boot other slot</button>
<div class="msg" id="fm"></div></section>

<section><h2>Log</h2><button class="ghost" onclick="loadLog()">Refresh</button><pre id="log"></pre></section>
</main>
<script>
const $=id=>document.getElementById(id);
const _f=window.fetch;window.fetch=async(...a)=>{const r=await _f(...a);if(r.status==401)location.reload();return r};
const kv=(el,o)=>{el.innerHTML='';for(const[k,v]of Object.entries(o)){const b=document.createElement('b');b.textContent=k;const s=document.createElement('span');s.textContent=v;el.append(b,s)}};
async function status(){try{const s=await(await fetch('/api/status')).json();
kv($('st'),{'Printer':s.moonraker?('connected, Klipper '+s.klippy):'not connected','AFC lanes':s.afc_lanes,'IP':s.ip,'Wi-Fi signal':s.rssi+' dBm',
'Air':s.air_temp!==undefined?s.air_temp.toFixed(1)+' °C, '+s.air_hum.toFixed(0)+' %':'-','Uptime':Math.floor(s.uptime_s/60)+' min','Free RAM / PSRAM':(s.heap>>10)+' / '+(s.psram>>10)+' KB'});
kv($('fw'),{'Version':s.version,'Running':s.running,'Other slot':s.other})}catch(e){}}
function shoot(){$('shot').src='/api/screenshot.bmp?'+Date.now()}
async function tile(i){await fetch('/api/tile?i='+i,{method:'POST'});setTimeout(shoot,300)}
async function post(u){const r=await fetch(u,{method:'POST'});$('fm').textContent=await r.text()}
async function loadCfg(){const c=await(await fetch('/api/config')).json();const f=$('cf');
if(!f.theme.options.length)c.themes.forEach(t=>{const o=document.createElement('option');o.value=t.id;o.textContent=t.name;f.theme.append(o)});
for(const k of['wifi_ssid','mr_host','mr_port','hostname','theme','eyes','eyes_color','gaze','tz','sleep_s','brightness','sleep_brightness'])f[k].value=c[k];
f.inv_x.checked=c.inv_x;f.inv_y.checked=c.inv_y;f.clock24.checked=c.clock24;f.afc_screen.checked=c.afc_screen;
f.wifi_pass.placeholder=c.wifi_pass_set?'stored (hidden)':'not set';f.mr_key.placeholder=c.mr_key_set?'stored (hidden)':'not set'}
async function scan(){$('cm').textContent='Scanning...';const n=await(await fetch('/api/scan')).json();
$('nets').innerHTML='';n.forEach(x=>{const o=document.createElement('option');o.value=x.ssid;o.label=x.rssi+' dBm';$('nets').append(o)});$('cm').textContent=n.length+' networks found'}
$('cf').onsubmit=async e=>{e.preventDefault();const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData($('cf')))});
$('cm').textContent=r.ok?await r.text():(await r.json()).error;['wifi_pass','mr_key','admin_pass'].forEach(k=>$('cf')[k].value='');loadCfg()}
function upload(){const f=$('bin').files[0];if(!f)return;const x=new XMLHttpRequest(),d=new FormData();d.append('firmware',f,f.name);
$('pr').hidden=false;x.upload.onprogress=e=>$('pr').value=e.loaded/e.total*100;
x.onload=()=>{$('fm').textContent=x.responseText;if(x.status==200)setTimeout(()=>location.reload(),9000)};x.open('POST','/update');x.send(d)}
async function loadLog(){$('log').textContent=await(await fetch('/api/log')).text();$('log').scrollTop=1e9}
status();loadCfg();shoot();loadLog();setInterval(status,4000);setInterval(()=>{if($('live').checked)shoot()},1500);
</script></body></html>)HTML";
