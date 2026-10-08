// =============================================================
//  web_page.h  調整用Webページ (スマホ/PCのブラウザで開く)
// =============================================================
#pragma once

static const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LineTrace</title>
<style>
:root{--bg:#111;--fg:#eee;--sub:#999;--acc:#2d9cdb;--ok:#27ae60;--ng:#eb5757;--card:#1c1c1c}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:720px;margin:0 auto;padding:12px}
h1{font-size:18px;margin:4px 0 10px}
.card{background:var(--card);border-radius:10px;padding:10px;margin-bottom:10px}
canvas{width:100%;image-rendering:pixelated;border-radius:6px;background:#000;display:block}
.row{display:flex;gap:8px;flex-wrap:wrap}
button{flex:1;min-width:90px;padding:14px 8px;font-size:16px;border:0;border-radius:8px;color:#fff;background:#444}
#start{background:var(--ok)}#stop{background:var(--ng)}
.stat{font:13px ui-monospace,monospace;color:var(--sub);white-space:pre;overflow-x:auto}
.p{display:grid;grid-template-columns:9em 1fr 4.5em;gap:8px;align-items:center;margin:6px 0;font-size:14px}
.p input[type=range]{width:100%}.p output{text-align:right;font-variant-numeric:tabular-nums}
#state{font-weight:bold}
</style></head><body><main>
<h1>LineTrace <span id="state">--</span></h1>
<div class="card"><canvas id="cv" width="160" height="120"></canvas></div>
<div class="card row">
  <button id="start">START</button><button id="stop">STOP</button>
</div>
<div class="card"><div class="stat" id="stat">接続中...</div></div>
<div class="card"><div id="params"></div>
  <div class="row" style="margin-top:8px"><button id="save">NVSに保存</button><button id="reset">初期値に戻す</button></div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
const cv=$('cv'),cx=cv.getContext('2d');
let st=null;
async function api(p){const r=await fetch(p,{cache:'no-store'});if(!r.ok)throw r.status;return r;}
$('start').onclick=()=>api('/run?on=1');
$('stop').onclick=()=>api('/run?on=0');
$('save').onclick=async()=>{await api('/save');alert('保存しました');};
$('reset').onclick=async()=>{await api('/reset');loadParams();};

async function loadParams(){
  const ps=await (await api('/params')).json();
  const box=$('params');box.innerHTML='';
  for(const p of ps){
    const d=document.createElement('div');d.className='p';
    d.innerHTML=`<label>${p.label}</label><input type=range min=${p.min} max=${p.max} step=${p.step} value=${p.value}><output>${p.value}</output>`;
    const s=d.querySelector('input'),o=d.querySelector('output');
    let t=null;
    s.oninput=()=>{o.textContent=s.value;clearTimeout(t);t=setTimeout(()=>api(`/set?${p.key}=${s.value}`),120);};
    box.appendChild(d);
  }
}

function draw(buf){
  const dv=new DataView(buf),w=dv.getUint16(0,true),h=dv.getUint16(2,true);
  if(cv.width!==w){cv.width=w;cv.height=h;}
  const px=new Uint8Array(buf,4),img=cx.createImageData(w,h);
  for(let i=0;i<w*h;i++){const v=px[i];img.data[i*4]=v;img.data[i*4+1]=v;img.data[i*4+2]=v;img.data[i*4+3]=255;}
  cx.putImageData(img,0,0);
  if(!st)return;
  st.bands.forEach((b,i)=>{
    cx.strokeStyle=['#2d9cdb','#f2c94c','#bb6bd9'][i];cx.lineWidth=1;
    cx.strokeRect(0.5,b.y0+0.5,w-1,b.y1-b.y0-1);
    if(b.found){
      const x=(b.pos+1)/2*(w-1),hw=b.width*w/2;
      cx.fillStyle='rgba(39,174,96,.35)';cx.fillRect(x-hw,b.y0,hw*2,b.y1-b.y0);
      cx.fillStyle='#27ae60';cx.fillRect(x-1,b.y0,2,b.y1-b.y0);
    }
  });
  // 偏差バー
  const e=st.ctrl.err,y=h-4;
  cx.fillStyle='#333';cx.fillRect(0,y,w,4);
  cx.fillStyle=st.ctrl.lost?'#eb5757':'#2d9cdb';
  cx.fillRect(w/2,y,e*w/2,4);
}

function showStat(s){
  $('state').textContent=s.running?'RUNNING':'STOPPED';
  $('state').style.color=s.running?'#27ae60':'#eb5757';
  const c=s.ctrl;
  let t=`${s.net.mode.toUpperCase()} ${s.net.ip} (${s.net.host}.local)${s.net.mode==='sta'?'  RSSI '+s.net.rssi+'dBm':''}\n`;
  if(!s.camera)t+='!! カメラ初期化失敗 !!\n';
  t+=`fps ${s.fps.toFixed(1)}  frame ${s.frame}\n`;
  t+=`err ${c.err.toFixed(3)}  P ${c.p.toFixed(3)}  D ${c.d.toFixed(3)}  ${c.lost?'LOST':''}\n`;
  t+=`L ${c.l.toFixed(2)}  R ${c.r.toFixed(2)}   VM ${s.vm.toFixed(2)}V\n`;
  const rpm=v=>s.enc.ppr>0?` (${(v*60/s.enc.ppr).toFixed(0)}rpm)`:'';
  t+=`ENC L ${s.enc.l} ${s.enc.sl.toFixed(0)}c/s${rpm(s.enc.sl)}  R ${s.enc.r} ${s.enc.sr.toFixed(0)}c/s${rpm(s.enc.sr)}\n`;
  s.bands.forEach((b,i)=>{t+=`band${i} ${b.found?'o':'x'} pos ${b.pos.toFixed(3)} w ${b.width.toFixed(2)} cov ${b.cov.toFixed(2)} C ${b.contrast} th ${b.thr}\n`;});
  $('stat').textContent=t;
}

async function loop(){
  for(;;){
    try{
      st=await (await api('/status')).json();showStat(st);
      if(st.camera)draw(await (await api('/frame')).arrayBuffer());
    }catch(e){$('stat').textContent='通信エラー: '+e;await new Promise(r=>setTimeout(r,500));}
    await new Promise(r=>setTimeout(r,60));
  }
}
loadParams();loop();
</script></body></html>
)HTML";
