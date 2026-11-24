const $=s=>document.querySelector(s),DAYS=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
const OFFER=[540,600,660,720,840,900,960];
const lbl=m=>{const h=m/60|0;return String(h%12||12).padStart(2,'0')+':'+String(m%60).padStart(2,'0')+' '+(h>=12?'PM':'AM')};
const sleep=ms=>new Promise(r=>setTimeout(r,ms));

class N{constructor(k){this.k=k;this.h=1;this.l=this.r=null}}
const H=n=>n?n.h:0,U=n=>{n.h=1+Math.max(H(n.l),H(n.r))};
const rr=y=>{const x=y.l;y.l=x.r;x.r=y;U(y);U(x);return x};
const rl=x=>{const y=x.r;x.r=y.l;y.l=x;U(x);U(y);return y};
function bal(n){U(n);const b=H(n.l)-H(n.r);
 if(b>1){if(H(n.l.l)<H(n.l.r))n.l=rl(n.l);return rr(n)}
 if(b<-1){if(H(n.r.r)<H(n.r.l))n.r=rr(n.r);return rl(n)}return n}
const ins=(n,k)=>!n?new N(k):k<n.k?(n.l=ins(n.l,k),bal(n)):k>n.k?(n.r=ins(n.r,k),bal(n)):n;
const has=(n,k)=>{while(n){if(k===n.k)return true;n=k<n.k?n.l:n.r}return false};
const mn=n=>{while(n.l)n=n.l;return n};
function del(n,k){if(!n)return null;
 if(k<n.k)n.l=del(n.l,k);else if(k>n.k)n.r=del(n.r,k);
 else{if(!n.l)return n.r;if(!n.r)return n.l;const s=mn(n.r);n.k=s.k;n.r=del(n.r,s.k)}
 return bal(n)}
const inorder=(n,a=[])=>{if(n){inorder(n.l,a);a.push(n.k);inorder(n.r,a)}return a};
const fresh=()=>OFFER.reduce(ins,null);

class Mx{constructor(){this.t=Promise.resolve()}
 lock(){let rel;const nx=new Promise(r=>rel=r),pv=this.t;this.t=pv.then(()=>nx);return pv.then(()=>{this.rel=rel})}
 unlock(){this.rel()}}

const docs=new Map([
 ['D201',{n:'Dr. A. Sharma',s:'Cardiology',d:['Monday','Wednesday','Friday']}],
 ['D202',{n:'Dr. R. Verma',s:'Dermatology',d:['Tuesday','Thursday','Friday']}],
 ['D203',{n:'Dr. P. Mehta',s:'Orthopedics',d:['Monday','Tuesday','Thursday']}],
 ['D204',{n:'Dr. N. Singh',s:'General Medicine',d:['Wednesday','Friday','Saturday']}],
 ['D205',{n:'Dr. S. Iyer',s:'Pediatrics',d:['Monday','Thursday','Saturday']}],
 ['D206',{n:'Dr. K. Nair',s:'Neurology',d:['Tuesday','Wednesday','Friday']}]]);
const pts=new Map([
 ['P101',{n:'Aarav Verma',a:34,ph:'+91-98110XXXXX',i:'Mild hypertension'}],
 ['P104',{n:'Rohan Gupta',a:29,ph:'+91-98111XXXXX',i:'No known allergies'}],
 ['P106',{n:'Sneha Rao',a:25,ph:'+91-98112XXXXX',i:'Migraine history'}],
 ['P109',{n:'Meera Kapoor',a:41,ph:'+91-98113XXXXX',i:'Routine consultation'}],
 ['P111',{n:'Kavita Singh',a:31,ph:'+91-98114XXXXX',i:'No known allergies'}],
 ['P113',{n:'Imran Khan',a:52,ph:'+91-98115XXXXX',i:'Type 2 diabetes follow-up'}],
 ['P114',{n:'Anjali Desai',a:8,ph:'+91-98116XXXXX',i:'Vaccination check'}],
 ['P116',{n:'Vikram Joshi',a:46,ph:'+91-98117XXXXX',i:'Knee pain'}],
 ['P118',{n:'Priya Nambiar',a:37,ph:'+91-98118XXXXX',i:'Skin allergy review'}]]);
const appts=new Map(),sched=new Map(),pslots=new Set();let ctr=500;

function pd(s){const m=/^(\d{4})-(\d{1,2})-(\d{1,2})$/.exec((s||'').trim());if(!m)return null;
 const y=+m[1],mo=+m[2],d=+m[3],t=new Date(Date.UTC(y,mo-1,d));
 if(t.getUTCFullYear()!==y||t.getUTCMonth()!==mo-1||t.getUTCDate()!==d)return null;
 return{s:t.toISOString().slice(0,10),wd:DAYS[t.getUTCDay()]}}
const gs=(did,ds)=>{const k=did+'|'+ds;if(!sched.has(k))sched.set(k,{root:fresh()});return sched.get(k)};
const E=m=>({ok:0,m});
function check(did,ds,key){const d=docs.get(did);if(!d)return E('Doctor not found.');
 const t=pd(ds);if(!t)return E('Enter a real calendar date.');
 if(!d.d.includes(t.wd))return E(d.n+' doesn’t work on '+t.wd+'s.');
 if(!OFFER.includes(key))return E('That time isn’t an offered slot.');return{ok:1,t}}

function book(pid,did,ds,key){
 if(!pts.has(pid))return E('Choose a registered patient.');
 const c=check(did,ds,key);if(!c.ok)return c;
 const s=gs(did,c.t.s);if(!has(s.root,key))return E('That slot is already booked.');
 const pk=pid+'|'+c.t.s+'|'+key;if(pslots.has(pk))return E('This patient already has an appointment then.');
 pslots.add(pk);s.root=del(s.root,key);
 const a={id:'APT'+(++ctr),pid,did,date:c.t.s,key,st:'CONFIRMED'};appts.set(a.id,a);log(`${a.id} booked: ${pts.get(pid).n} with ${docs.get(did).n}, ${a.date} ${lbl(key)}`);return{ok:1,a}}
function cancel(id){const a=appts.get(id);if(!a||a.st!=='CONFIRMED')return E('Appointment is not active.');
 const s=gs(a.did,a.date);s.root=ins(s.root,a.key);pslots.delete(a.pid+'|'+a.date+'|'+a.key);a.st='CANCELLED';log(`${a.id} cancelled and the slot is free again`);return{ok:1,a}}
function resched(id,ds,key){const a=appts.get(id);if(!a||a.st!=='CONFIRMED')return E('Appointment is not active.');
 const c=check(a.did,ds,key);if(!c.ok)return c;
 if(c.t.s===a.date&&key===a.key)return E('That is the current slot.');
 const ns=gs(a.did,c.t.s),os=gs(a.did,a.date);
 if(!has(ns.root,key))return E('New slot is busy. Your original appointment is kept.');
 const nk=a.pid+'|'+c.t.s+'|'+key;if(pslots.has(nk))return E('Patient already has an appointment then. Original kept.');
 pslots.add(nk);ns.root=del(ns.root,key);os.root=ins(os.root,a.key);pslots.delete(a.pid+'|'+a.date+'|'+a.key);
 a.date=c.t.s;a.key=key;log(`${a.id} moved to ${a.date} ${lbl(key)}`);return{ok:1,a}}

const LOG=[],KEY='hc-appt-v2';
const log=m=>{LOG.unshift({t:new Date().toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}),m});LOG.length=Math.min(LOG.length,8)};
function save(){try{localStorage.setItem(KEY,JSON.stringify({p:[...pts],a:[...appts.values()],c:ctr,l:LOG}))}catch(e){}}
function load(){try{const d=JSON.parse(localStorage.getItem(KEY));if(!d)return 0;d.p.forEach(([k,v])=>pts.set(k,v));
 d.a.forEach(a=>{appts.set(a.id,a);if(a.st==='CONFIRMED'){const s=gs(a.did,a.date);s.root=del(s.root,a.key);pslots.add(a.pid+'|'+a.date+'|'+a.key)}});ctr=d.c;LOG.push(...d.l);return 1}catch(e){return 0}}
function renderDash(){const c=[...appts.values()].filter(a=>a.st==='CONFIRMED').length;
 $('#stats').innerHTML=[[docs.size,'Doctors'],[pts.size,'Patients'],[c,'Confirmed visits'],[appts.size-c,'Cancelled']].map(([n,l])=>`<div class="stat"><b>${n}</b><span>${l}</span></div>`).join('');
 $('#act').innerHTML=LOG.length?LOG.map(l=>`<li><b>${l.t}</b>${esc(l.m)}</li>`).join(''):'<li>No activity yet. Book a visit to get started.</li>';
 const upcoming=[...appts.values()].filter(a=>a.st==='CONFIRMED').sort((a,b)=>(a.date+a.key)-(b.date+b.key)).slice(0,4);
 $('#dqueue').innerHTML=upcoming.length?upcoming.map((a,i)=>{
  const d=new Date(a.date+'T00:00:00Z');
  return`<div class="visit" style="animation-delay:${i*.07}s"><div class="visit-date"><span>${d.toLocaleDateString('en-US',{month:'short',timeZone:'UTC'})}</span><b>${d.getUTCDate()}</b></div><div><strong>${esc(pts.get(a.pid).n)}</strong><small>${esc(docs.get(a.did).n)} · ${lbl(a.key)}</small></div></div>`}).join(''):'<p class="empty-queue">No confirmed visits. Use Book to add the next appointment.</p>'}
function renderHM(){const B=7,b=Array.from({length:B},()=>[]);for(const id of pts.keys()){let h=5381;for(const c of id)h=((h<<5)+h)^c.charCodeAt(0);b[(h>>>0)%B].push(id)}
 $('#hm').innerHTML=b.map((c,i)=>`<div class="bk"><span>${i}</span>${c.map(x=>`<i>${x}</i>`).join('')||'<em>empty</em>'}</div>`).join('')}
function drawTree(r){const svg=$('#tsvg');if(!r){svg.setAttribute('viewBox','0 0 320 40');svg.innerHTML='<text x="10" y="24">All slots booked: the tree is empty.</text>';return}
 let i=0,e='',v='';(function w(n,d){if(!n)return;w(n.l,d+1);n.x=40+i++*84;n.y=30+d*62;w(n.r,d+1)})(r,0);
 (function w(n){for(const c of[n.l,n.r])if(c){e+=`<line x1="${n.x}" y1="${n.y}" x2="${c.x}" y2="${c.y}"/>`;w(c)}
  v+=`<g transform="translate(${n.x},${n.y})"><rect x="-38" y="-16" width="76" height="32" rx="16"/><text y="5" text-anchor="middle">${lbl(n.k).replace(':00 ',' ').replace(/^0/,'')}</text></g>`})(r);
 svg.setAttribute('viewBox',`0 0 ${i*84} ${H(r)*62+14}`);svg.innerHTML=e+v}
const el=(t,h)=>{const e=document.createElement(t);e.innerHTML=h;return e};
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let tt;function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(tt);tt=setTimeout(()=>t.classList.remove('on'),3200)}
const say=r=>{if(!r.ok)toast(r.m);return r.ok};
const TABS=[['dash','Dashboard'],['slots','Free slots'],['book','Book'],['appts','Appointments'],['patients','Patients'],['doctors','Doctors'],['demo','Lock demo']];
function show(id){document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id===id));
 document.querySelectorAll('#nav button').forEach(b=>b.setAttribute('aria-selected',b.dataset.t===id))}
TABS.forEach(([id,t])=>{const b=el('button',t);b.dataset.t=id;b.role='tab';b.onclick=()=>show(id);$('#nav').append(b)});
show('dash');

const today=new Date().toISOString().slice(0,10);
['#sdt','#bdt','#rsd'].forEach(s=>$(s).value=today);
const timeOpts=OFFER.map(k=>`<option value="${k}">${lbl(k)}</option>`).join('');
['#bt','#rst','#mt'].forEach(s=>$(s).innerHTML=timeOpts);

function renderLists(){save();renderHM();renderDash();
 const dopt=[...docs].map(([i,d])=>`<option value="${i}">${esc(d.n)} (${esc(d.s)})</option>`).join('');
 const keep=['#sd','#bd'].map(s=>$(s).value);
 $('#sd').innerHTML=$('#bd').innerHTML=dopt;$('#sd').value=keep[0]||'D201';$('#bd').value=keep[1]||'D201';
 const bp=$('#bp').value;
 $('#bp').innerHTML=[...pts].map(([i,p])=>`<option value="${i}">${i} · ${esc(p.n)}</option>`).join('');if(bp)$('#bp').value=bp;
 $('#ptb').innerHTML=[...pts].map(([i,p])=>`<tr><td>${i}</td><td>${esc(p.n)}</td><td>${p.a}</td><td>${esc(p.ph)}</td><td>${esc(p.i)}</td></tr>`).join('');
 $('#dgrid').innerHTML=[...docs].map(([i,d])=>`<div class="card"><h3>${esc(d.n)}</h3><p class="meta">${i} · ${esc(d.s)}</p><p>${d.d.join(', ')}</p><p class="meta">Hourly slots 09:00–12:00 and 02:00–04:00</p></div>`).join('');
 const q=($('#af').value||'').toLowerCase(),rows=[...appts.values()].filter(a=>(a.id+pts.get(a.pid).n+docs.get(a.did).n+a.date).toLowerCase().includes(q));
 $('#atb').innerHTML=rows.length?rows.map(a=>`<tr><td>${a.id}</td><td>${a.pid} ${esc(pts.get(a.pid).n)}</td><td>${esc(docs.get(a.did).n)}</td><td>${a.date} ${lbl(a.key)}</td><td><span class="tag ${a.st==='CANCELLED'?'c':''}">${a.st==='CONFIRMED'?'Confirmed':'Cancelled'}</span></td><td>${a.st==='CONFIRMED'?`<button class="b o s" data-r="${a.id}">Reschedule</button> <button class="b o s" data-c="${a.id}">Cancel</button>`:''}</td></tr>`).join(''):'<tr><td colspan="6">No appointments yet. Pick a free slot or use Book.</td></tr>';
 renderSlots()}

function renderSlots(){
 const did=$('#sd').value,d=docs.get(did),t=pd($('#sdt').value),g=$('#sgrid');g.innerHTML='';$('#tsvg').innerHTML='';
 if(!t){$('#sinfo').textContent='Choose a valid date.';$('#stree').textContent='';return}
 if(!d.d.includes(t.wd)){$('#sinfo').textContent=`${d.n} doesn’t work on ${t.wd}s. Working days: ${d.d.join(', ')}.`;$('#stree').textContent='';return}
 const s=gs(did,t.s),free=inorder(s.root);
 $('#sinfo').textContent=`${d.n}, ${t.wd} ${t.s}: ${free.length} of ${OFFER.length} slots free. Free slots are read from the tree in order.`;
 OFFER.forEach(k=>{const a=[...appts.values()].find(x=>x.did===did&&x.date===t.s&&x.key===k&&x.st==='CONFIRMED');
  const b=el('button',`<b>${lbl(k)}</b><small>${a?'Booked · '+esc(pts.get(a.pid).n):'Free · click to book'}</small>`);
  b.className='slot'+(a?' x':'');if(a)b.disabled=true;
  else b.onclick=()=>{$('#bd').value=did;$('#bdt').value=t.s;$('#bt').value=k;show('book');toast('Slot filled in. Choose a patient and book.')};
  g.append(b)});
 $('#stree').textContent=`Tree height ${H(s.root)} for ${free.length} free slots (a balanced tree keeps lookups fast).`;drawTree(s.root)}

$('#sd').onchange=$('#sdt').onchange=renderSlots;
$('#next').onclick=()=>{const did=$('#sd').value,d=docs.get(did);let t=pd($('#sdt').value);if(!t)return toast('Choose a valid date.');
 const dt=new Date($('#sdt').value+'T00:00:00Z');
 for(let i=0;i<30;i++,dt.setUTCDate(dt.getUTCDate()+1)){const x=pd(dt.toISOString().slice(0,10));if(!d.d.includes(x.wd))continue;
  const s=gs(did,x.s);if(s.root){$('#sdt').value=x.s;renderSlots();return toast(`Earliest free: ${x.s} at ${lbl(mn(s.root).k)}`)}}
 toast('No free slots in the next 30 days.')};
$('#bgo').onclick=()=>{const r=book($('#bp').value,$('#bd').value,$('#bdt').value,+$('#bt').value);
 if(say(r)){toast(`Confirmed ${r.a.id}: ${r.a.date} at ${lbl(r.a.key)}`);renderLists();show('appts')}};
$('#atb').onclick=e=>{const c=e.target.dataset.c,r=e.target.dataset.r;
 if(c&&say(cancel(c))){toast(c+' cancelled. Slot is free again.');renderLists()}
 if(r){$('#rsid').textContent=r;$('#rsd').value=appts.get(r).date;$('#rst').value=appts.get(r).key;$('#rs').showModal()}};
$('#rsgo').onclick=()=>{const id=$('#rsid').textContent,r=resched(id,$('#rsd').value,+$('#rst').value);
 if(say(r))toast(id+' moved to '+r.a.date+' at '+lbl(r.a.key));renderLists()};
$('#qgo').onclick=()=>{const p=pts.get($('#qid').value.trim().toUpperCase());
 $('#qres').textContent=p?`${$('#qid').value.trim().toUpperCase()}: ${p.n}, age ${p.a}, ${p.ph}. ${p.i}.`:'No patient with that ID.'};
$('#rgo').onclick=()=>{const id=$('#rid').value.trim().toUpperCase(),n=$('#rn').value.trim(),a=+$('#ra').value;
 if(!/^[A-Z0-9]{1,15}$/.test(id))return toast('ID must be 1–15 letters or digits.');
 if(pts.has(id))return toast('That patient ID already exists.');
 if(!n)return toast('Enter the patient’s name.');if(!(a>=0&&a<=120))return toast('Age must be 0–120.');
 pts.set(id,{n,a,ph:$('#rph').value||'-',i:$('#ri').value||'-'});['#rid','#rn','#ra','#rph','#ri'].forEach(s=>$(s).value='');
 toast('Registered '+id);renderLists()};

$('#mgo').onclick=async()=>{
 const key=+$('#mt').value,n=+$('#mn').value,useLock=$('#ml').value==='1',log=$('#mlog'),s={root:fresh(),mx:new Mx()},won=[];
 log.textContent=`${n} patients request ${lbl(key)} at once. Lock ${useLock?'ON':'OFF'}.\n`;$('#mgo').disabled=true;
 const names=[...pts.values()].map(p=>p.n);
 await Promise.all(Array.from({length:n},async(_,i)=>{
  if(useLock)await s.mx.lock();
  const free=has(s.root,key);await sleep(30+Math.random()*80);
  if(free){s.root=del(s.root,key);won.push(names[i]);log.innerHTML+=`${esc(names[i])}: <span class="ok">booked</span>\n`}
  else log.innerHTML+=`${esc(names[i])}: <span class="no">rejected, slot taken</span>\n`;
  if(useLock)s.mx.unlock()}));
 log.innerHTML+=won.length===1?`\n<span class="ok">Correct: exactly one booking.</span> The lock made check-and-book one step.`:`\n<span class="no">${won.length} bookings for one slot.</span> Without the lock, everyone passed the check before anyone updated the tree.`;
 $('#mgo').disabled=false};

const nd=(did,n=0)=>{const d=new Date(today+'T00:00:00Z');for(;;d.setUTCDate(d.getUTCDate()+1)){const x=pd(d.toISOString().slice(0,10));if(docs.get(did).d.includes(x.wd)&&!n--)return x.s}};
if(!load()){[['P101','D201',0,600],['P104','D201',0,840],['P106','D202',0,660],['P109','D203',1,540],['P111','D204',0,900],['P113','D206',0,720],['P114','D205',1,540]].forEach(([p,d,n,k])=>book(p,d,nd(d,n),k));LOG.length=0;log('Demo schedule loaded')}
$('#af').oninput=renderLists;
$('#rst0').onclick=()=>{try{localStorage.removeItem(KEY)}catch(e){}location.reload()};
renderLists();
