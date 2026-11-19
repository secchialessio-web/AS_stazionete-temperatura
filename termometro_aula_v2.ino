// Termometro d'aula: ESP32 + DS18B20 + RTC DS3231
// Librerie: OneWire, DallasTemperature, RTClib (Adafruit)
// v2: sito scrollabile, sfondo animato, grafico smooth interattivo con storico (1 ora)

#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <RTClib.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

// ---------- CONFIGURAZIONE ----------
// Attiva UNA SOLA modalità:
#define MODALITA_CLIENT
//define MODALITA_ACCESS_POINT

const char* WIFI_SSID = "wifi";
const char* WIFI_PASS = "AssoDiCuori!!";

const char* AP_SSID = "CLASSROOM_CLIMATE";
const char* AP_PASS = "12345678";

const int PIN_DS18B20 = 4;
const int PIN_LED = 5;
const char* TZ_ITALIA = "CET-1CEST,M3.5.0,M10.5.0/3";

#if defined(MODALITA_CLIENT) && defined(MODALITA_ACCESS_POINT)
  #error "Attiva una sola modalità Wi-Fi"
#elif !defined(MODALITA_CLIENT) && !defined(MODALITA_ACCESS_POINT)
  #error "Definisci una modalità Wi-Fi"
#endif

OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);
RTC_DS3231 rtc;
WebServer server(80);

float temperatura = NAN;
bool rtcPresente = false;
volatile bool ntpSincronizzato = false;

unsigned long tRichiesta = 0;
const unsigned long INTERVALLO_LETTURA = 400;
bool inConversione = false;
unsigned long tUltimoReconnect = 0;

// ---------- STORICO TEMPERATURE (buffer circolare) ----------
// 1 campione ogni 5 s, 720 campioni = ultima ora. Il browser lo scarica
// all'apertura della pagina, così il grafico non parte mai da zero.
const int MAX_STORICO = 720;
const unsigned long INTERVALLO_STORICO = 5000;
float histV[MAX_STORICO];
uint32_t histT[MAX_STORICO];
int histHead = 0;
int histCount = 0;
unsigned long tUltimoCampione = 0;

const char PAGINA[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Classroom Climate</title>
<style>
:root{--h:190;--accent:hsl(var(--h),90%,66%);--p:0}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;font-family:'Segoe UI',system-ui,Arial,sans-serif;color:#fff;background:#0b1030;overflow-x:hidden}
.bg{position:fixed;inset:0;z-index:-3;background:linear-gradient(135deg,#0b1030,#2a1b5e,#0f4a6b,#3b1d6e,#0b2a4a,#0b1030);background-size:400% 400%;animation:bgMove 24s ease infinite;filter:hue-rotate(calc(var(--p)*70deg));transition:filter .4s linear}
@keyframes bgMove{0%{background-position:0% 50%}50%{background-position:100% 50%}100%{background-position:0% 50%}}
.orbs{position:fixed;inset:0;z-index:-2;pointer-events:none;overflow:hidden}
.orb{position:absolute;border-radius:50%;filter:blur(60px);opacity:.35;will-change:transform}
.orb.one{width:340px;height:340px;background:#a855f7;top:-80px;left:-80px}
.orb.two{width:420px;height:420px;background:hsl(var(--h),85%,50%);bottom:-120px;right:-100px}
.orb.three{width:200px;height:200px;background:#6366f1;top:45%;left:60%}
nav{position:fixed;top:0;left:0;right:0;z-index:20;display:flex;justify-content:space-between;align-items:center;padding:12px 20px;background:#0b103088;backdrop-filter:blur(18px);border-bottom:1px solid #ffffff14}
.brand{display:flex;align-items:center;gap:10px;font-weight:700;letter-spacing:1.5px;font-size:14px}
.brand i{font-style:normal;width:32px;height:32px;display:grid;place-items:center;border-radius:11px;background:linear-gradient(135deg,#8b5cf6,var(--accent));font-size:17px}
.links{display:flex;gap:6px}
.links a{color:#c4c9e8;text-decoration:none;font-size:12px;padding:7px 12px;border-radius:20px;letter-spacing:1px;transition:.2s}
.links a:hover{background:#ffffff18;color:#fff}
.live{padding:7px 13px;border:1px solid #ffffff25;background:#ffffff10;border-radius:30px;font-size:11px;color:#a5f3fc;letter-spacing:1px;white-space:nowrap}
.live span{display:inline-block;width:7px;height:7px;background:#4ade80;border-radius:50%;margin-right:7px;box-shadow:0 0 10px #4ade80;animation:pulse 1.6s infinite}
@keyframes pulse{50%{opacity:.35}}
section{max-width:960px;margin:auto;padding:90px 18px 40px;position:relative}
.hero{min-height:100vh;display:flex;flex-direction:column;justify-content:center;align-items:center;text-align:center}
.hero small{color:#a5b4fc;letter-spacing:4px;font-size:12px}
.hero h2{font-size:clamp(24px,4.5vw,36px);font-weight:600;margin:10px 0 0}
.bigtemp{font-size:clamp(90px,22vw,190px);font-weight:700;letter-spacing:-6px;line-height:1.05;margin:20px 0 0;background:linear-gradient(100deg,hsl(var(--h),90%,72%),#a5b4fc,#e9d5ff);-webkit-background-clip:text;background-clip:text;-webkit-text-fill-color:transparent;font-variant-numeric:tabular-nums}
.bigtemp sup{font-size:.28em;letter-spacing:0;vertical-align:top;position:relative;top:.25em}
.badge{margin-top:8px;display:inline-block;padding:9px 20px;border-radius:30px;background:#ffffff14;border:1px solid #ffffff28;backdrop-filter:blur(14px);font-size:14px;transition:.4s}
.hero p{color:#b6c3e2;font-size:14px;margin:14px 0 0}
.scroll{position:absolute;bottom:26px;left:50%;transform:translateX(-50%);color:#a5b4fc;font-size:11px;letter-spacing:3px;text-align:center;animation:bob 2s ease-in-out infinite}
.scroll b{display:block;font-size:20px;font-weight:400}
@keyframes bob{50%{transform:translate(-50%,8px)}}
h3{font-size:12px;letter-spacing:3px;color:#a5b4fc;text-transform:uppercase;margin:0 0 6px;font-weight:600}
.title{font-size:clamp(22px,4vw,30px);margin:0 0 22px;font-weight:600}
.card{background:linear-gradient(145deg,#ffffff20,#ffffff08);border:1px solid #ffffff25;border-radius:26px;padding:24px;backdrop-filter:blur(22px);box-shadow:0 20px 60px #00000030;position:relative;overflow:hidden}
.chart-head{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px;margin-bottom:12px}
.tabs{display:flex;gap:6px;background:#ffffff10;padding:4px;border-radius:30px}
.tabs button{border:0;background:transparent;color:#c4c9e8;padding:8px 16px;border-radius:24px;font-size:12px;cursor:pointer;transition:.25s;font-family:inherit}
.tabs button.on{background:linear-gradient(135deg,#8b5cf6,var(--accent));color:#fff;box-shadow:0 4px 18px #8b5cf655}
#chart{width:100%;height:340px;display:block;touch-action:pan-y;cursor:crosshair}
.hint{color:#8796bc;font-size:11px;letter-spacing:1px;margin-top:8px;text-align:center}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin-top:18px}
.stat{background:#ffffff10;border:1px solid #ffffff20;border-radius:22px;padding:18px;backdrop-filter:blur(15px);transition:transform .25s,background .25s}
.stat:hover{transform:translateY(-4px);background:#ffffff18}
.stat .l{font-size:11px;letter-spacing:1.5px;color:#a5b4fc;text-transform:uppercase}
.stat .v{font-size:28px;font-weight:600;margin-top:10px;font-variant-numeric:tabular-nums}
.stat .v small{font-size:14px;color:#c4c9e8}
.gauge{position:relative;height:16px;border-radius:10px;margin:34px 0 10px;background:linear-gradient(90deg,#38bdf8,#22d3ee 25%,#4ade80 45%,#4ade80 60%,#fbbf24 75%,#f87171)}
.marker{position:absolute;top:-8px;width:6px;height:32px;border-radius:4px;background:#fff;box-shadow:0 0 16px #fff;left:0;transition:left .8s cubic-bezier(.3,1.3,.5,1)}
.scale{display:flex;justify-content:space-between;color:#8796bc;font-size:11px}
.comfort-text{font-size:15px;color:#d5dcf5;margin-top:18px;line-height:1.6}
.two{display:grid;grid-template-columns:1fr 1fr;gap:18px}
.clock{font-size:clamp(38px,8vw,60px);font-weight:600;margin:16px 0 4px;font-variant-numeric:tabular-nums;letter-spacing:1px}
.date{color:#c4c9e8;font-size:14px}
.row{display:flex;align-items:center;justify-content:space-between;padding:13px 0;border-bottom:1px solid #ffffff14;font-size:14px}
.row:last-child{border:0}
.row span:first-child{color:#c4c9e8}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:#4ade80;box-shadow:0 0 10px currentColor;margin-right:8px;color:#4ade80}
.footer{text-align:center;color:#8796bc;font-size:11px;letter-spacing:2px;padding:50px 0 40px}
.reveal{opacity:0;transform:translateY(40px);transition:opacity .9s ease,transform .9s ease}
.reveal.in{opacity:1;transform:none}
@media(max-width:700px){.links{display:none}.stats{grid-template-columns:1fr 1fr}.two{grid-template-columns:1fr}#chart{height:260px}.card{padding:18px}}
</style>
</head>
<body>
<div class="bg" id="bg"></div>
<div class="orbs"><div class="orb one" id="o1"></div><div class="orb two" id="o2"></div><div class="orb three" id="o3"></div></div>

<nav>
  <div class="brand"><i>🌡️</i>CLASSROOM CLIMATE</div>
  <div class="links"><a href="#home">HOME</a><a href="#grafico">GRAFICO</a><a href="#comfort">COMFORT</a><a href="#info">DISPOSITIVO</a></div>
  <div class="live"><span></span>LIVE</div>
</nav>

<section class="hero" id="home">
  <small>SMART ENVIRONMENT</small>
  <h2>Il clima della tua aula.</h2>
  <div class="bigtemp"><span id="temp">--.-</span><sup>°C</sup></div>
  <div class="badge" id="badge">In attesa del sensore...</div>
  <p id="tempNote">Controllo intelligente della temperatura in tempo reale</p>
  <div class="scroll">SCORRI<b>⌄</b></div>
</section>

<section id="grafico">
  <div class="reveal">
    <h3>Andamento</h3>
    <div class="title">Temperatura nel tempo</div>
    <div class="card">
      <div class="chart-head">
        <div class="tabs" id="tabs">
          <button data-r="300000">5 min</button>
          <button data-r="900000" class="on">15 min</button>
          <button data-r="3600000">1 ora</button>
        </div>
        <div style="font-size:12px;color:#c4c9e8" id="chartNow"></div>
      </div>
      <canvas id="chart"></canvas>
      <div class="hint">Passa il mouse o trascina il dito sul grafico per vedere i valori</div>
    </div>
    <div class="stats">
      <div class="stat"><div class="l">Minima</div><div class="v"><span id="sMin">--</span><small> °C</small></div></div>
      <div class="stat"><div class="l">Massima</div><div class="v"><span id="sMax">--</span><small> °C</small></div></div>
      <div class="stat"><div class="l">Media</div><div class="v"><span id="sAvg">--</span><small> °C</small></div></div>
      <div class="stat"><div class="l">Tendenza</div><div class="v" id="sTrend">--</div></div>
    </div>
  </div>
</section>

<section id="comfort">
  <div class="reveal">
    <h3>Comfort</h3>
    <div class="title">Come si sta in aula?</div>
    <div class="card">
      <div class="gauge"><div class="marker" id="marker"></div></div>
      <div class="scale"><span>10°</span><span>15°</span><span>20°</span><span>25°</span><span>30°</span><span>35°</span></div>
      <div class="comfort-text" id="comfortText">In attesa dei dati...</div>
    </div>
  </div>Sensore192
</section>

<section id="info">
  <div class="reveal">
    <h3>Dispositivo</h3>
    <div class="title">Ora e stato del sistema</div>
    <div class="two">
      <div class="card">
        <div class="card-label" style="color:#c4c9e8;font-size:12px;letter-spacing:2px">ORA LOCALE</div>
        <div class="clock" id="ora">--:--:--</div>
        <div class="date" id="data">--/--/----</div>
      </div>
      <div class="card">
        <div class="row"><span>Connessione</span><span><i class="dot" id="wifiDot"></i><span id="wifi">In attesa</span></span></div>
        <div class="row"><span>Sensore DS18B20</span><span><i class="dot" id="sensorDot"></i><span id="sensor">In attesa</span></span></div>
        <div class="row"><span>Acceso da</span><span id="uptime">--</span></div>
        <div class="row"><span>Campioni nel grafico</span><span id="nsamples">0</span></div>
      </div>
    </div>
  </div>
  <div class="footer">ESP32 · DS18B20 · DS3231 · SMART CLASSROOM</div>
</section>

<script>
const $=id=>document.getElementById(id);
let data=[];            // {t: ms epoch, v: °C}
let range=900000;
let target=null, disp=null;
let hue=190, hoverX=null, series=[], dirty=true, yLo=null, yHi=null;
let busy=false, lastUp=0;

/* ---------- sfondo: scroll + parallax ---------- */
function onScroll(){
  const max=document.documentElement.scrollHeight-innerHeight;
  const p=max>0?scrollY/max:0;
  document.documentElement.style.setProperty('--p',p.toFixed(3));
  $('o1').style.transform='translate3d('+(p*200)+'px,'+(scrollY*.25)+'px,0)';
  $('o2').style.transform='translate3d('+(-p*220)+'px,'+(-scrollY*.2)+'px,0)';
  $('o3').style.transform='translate3d('+(-p*120)+'px,'+(scrollY*.35)+'px,0)';
}
addEventListener('scroll',onScroll,{passive:true});onScroll();

/* ---------- comparsa delle sezioni ---------- */
const io=new IntersectionObserver(es=>es.forEach(e=>{if(e.isIntersecting)e.target.classList.add('in')}),{threshold:.12});
document.querySelectorAll('.reveal').forEach(el=>io.observe(el));

/* ---------- tabs intervallo ---------- */
$('tabs').addEventListener('click',e=>{
  const b=e.target.closest('button');if(!b)return;
  document.querySelectorAll('#tabs button').forEach(x=>x.classList.remove('on'));
  b.classList.add('on');range=+b.dataset.r;dirty=true;yLo=null;
});

/* ---------- grafico canvas ---------- */
const cv=$('chart'),ctx=cv.getContext('2d');
let W=0,H=0;
function resize(){
  const dpr=devicePixelRatio||1,r=cv.getBoundingClientRect();
  W=r.width;H=r.height;cv.width=W*dpr;cv.height=H*dpr;ctx.setTransform(dpr,0,0,dpr,0,0);
}
addEventListener('resize',resize);resize();
cv.addEventListener('pointermove',e=>{hoverX=e.clientX-cv.getBoundingClientRect().left});
cv.addEventListener('pointerdown',e=>{hoverX=e.clientX-cv.getBoundingClientRect().left});
cv.addEventListener('pointerleave',()=>{hoverX=null});
cv.addEventListener('pointerup',e=>{if(e.pointerType!=='mouse')hoverX=null});

function fmtT(t,sec){
  return new Date(t).toLocaleTimeString('it-IT',sec?{hour:'2-digit',minute:'2-digit',second:'2-digit'}:{hour:'2-digit',minute:'2-digit'});
}

// riduce i punti in "bin" di tempo stabili (niente sfarfallio mentre il grafico scorre)
function build(){
  const t0=Date.now()-range-range*.05;
  const pts=data.filter(p=>p.t>=t0);
  const N=70,bin=range/N,m=new Map();
  for(const p of pts){
    const k=Math.floor(p.t/bin);let b=m.get(k);
    if(!b){b={s:0,n:0,ts:0};m.set(k,b)}
    b.s+=p.v;b.n++;b.ts+=p.t;
  }
  const out=[];
  [...m.keys()].sort((a,b)=>a-b).forEach(k=>{const b=m.get(k);out.push({t:b.ts/b.n,v:b.s/b.n})});
  return out;
}

function curve(pts){
  ctx.moveTo(pts[0].x,pts[0].y);
  for(let i=0;i<pts.length-1;i++){
    const p0=pts[Math.max(0,i-1)],p1=pts[i],p2=pts[i+1],p3=pts[Math.min(pts.length-1,i+2)];
    const k=1/6;
    ctx.bezierCurveTo(p1.x+(p2.x-p0.x)*k,p1.y+(p2.y-p0.y)*k,p2.x-(p3.x-p1.x)*k,p2.y-(p3.y-p1.y)*k,p2.x,p2.y);
  }
}

function draw(){
  const now=Date.now(),t0=now-range;
  ctx.clearRect(0,0,W,H);
  if(dirty){series=build();dirty=false}
  const padL=46,padR=16,padT=16,padB=30,pw=W-padL-padR,ph=H-padT-padB;
  const acc='hsl('+hue+',90%,66%)';

  // asse Y animato
  if(series.length){
    let lo=Infinity,hi=-Infinity;
    for(const p of series){if(p.v<lo)lo=p.v;if(p.v>hi)hi=p.v}
    if(hi-lo<1.5){const m=(hi+lo)/2;lo=m-.75;hi=m+.75}
    lo-=.2;hi+=.2;
    if(yLo===null){yLo=lo;yHi=hi}
    yLo+=(lo-yLo)*.08;yHi+=(hi-yHi)*.08;
  }
  if(yLo===null){yLo=20;yHi=25}
  const X=t=>padL+(t-t0)/range*pw;
  const Y=v=>padT+(1-(v-yLo)/(yHi-yLo))*ph;

  // griglia
  ctx.font='11px Segoe UI,Arial';ctx.lineWidth=1;
  for(let k=0;k<=4;k++){
    const y=padT+ph*k/4;
    ctx.strokeStyle='rgba(255,255,255,.09)';ctx.beginPath();ctx.moveTo(padL,y);ctx.lineTo(W-padR,y);ctx.stroke();
    ctx.fillStyle='#8796bc';ctx.textAlign='right';ctx.fillText((yHi-(yHi-yLo)*k/4).toFixed(1),padL-8,y+4);
  }
  ctx.textAlign='center';
  for(let k=0;k<=3;k++){
    const t=t0+range*k/3;
    ctx.fillStyle='#8796bc';
    ctx.textAlign=k===0?'left':(k===3?'right':'center');
    ctx.fillText(fmtT(t,range<=300000),X(t),H-8);
  }

  if(series.length<2){
    ctx.fillStyle='#c4c9e8';ctx.textAlign='center';ctx.font='14px Segoe UI,Arial';
    ctx.fillText('Raccolta dati in corso...',W/2,H/2);
    return;
  }

  const pts=series.map(p=>({x:X(p.t),y:Y(p.v),t:p.t,v:p.v}));
  ctx.save();
  ctx.beginPath();ctx.rect(padL,0,pw+padR,H);ctx.clip();

  // area
  const g=ctx.createLinearGradient(0,padT,0,padT+ph);
  g.addColorStop(0,'hsla('+hue+',90%,66%,.38)');g.addColorStop(1,'hsla('+hue+',90%,66%,0)');
  ctx.beginPath();curve(pts);
  ctx.lineTo(pts[pts.length-1].x,padT+ph);ctx.lineTo(pts[0].x,padT+ph);ctx.closePath();
  ctx.fillStyle=g;ctx.fill();

  // linea con bagliore
  ctx.beginPath();curve(pts);
  const lg=ctx.createLinearGradient(padL,0,W-padR,0);
  lg.addColorStop(0,'#a78bfa');lg.addColorStop(1,acc);
  ctx.strokeStyle=lg;ctx.lineWidth=3.2;ctx.lineCap='round';ctx.lineJoin='round';
  ctx.shadowColor=acc;ctx.shadowBlur=14;ctx.stroke();ctx.shadowBlur=0;
  ctx.restore();

  // punto "adesso" pulsante
  const last=pts[pts.length-1];
  const pulse=(Math.sin(now/350)+1)/2;
  ctx.beginPath();ctx.arc(last.x,last.y,6+pulse*7,0,6.283);ctx.fillStyle='hsla('+hue+',90%,66%,'+(.28-pulse*.2)+')';ctx.fill();
  ctx.beginPath();ctx.arc(last.x,last.y,5,0,6.283);ctx.fillStyle='#fff';ctx.fill();

  // crosshair interattivo
  if(hoverX!==null&&hoverX>=padL&&hoverX<=W-padR){
    let best=pts[0];
    for(const p of pts){if(Math.abs(p.x-hoverX)<Math.abs(best.x-hoverX))best=p}
    ctx.strokeStyle='rgba(255,255,255,.35)';ctx.setLineDash([4,4]);
    ctx.beginPath();ctx.moveTo(best.x,padT);ctx.lineTo(best.x,padT+ph);ctx.stroke();ctx.setLineDash([]);
    ctx.beginPath();ctx.arc(best.x,best.y,6,0,6.283);ctx.fillStyle=acc;ctx.fill();
    ctx.lineWidth=2;ctx.strokeStyle='#fff';ctx.stroke();
    const txt=best.v.toFixed(1)+' °C',tm=fmtT(best.t,true);
    ctx.font='600 14px Segoe UI,Arial';const w=Math.max(ctx.measureText(txt).width,ctx.measureText(tm).width)+24;
    let bx=best.x-w/2;bx=Math.max(padL,Math.min(W-padR-w,bx));
    let by=best.y-62;if(by<2)by=best.y+14;
    ctx.fillStyle='rgba(15,18,50,.92)';ctx.strokeStyle='rgba(255,255,255,.25)';ctx.lineWidth=1;
    ctx.beginPath();ctx.roundRect(bx,by,w,46,12);ctx.fill();ctx.stroke();
    ctx.fillStyle='#fff';ctx.textAlign='center';ctx.fillText(txt,bx+w/2,by+20);
    ctx.font='11px Segoe UI,Arial';ctx.fillStyle='#a5b4fc';ctx.fillText(tm,bx+w/2,by+37);
  }
}

/* ---------- ciclo di animazione ---------- */
function frame(){
  if(target!==null){
    if(disp===null)disp=target;
    disp+=(target-disp)*.08;
    $('temp').textContent=disp.toFixed(1);
  }
  draw();
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

/* ---------- comfort e colori ---------- */
function comfort(t){
  if(t<18)return['🥶 Freddo','Fa piuttosto freddo: valuta di accendere il riscaldamento o chiudere le finestre.'];
  if(t<20)return['🧥 Fresco','Temperatura un po\' bassa, ma accettabile con un maglioncino.'];
  if(t<=24)return['✅ Ideale','Condizioni ottimali per studiare e concentrarsi.'];
  if(t<=27)return['🌤️ Caldo','Fa caldo: apri le finestre per far circolare l\'aria.'];
  return['🔥 Molto caldo','Temperatura elevata: ventila l\'aula e fai bere gli studenti.'];
}
function updateStats(){
  const t0=Date.now()-range,pts=data.filter(p=>p.t>=t0);
  if(!pts.length)return;
  let mn=Infinity,mx=-Infinity,s=0;
  for(const p of pts){if(p.v<mn)mn=p.v;if(p.v>mx)mx=p.v;s+=p.v}
  $('sMin').textContent=mn.toFixed(1);$('sMax').textContent=mx.toFixed(1);$('sAvg').textContent=(s/pts.length).toFixed(1);
  const d=pts[pts.length-1].v-pts[0].v;
  $('sTrend').innerHTML=Math.abs(d)<.15?'→ <small>stabile</small>':(d>0?'↗ +':'↘ ')+d.toFixed(1)+'<small> °C</small>';
  $('nsamples').textContent=data.length;
}
function fmtUp(ms){
  const s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor(s%3600/60);
  return h?h+' h '+m+' min':m+' min '+(s%60)+' s';
}

/* ---------- dati dal dispositivo ---------- */
async function caricaStorico(){
  try{
    const r=await fetch('/history');if(!r.ok)return;
    const d=await r.json(),base=Date.now();
    data=d.p.map(a=>({t:base-(d.up-a[0]),v:a[1]}));
    dirty=true;updateStats();
  }catch(e){}
}

async function aggiorna(){
  if(busy)return;busy=true;
  try{
    const r=await fetch('/data');
    if(!r.ok)throw new Error('HTTP');
    const d=await r.json();
    $('ora').textContent=d.ora;$('data').textContent=d.data;
    $('uptime').textContent=fmtUp(d.up);
    const ok=d.temp!==null&&Number.isFinite(d.temp);
    $('sensor').textContent=ok?'Operativo':'Non disponibile';
    $('sensorDot').style.background=$('sensorDot').style.color=ok?'#4ade80':'#f87171';
    $('wifi').textContent='ESP32 raggiungibile';
    $('wifiDot').style.background=$('wifiDot').style.color='#4ade80';
    if(ok){
      target=d.temp;
      data.push({t:Date.now(),v:d.temp});
      const lim=Date.now()-3600000;
      while(data.length&&data[0].t<lim)data.shift();
      dirty=true;updateStats();
      hue=Math.round(210-Math.max(0,Math.min(1,(d.temp-15)/15))*190);
      document.documentElement.style.setProperty('--h',hue);
      const c=comfort(d.temp);
      $('badge').textContent=c[0];$('comfortText').textContent=c[1];
      $('tempNote').textContent='Temperatura rilevata correttamente';
      $('marker').style.left='calc('+(Math.max(10,Math.min(35,d.temp))-10)/25*100+'% - 3px)';
      $('chartNow').textContent='Adesso: '+d.temp.toFixed(1)+' °C';
    }else{
      $('tempNote').textContent='Sensore non disponibile';
      $('badge').textContent='⚠️ Nessun dato';
    }
    if(lastUp<0){lastUp=0;caricaStorico()}
  }catch(e){
    $('wifi').textContent='Non raggiungibile';
    $('wifiDot').style.background=$('wifiDot').style.color='#f87171';
    lastUp=-1;
  }
  busy=false;
}

caricaStorico().then(()=>{aggiorna();setInterval(aggiorna,1000)});
</script>
</body>
</html>
)rawliteral";

bool oraValida() {
  return time(nullptr) > 1700000000;
}

void onNtpSync(struct timeval* tv) {
  ntpSincronizzato = true;
}

void gestisciHome() {
  server.send_P(200, "text/html", PAGINA);
}

void gestisciDati() {
  String json = "{\"temp\":";
  if (isnan(temperatura)) json += "null";
  else json += String(temperatura, 1);

  char oraBuf[16] = "--:--:--";
  char dataBuf[24] = "ora non impostata";

  if (oraValida()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    strftime(oraBuf, sizeof(oraBuf), "%H:%M:%S", &t);
    strftime(dataBuf, sizeof(dataBuf), "%d/%m/%Y", &t);
  }

  json += ",\"ora\":\"" + String(oraBuf) + "\",\"data\":\"" + String(dataBuf) + "\"";
  json += ",\"up\":" + String(millis()) + "}";
  server.send(200, "application/json", json);
}

// Invia lo storico a blocchi, senza costruire un unico String enorme.
void gestisciStorico() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  String chunk = "{\"up\":" + String(millis()) + ",\"p\":[";
  int start = (histHead - histCount + MAX_STORICO) % MAX_STORICO;

  for (int i = 0; i < histCount; i++) {
    int idx = (start + i) % MAX_STORICO;
    if (i > 0) chunk += ",";
    chunk += "[" + String(histT[idx]) + "," + String(histV[idx], 1) + "]";
    if (chunk.length() > 1200) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  chunk += "]}";
  server.sendContent(chunk);
  server.sendContent("");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.setResolution(11);

  Wire.begin(21, 22);
  rtcPresente = rtc.begin();

  if (rtcPresente && !rtc.lostPower()) {
    struct timeval tv = { (time_t)rtc.now().unixtime(), 0 };
    settimeofday(&tv, nullptr);
    Serial.println("Ora caricata dall'RTC");
  } else {
    Serial.println("RTC assente o senza batteria: attendo NTP");
  }

  // NTP funziona in modalità client solo se la rete offre Internet.
  sntp_set_time_sync_notification_cb(onNtpSync);
  configTzTime(TZ_ITALIA, "pool.ntp.org", "time.google.com");

#if defined(MODALITA_CLIENT)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

#elif defined(MODALITA_ACCESS_POINT)
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAP(AP_SSID, AP_PASS)) {
    Serial.println("Errore nell'avvio dell'Access Point");
  } else {
    Serial.print("Rete Wi-Fi: ");
    Serial.println(AP_SSID);
    Serial.print("Pagina web: http://");
    Serial.println(WiFi.softAPIP());
  }
#endif

  server.on("/", gestisciHome);
  server.on("/data", gestisciDati);
  server.on("/history", gestisciStorico);
  server.begin();
}

void loop() {
  server.handleClient();

#if defined(MODALITA_CLIENT)
  bool connesso = (WiFi.status() == WL_CONNECTED);
  digitalWrite(PIN_LED, connesso ? LOW : HIGH);

  static bool ipStampato = false;

  if (connesso && !ipStampato) {
    Serial.print("Pagina web: http://");
    Serial.println(WiFi.localIP());
    ipStampato = true;
  } else if (!connesso) {
    ipStampato = false;
  }

  if (!connesso && millis() - tUltimoReconnect >= 10000) {
    tUltimoReconnect = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

#elif defined(MODALITA_ACCESS_POINT)
  digitalWrite(PIN_LED, LOW);
#endif

  // Aggiorna il DS3231 dopo una sincronizzazione NTP.
  if (ntpSincronizzato) {
    ntpSincronizzato = false;

    if (rtcPresente) {
      rtc.adjust(DateTime((uint32_t)time(nullptr)));
      Serial.println("RTC aggiornato da NTP");
    }
  }

  // Lettura DS18B20 non bloccante circa ogni 400 ms.
  if (!inConversione && millis() - tRichiesta >= INTERVALLO_LETTURA) {
    sensors.requestTemperatures();
    tRichiesta = millis();
    inConversione = true;
  }
  else if (inConversione && millis() - tRichiesta >= 400) {
    float t = sensors.getTempCByIndex(0);

    temperatura = (t == DEVICE_DISCONNECTED_C || t == 85.0f ||
                   t < -55.0f || t > 125.0f) ? NAN : t;

    inConversione = false;

    if (isnan(temperatura)) {
      Serial.println("Sensore DS18B20 non disponibile o lettura non valida");
    } else {
      Serial.printf("Temperatura: %.1f C\n", temperatura);

      // Salva un campione nello storico ogni 5 secondi.
      if (millis() - tUltimoCampione >= INTERVALLO_STORICO) {
        tUltimoCampione = millis();
        histV[histHead] = temperatura;
        histT[histHead] = (uint32_t)millis();
        histHead = (histHead + 1) % MAX_STORICO;
        if (histCount < MAX_STORICO) histCount++;
      }
    }
  }
}
