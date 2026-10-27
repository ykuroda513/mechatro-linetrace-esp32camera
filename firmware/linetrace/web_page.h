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
<div class="card"><b>モータテスト</b>（ライン走行は止まります。指定時間で自動停止）
  <div id="tparams">読み込み中...</div>
  <div class="stat">速度FBのON/OFFは下のパラメータ「速度FB」に従います。テスト設定もロボット側に保存されます</div>
  <div class="row"><button id="mrun">回す</button><button id="mstop">止める</button><button id="mlog">ログ表示</button></div>
  <div class="row" style="margin-top:6px"><button id="emark">ENCカウント0に (手回し確認用)</button></div>
  <div class="row" style="margin-top:6px"><button id="mcal">最高速度を測る (duty100%で1.5秒)</button></div>
  <canvas id="chart" width="600" height="320" style="margin-top:8px;background:#000"></canvas>
  <div class="stat">上=左車輪 下=右車輪 / 白破線=目標 色実線=実測 灰=duty / 横軸3秒</div>
  <div class="stat" id="lsum"></div>
</div>
<div class="card"><div id="params"></div>
  <div class="row" style="margin-top:8px"><button id="save">NVSに保存</button><button id="reset">初期値に戻す</button></div>
  <div class="stat" id="savestate"></div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
const cv=$('cv'),cx=cv.getContext('2d');
let st=null;
async function api(p){const r=await fetch(p,{cache:'no-store'});if(!r.ok)throw r.status;return r;}
$('start').onclick=()=>api('/run?on=1');
$('stop').onclick=()=>api('/stop');
// ---- パラメータ: 表示は常にロボット側の値 (/params) に合わせる ----
const pending=new Map();   // key -> {timer, value}  まだ送っていない変更
function queueSet(key,val){
  const old=pending.get(key);if(old)clearTimeout(old.timer);
  const timer=setTimeout(()=>{pending.delete(key);api(`/set?${key}=${val}`);},120);
  pending.set(key,{timer,value:val});
}
async function flush(){   // 未送信の変更を今すぐ送る (保存・テスト開始の前に必ず呼ぶ)
  const jobs=[];
  for(const [key,{timer,value}] of pending){clearTimeout(timer);jobs.push(api(`/set?${key}=${value}`));}
  pending.clear();await Promise.all(jobs);
}
$('mrun').onclick=async()=>{await flush();
  const r=await (await api('/motor')).json();   // 引数なし = ロボットに保存されたテスト設定で回す
  $('lsum').textContent=`送信: 左${r.l} 右${r.r} mm/s ${r.rev>0?r.rev+'回転':r.ms+'ms'}`;
  if(!(r.rev>0))setTimeout(drawLog,r.ms+300);};
$('mstop').onclick=()=>api('/stop');
$('mlog').onclick=()=>drawLog();
$('emark').onclick=()=>api('/encmark');
$('mcal').onclick=async()=>{if(!confirm('左右のモータを全速で1.5秒回します。車輪を浮かせるか、走れる場所に置いてください'))return;
  await api('/calib');setTimeout(async()=>{await drawLog();loadParams();},1900);};
async function drawLog(){
  const g=await (await api('/log')).json(),c=$('chart'),x=c.getContext('2d'),W=c.width,H=c.height;
  x.fillStyle='#000';x.fillRect(0,0,W,H);
  if(!g.n){$('lsum').textContent='ログなし';return;}
  // 縦軸: 目標・実測の最大値に合わせる (mm/s)。dutyは右側の軸 0..100%
  const all=[...g.tl,...g.tr,...g.sl,...g.sr].map(Math.abs);
  const R=Math.max(100,Math.ceil(Math.max(...all)*1.2/50)*50);
  const X=i=>i*W/300,PH=H/2;
  const avg=(a,from)=>{const b=a.slice(from);return b.reduce((p,q)=>p+q,0)/Math.max(1,b.length);};
  const from=Math.max(0,g.n-100);   // 最後の1秒 (定常値)
  const sides=[['左',g.tl,g.sl,g.dl,'#56ccf2'],['右',g.tr,g.sr,g.dr,'#f2c94c']];
  let sum='最後の1秒の平均:  ';
  sides.forEach(([name,t,y,d,col],k)=>{
    const top=k*PH,Y=v=>top+PH-6-(v/R)*(PH-20),YD=v=>top+PH-6-(v/1000)*(PH-20);
    x.strokeStyle='#444';x.lineWidth=1;x.setLineDash([]);
    x.beginPath();x.moveTo(0,Y(0));x.lineTo(W,Y(0));x.stroke();
    x.strokeStyle='#222';for(const v of [R/2,R]){x.beginPath();x.moveTo(0,Y(v));x.lineTo(W,Y(v));x.stroke();}
    const line=(a,f,col,w,dash)=>{x.strokeStyle=col;x.lineWidth=w;x.setLineDash(dash);x.beginPath();
      a.forEach((v,i)=>i?x.lineTo(X(i),f(v)):x.moveTo(X(i),f(v)));x.stroke();x.setLineDash([]);};
    line(d,YD,'#777',1,[]);            // duty (右軸 %)
    line(t,Y,'#fff',1.5,[6,4]);        // 目標
    line(y,Y,col,2.5,[]);              // 実測
    x.fillStyle=col;x.font='12px monospace';
    x.fillText(`${name}  縦軸 0〜${R} mm/s   灰=duty 0〜100%`,4,top+14);
    const at=avg(t,from),ay=avg(y,from),ad=avg(d,from)/10;
    sum+=`${name} 目標${at.toFixed(0)} 実測${ay.toFixed(0)} mm/s duty${ad.toFixed(0)}%   `;
  });
  $('lsum').textContent=sum;
}
$('save').onclick=async()=>{await flush();await api('/save');await loadParams();alert('保存しました');};
$('reset').onclick=async()=>{pending.forEach(p=>clearTimeout(p.timer));pending.clear();await api('/reset');loadParams();};

let paramsBuilt=false;
async function loadParams(){
  const ps=await (await api('/params')).json();
  if(!paramsBuilt){
    const box=$('params'),tbox=$('tparams');box.innerHTML='';tbox.innerHTML='';
    for(const p of ps){
      const d=document.createElement('div');d.className='p';
      d.innerHTML=`<label>${p.label.replace(/^テスト /,'')}</label><input type=range autocomplete=off min=${p.min} max=${p.max} step=${p.step}><output></output>`;
      const s=d.querySelector('input'),o=d.querySelector('output');
      s.dataset.key=p.key;
      s.oninput=()=>{o.textContent=s.value;queueSet(p.key,s.value);};
      (p.key.startsWith('t_')?tbox:box).appendChild(d);
    }
    paramsBuilt=true;
  }
  // 値の反映: 操作中・未送信のものは上書きしない
  for(const p of ps){
    const s=document.querySelector(`input[data-key="${p.key}"]`);if(!s)continue;
    if(pending.has(p.key)||document.activeElement===s)continue;
    s.value=p.value;s.nextElementSibling.textContent=p.value;
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

let lastBoot=null,lastSync=0;
function showStat(s){
  if(lastBoot!==null&&s.boot!==lastBoot){paramsBuilt&&loadParams();$('lsum').textContent='ロボットが再起動しました。設定を読み直しました';}
  lastBoot=s.boot;
  if(Date.now()-lastSync>3000){lastSync=Date.now();loadParams().catch(()=>{});}   // 他の端末での変更や自動設定も反映
  $('savestate').textContent=s.unsaved?'● 未保存の変更があります (電源を切ると元に戻ります)':'保存済み';
  $('savestate').style.color=s.unsaved?'#f2994a':'#999';
  $('state').textContent=s.running?'RUNNING':'STOPPED';
  $('state').style.color=s.running?'#27ae60':'#eb5757';
  const c=s.ctrl;
  let t=`${s.net.mode.toUpperCase()} ${s.net.ip} (${s.net.host}.local)${s.net.mode==='sta'?'  RSSI '+s.net.rssi+'dBm':''}\n`;
  if(!s.camera)t+='!! カメラ初期化失敗 !!\n';
  t+=`fps ${s.fps.toFixed(1)}  frame ${s.frame}   輝度 平均${s.luma.mean} 最大${s.luma.max}${s.luma.mean>235?'  ← 白飛び':''}${s.luma.mean<20?'  ← 暗すぎ':''}\n`;
  t+=`err ${c.err.toFixed(3)}  P ${c.p.toFixed(3)}  D ${c.d.toFixed(3)}  ${c.lost?'LOST':''}\n`;
  t+=`ライン制御の出力 L ${c.l.toFixed(0)} R ${c.r.toFixed(0)} mm/s   VM ${s.vm.toFixed(2)}V\n`;
  const d=s.drv,rpm=v=>(v/(Math.PI*s.enc.wheel)*60).toFixed(0);
  t+=`DRV ${d.active?(d.calib?'CALIB':d.test?'TEST':'RUN'):'stop'}  速度FB ${d.fb?'ON':'OFF'}  最高速度 L${d.vmax[0]} R${d.vmax[1]} mm/s\n`;
  t+=`  目標 L${d.tl} R${d.tr}  実測 L${d.sl} R${d.sr} mm/s (L${rpm(d.sl)} R${rpm(d.sr)} rpm)  duty L${d.dl.toFixed(2)} R${d.dr.toFixed(2)}\n`;
  if(d.cal[0]||d.cal[1])t+=`  前回の最高速度計測: L${d.cal[0]} R${d.cal[1]} mm/s\n`;
  const e=s.enc;
  t+=`ENC 基準から L ${e.dl} (${(e.dl/e.ppr).toFixed(2)}回転)  R ${e.dr} (${(e.dr/e.ppr).toFixed(2)}回転)   1回転=${e.ppr}\n`;
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
