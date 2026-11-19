/* 春风交互原型。录音/引擎/模型均为模拟；不会发出任何 API 请求。 */
(() => {
  'use strict';
  const $ = (s, r = document) => r.querySelector(s);
  const esc = (value) => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const icon = window.ChunfeIcons.icon;
  const main = $('#main-content'), modalRoot = $('#modal-root'), toastRoot = $('#toast-root');
  const STORE = 'chunfeng-prototype-sessions-v1', CONFIG = 'chunfeng-prototype-config-v1', PROFILE = 'chunfeng-prototype-background-v1';
  const sampleMarkdown = "# 背景资料\n\n## 身份与职责\n参与项目协作，负责整理需求、协调事项和跟进进度。\n\n## 业务资料\n当前正在讨论项目方案与交付安排，具体范围、负责人和时间以双方确认的信息为准。\n\n## 沟通偏好\n先确认共同目标，再说明思路和取舍。表达自然、简洁；不编造事实，不替任何一方作出未经确认的承诺。";
  const initialConfig = {endpoint:'https://api.example.com/v1',model:'gpt-4.1',name:'通用模型接口'};
  const newDraft = () => ({title:''});
  const blankBackground = () => ({name:'',text:'',updatedAt:''});
  const state = {route:'home',draft:newDraft(),background:blankBackground(),backgroundDraft:blankBackground(),backgroundDirty:false,startScenario:'success',config:{...initialConfig},key:'',engine:'ready',testState:'ready',testResult:'',testScenario:'success',recordId:null,recordTab:'questions',systemDevice:'系统默认输出',microphone:'系统默认麦克风',historySearch:'',overlay:null,preflight:null,transcriptionTest:null,fileLoading:false};
  let toastTimer, modalPreviousFocus, preflightTimers = [], engineTimer, connectionTimer, fileGeneration = 0, engineStart = null, engineGeneration = 0;
  const sampleRecords = [
  {
    "id": "sample-session",
    "title": "项目方案沟通",
    "createdAt": "2026-11-02T14:30:00+08:00",
    "endedAt": "2026-11-02T14:54:00+08:00",
    "durationSeconds": 1440,
    "backgroundName": "背景资料.md",
    "backgroundText": sampleMarkdown,
    "model": "gpt-4.1",
    "mode": "full",
    "isDemo": true,
    "transcript": [
      {
        "id": "m0-0-other",
        "role": "other",
        "text": "这次方案最需要先确认的是什么？",
        "time": "00:12"
      },
      {
        "id": "m0-0-me",
        "role": "me",
        "text": "我想先确认目标、交付范围和验收标准。",
        "time": "00:20"
      },
      {
        "id": "m0-1-other",
        "role": "other",
        "text": "我把刚才讨论的要点记下来了。",
        "time": "01:12"
      },
      {
        "id": "m0-2-other",
        "role": "other",
        "text": "请把下一步的安排和各自负责的事项整理一下。",
        "time": "02:12"
      },
      {
        "id": "m0-2-me",
        "role": "me",
        "text": "我们先列清交付项，再一起确认负责人和时间。",
        "time": "02:20"
      }
    ],
    "questions": [
      {
        "id": "q0-0",
        "kind": "question",
        "text": "这次方案最需要先确认的是什么？",
        "fullAnswer": "先确认目标、范围和验收方式，再讨论具体方案。\n\n可以这样回应：我想先确认这次要解决的核心问题、需要交付到什么程度，以及怎样判断方案达到了目标。把这三点对齐后，再讨论实现方式。\n\n如果信息还不完整，先列出待确认项，不替对方补充事实。",
        "createdAt": "2026-11-02T14:30:00+08:00"
      },
      {
        "id": "q0-2",
        "kind": "request",
        "text": "请把下一步的安排和各自负责的事项整理一下。",
        "fullAnswer": "把下一步拆成具体事项，并区分已确认和待确认的信息。\n\n可以这样回应：我们先列清交付项，再一起确认每项的负责人和时间点。还没有确定的部分先标为待确认，避免遗漏或误解。\n\n整理后复述一遍分工，确认双方理解一致。",
        "createdAt": "2026-11-02T14:30:00+08:00"
      }
    ]
  },
  {
    "id": "sample-session-2",
    "title": "交付安排讨论",
    "createdAt": "2026-11-01T14:30:00+08:00",
    "endedAt": "2026-11-01T14:54:00+08:00",
    "durationSeconds": 1440,
    "backgroundName": "背景资料.md",
    "backgroundText": sampleMarkdown,
    "model": "gpt-4.1",
    "mode": "full",
    "isDemo": true,
    "transcript": [
      {
        "id": "m1-0-other",
        "role": "other",
        "text": "这个时间可能来不及，而且现在的范围有点大。",
        "time": "00:12"
      },
      {
        "id": "m1-0-me",
        "role": "me",
        "text": "我们先核实时间约束，再讨论缩小范围或分阶段交付。",
        "time": "00:20"
      },
      {
        "id": "m1-1-other",
        "role": "other",
        "text": "我先把相关资料发到群里。",
        "time": "01:12"
      }
    ],
    "questions": [
      {
        "id": "q1-0",
        "kind": "objection",
        "text": "这个时间可能来不及，而且现在的范围有点大。",
        "fullAnswer": "先承认时间和范围上的顾虑，再讨论可以调整的部分。\n\n可以这样回应：理解你的担心。我们先核实时间约束，把必须完成和可以延后的内容分开，再比较缩小范围或分阶段交付的可行性。\n\n在工作量和资源还没核实前，不急着承诺新的日期。",
        "createdAt": "2026-11-02T14:30:00+08:00"
      }
    ]
  }
];
  let sessions = [];
  try { const saved = localStorage.getItem(STORE); sessions = saved ? JSON.parse(saved) : sampleRecords; if (!Array.isArray(sessions)) sessions = sampleRecords; const conf = JSON.parse(localStorage.getItem(CONFIG)||'null'); if(conf && typeof conf==='object') state.config={...initialConfig,endpoint:String(conf.endpoint||initialConfig.endpoint),model:String(conf.model||initialConfig.model)}; } catch { sessions=sampleRecords; }
  try { const profile = JSON.parse(localStorage.getItem(PROFILE)||'null'); if(profile && typeof profile.text==='string' && typeof profile.name==='string') state.background={name:profile.name,text:profile.text,updatedAt:String(profile.updatedAt||'')}; } catch {}
  state.backgroundDraft={...state.background};
  state.editConfig = {...state.config};
  const stamp = (value) => { const d=new Date(value); return Number.isNaN(d.getTime())?'未记录日期':new Intl.DateTimeFormat('zh-CN',{month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hour12:false,timeZone:'Asia/Shanghai'}).format(d).replaceAll('/','.'); };
  const transcriptTime = value => /T\d{2}:/.test(String(value)) ? new Intl.DateTimeFormat('zh-CN',{hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false,timeZone:'Asia/Shanghai'}).format(new Date(value)) : String(value||'');
  const minutes = (s) => `${Math.max(1,Math.round((s.durationSeconds||0)/60))} 分钟`;
  const currentModel = () => state.config.model || 'gpt-4.1';
  function saveSessions() { try{localStorage.setItem(STORE,JSON.stringify(sessions));return true;}catch{toast('浏览器存储空间不足，记录暂保留在当前页面，请先导出。','alert');return false;} }
  function upsertSession(session) { const clone=JSON.parse(JSON.stringify(session)); const idx=sessions.findIndex(s=>s.id===clone.id);if(idx>=0)sessions[idx]=clone;else sessions.unshift(clone);saveSessions(); }
  function toast(message,kind='checkCircle',action=null) { clearTimeout(toastTimer);toastRoot.innerHTML=`<div class="toast">${icon(kind)}<span>${esc(message)}</span>${action?'<button id="toast-action">撤销</button>':''}</div>`;if(action)$('#toast-action').onclick=()=>{action();toastRoot.innerHTML='';};toastTimer=setTimeout(()=>toastRoot.innerHTML='',action?8500:4200); }
  function nav() {
    const link = (id,i,label) => `<a href="#${id}" aria-label="${label}" class="nav-link ${(state.route===id||(state.route==='record'&&id==='history'))?'active':''}" ${state.route===id?'aria-current="page"':''}>${icon(i)}<span>${label}</span></a>`;
    $('#navigation').innerHTML = link('home','mic','开始对话') + link('history','history','对话记录') + '<div class="nav-separator"></div><div class="nav-group-label">设置</div>' + link('background','file','背景资料') + link('settings','sliders','模型连接') + link('engine','chip','音频与转写');

  }
  function render(){nav();main.classList.toggle('home-main',state.route==='home');main.classList.toggle('settings-main',['settings','engine','background'].includes(state.route));main.innerHTML=({home:homePage,background:backgroundPage,history:historyPage,settings:settingsPage,engine:enginePage,record:recordPage}[state.route]||homePage)();main.classList.remove('content-enter');void main.offsetWidth;main.classList.add('content-enter');}
  function navigate(route) { if(state.overlay){state.overlay.destroy();state.overlay=null;$('#overlay-root').hidden=true;$('#app-window').hidden=false;} closeModal();const [r,id]=route.split('/');state.route=['home','background','history','settings','engine','record'].includes(r)?r:'home';if(id){state.recordId=decodeURIComponent(id);state.recordTab='questions';}render();window.scrollTo(0,0); }
  function go(route){if(location.hash==='#'+route)navigate(route);else location.hash=route;}
  function header(title,subtitle,action='') {
    return `<div class="page-heading"><div><h1>${title}</h1><p class="page-subtitle">${subtitle}</p></div>${action}</div>`;
  }
  function backgroundPage() {
    const d=state.backgroundDraft;
    return `${header('背景资料','保存一次，之后的对话会自动使用。')}
      <section class="profile-card">
        <div class="section-top"><label class="field-label" for="background-text">相关资料与沟通偏好</label><span class="status-pill">${state.background.text?'已保存':'尚未设置'}</span></div>
        <textarea id="background-text" class="text-input background-editor" maxlength="200000" placeholder="填写身份、业务资料与沟通偏好，或导入 Markdown。">${esc(d.text)}</textarea>
        <input id="markdown-file" type="file" accept=".md,text/markdown,text/plain" hidden>
        <div class="profile-actions"><span class="save-state" id="background-save-state">${state.backgroundDirty?'有未保存的修改':esc(d.name||'背景资料可选')}</span><button class="btn btn-secondary" data-action="choose-file" ${state.fileLoading?'disabled':''}>${state.fileLoading?'读取中…':'导入 Markdown'}</button><button class="btn btn-primary" data-action="save-background" ${state.fileLoading?'disabled':''}>保存背景</button></div>
      </section>`;
  }
  function envStatus(){if(state.engine==='ready')return ['转写就绪',''];if(state.engine==='loading')return ['引擎启动中','warning'];if(state.engine==='error')return ['引擎启动失败','error'];return ['引擎未启动','warning'];}
  function homePage() {
    const [status,tone]=envStatus();
    return `<section class="launcher" aria-labelledby="launcher-title">
      <div class="launcher-icon">${icon('mic')}</div>
      <h1 class="launcher-heading" id="launcher-title">开始一段对话</h1>
      <p class="launcher-description">实时转写与回应建议</p>
      <button class="btn btn-primary launcher-start" data-action="start">开始对话</button>
      <div class="launcher-status" aria-label="对话准备状态">
        <button class="launcher-status-item" data-action="settings" aria-label="模型连接：${esc(currentModel())}">${icon('model')}<span>${esc(currentModel())}</span></button>
        <button class="launcher-status-item ${tone}" data-action="engine" aria-label="音频与转写：${status}">${icon('audio')}<span>${status}</span></button>
        <button class="launcher-status-item" data-action="background" aria-label="背景资料：${state.background.text?'已配置':'未设置'}">${icon('file')}<span>${state.background.text?'背景已配置':'未设置背景'}</span></button>
      </div>
    </section>`;
  }
  function settingsPage() {
    return `${header('模型连接','配置模型接口，之后的对话会自动沿用。')}
      <section class="settings-form">
        <div class="setting-row"><label for="api-endpoint">API 地址</label><div class="setting-control"><input class="text-input" id="api-endpoint" type="url" placeholder="https://api.example.com/v1" value="${esc(state.editConfig.endpoint)}"><p class="help-text">填写服务商提供的基础地址，包含版本路径。</p></div></div>
        <div class="setting-row"><label for="api-key">API Key</label><div class="setting-control input-with-action"><input class="text-input" id="api-key" type="password" autocomplete="off" placeholder="输入 API Key" value="${esc(state.key)}"><button class="icon-button" data-action="toggle-key" aria-label="显示 Key">${icon('eye')}</button></div></div>
        <div class="setting-row"><label for="model-name">模型名称</label><div class="setting-control"><input class="text-input" id="model-name" placeholder="例如：你的模型 ID" value="${esc(state.editConfig.model)}"></div></div>
        ${state.testResult?`<div class="setting-row"><span class="setting-label">连接状态</span><p class="connection-result ${state.testState==='error'?'error':''}" role="status">${esc(state.testResult)}</p></div>`:''}
        <hr class="section-divider">
        <div class="settings-actions"><button class="btn btn-secondary" data-action="test-connection" ${state.testState==='testing'?'disabled':''}>${state.testState==='testing'?'<span class="spinner"></span>正在测试…':'测试连接'}</button><button class="btn btn-primary" data-action="save-config">保存配置</button></div>
      </section>`;
  }
  function enginePage() {
    const [status,tone]=envStatus();
    const options=(values,current)=>values.map(value=>`<option ${value===current?'selected':''}>${value}</option>`).join('');
    return `${header('音频与转写','管理本地转写引擎与双方音频输入。')}
      <section class="settings-form">
        <div class="setting-row"><span class="setting-label">转写引擎</span><div class="engine-control"><span class="status-pill ${tone}">${status}</span><div class="engine-actions"><button class="btn btn-secondary" data-action="test-transcription">测试转写</button><button class="btn ${state.engine==='ready'?'btn-secondary':'btn-primary'}" data-action="toggle-engine" ${state.engine==='loading'?'disabled':''}>${state.engine==='ready'?'停止引擎':state.engine==='loading'?'正在启动…':'启动引擎'}</button></div></div></div>
        <hr class="section-divider">
        <div class="setting-row"><label for="system-device">对方的声音</label><div class="setting-control"><select id="system-device" aria-label="系统音频设备">${options(['系统默认输出','耳机输出'],state.systemDevice)}</select><p class="help-text">通过系统音频接收</p></div></div>
        <div class="setting-row"><label for="microphone-device">你的声音</label><div class="setting-control"><select id="microphone-device" aria-label="麦克风设备">${options(['系统默认麦克风','内置麦克风','外接麦克风'],state.microphone)}</select><p class="help-text">通过麦克风接收</p></div></div>
        <hr class="section-divider">
        <div class="setting-row"><span class="setting-label">输入状态</span><div class="input-status"><span>${state.engine==='ready'?'设备就绪，等待开始对话':'引擎停止，字幕暂不可用'}</span></div></div>
      </section>`;
  }
  function historyPage(){return `${header('对话记录','查看已保存的对话、回应建议。',`<button class="btn btn-primary" data-action="new">${icon('plus')}新建对话</button>`)}<div class="history-toolbar"><div class="search-field">${icon('search')}<input class="text-input" id="history-search" aria-label="搜索对话名称" placeholder="搜索对话名称" value="${esc(state.historySearch)}"></div><span class="record-count" id="record-count">${sessions.length} 次对话</span></div><div id="history-list" class="history-list">${historyRows()}</div>`;}
  function historyRows(){const filtered=sessions.filter(s=>s.title.toLowerCase().includes(state.historySearch.toLowerCase()));return filtered.length?filtered.map(s=>`<div class="history-card"><span class="record-icon">${icon('message')}</span><div class="record-main"><strong>${esc(s.title)}</strong><small>${stamp(s.createdAt)} · ${minutes(s)} · ${(s.questions||[]).filter(q=>!q.ignored && q.kind!=='statement').length} 条建议${!s.endedAt?' · 未结束':''}</small></div><div class="history-actions"><button class="btn btn-subtle btn-small" data-action="record" data-id="${esc(s.id)}">查看记录 ${icon('chevron')}</button><button class="icon-button" data-action="export" data-id="${esc(s.id)}" aria-label="导出 ${esc(s.title)}">${icon('download')}</button><button class="icon-button" data-action="delete" data-id="${esc(s.id)}" aria-label="删除 ${esc(s.title)}">${icon('trash')}</button></div></div>`).join(''):`<div class="empty-state">${icon('folder')}<h3>${state.historySearch?'没有找到匹配的对话':'第一段对话，从这里开始'}</h3><p>${state.historySearch?'试试其他关键词。':'开始对话后，文字记录会自动保存在这里。'}</p>${state.historySearch?'':'<button class="btn btn-secondary" data-action="new">开始对话</button>'}</div>`;}
  const kindLabel = kind => ({question:'问题',request:'请求',objection:'异议'})[kind] || '问题';
  function recordAnswer(q){return q.fullAnswer || q.answer || [q.summary,...(q.points||[])].filter(Boolean).join('\n\n');}
  function recordPage(){const s=sessions.find(x=>x.id===state.recordId);if(!s)return '<div class="empty-state"><h3>这条记录已不存在</h3><button class="btn btn-secondary" data-action="history">返回记录</button></div>';const questions=(s.questions||[]).filter(q=>!q.ignored && q.kind!=='statement');return `<button class="back-link" data-action="history">${icon('back')}全部对话</button><div class="page-heading record-heading"><div><h1>${esc(s.title)} </h1><div class="record-summary"><span>${icon('clock')}${stamp(s.createdAt)} · ${minutes(s)}</span><span>${icon('sparkle')}${esc(s.model)}</span></div></div><div style="display:flex;gap:9px"><button class="btn btn-secondary btn-small" data-action="rename" data-id="${esc(s.id)}">${icon('edit')}重命名</button><button class="btn btn-primary btn-small" data-action="export" data-id="${esc(s.id)}">${icon('download')}导出 Markdown</button></div></div><div class="tabs" role="tablist" aria-label="对话记录内容">${[['questions',`回应建议 · ${questions.length}`],['transcript',`完整转写 · ${(s.transcript||[]).length}`],['background','背景快照']].map(([id,label])=>`<button class="tab ${state.recordTab===id?'active':''}" role="tab" aria-selected="${state.recordTab===id}" data-action="record-tab" data-tab="${id}">${label}</button>`).join('')}</div><div role="tabpanel">${state.recordTab==='questions'?(questions.length?questions.map((q,i)=>`<article class="question-record"><div class="q-label">建议 ${String(i+1).padStart(2,'0')} · ${kindLabel(q.kind)}</div><h3>${esc(q.text)}</h3><p class="record-answer">${esc(recordAnswer(q)||'这段内容尚未生成回应建议。')}</p></article>`).join(''):'<div class="empty-state"><h3>本次暂无回应建议</h3><p>仍可查看已保存的完整转写。</p></div>'):state.recordTab==='transcript'?`<div class="transcript-log">${(s.transcript||[]).length?s.transcript.map(m=>`<div class="transcript-line ${m.role==='me'?'me':''}"><span class="speaker">${m.role==='me'?'我':'对方'}</span><div class="transcript-bubble">${esc(m.text)}<small>${esc(transcriptTime(m.time))}</small></div></div>`).join(''):'<div class="empty-state"><h3>本次没有音频转写</h3></div>'}</div>`:`${s.backgroundName?`<p class="help-text" style="margin-bottom:12px">${esc(s.backgroundName)} · 本次使用的背景快照</p><pre class="background-content">${esc(s.backgroundText)}</pre>`:'<div class="empty-state"><h3>本次未上传背景资料</h3><p>可在设置中的“背景资料”保存资料，之后的对话会自动带入。</p></div>'}`}</div>`;}
  function destroyTranscriptionTest(){const test=state.transcriptionTest;state.transcriptionTest=null;test?.destroy();}
  function openModal(html,className=''){destroyTranscriptionTest();modalPreviousFocus=document.activeElement;modalRoot.innerHTML=`<div class="modal-backdrop"><section class="modal ${esc(className)}" role="dialog" aria-modal="true" aria-labelledby="modal-title"><button class="icon-button modal-close" data-action="close-modal" aria-label="关闭">${icon('close')}</button>${html}</section></div>`;document.body.style.overflow='hidden';setTimeout(()=>{const first=$('input,.btn,select,.modal-close',modalRoot);first?.focus();},0);}
  function closeModal(){const wasTest=!!state.transcriptionTest;destroyTranscriptionTest();preflightTimers.forEach(clearTimeout);preflightTimers=[];state.preflight=null;modalRoot.innerHTML='';document.body.style.overflow='';if(modalPreviousFocus?.isConnected)modalPreviousFocus.focus();else if(wasTest)$('[data-action="test-transcription"]')?.focus();modalPreviousFocus=null;}
  function cancelEngineStart(){++engineGeneration;clearTimeout(engineTimer);engineTimer=null;const pending=engineStart;engineStart=null;pending?.resolve(false);}
  function ensureEngineReady(){
    if(state.engine==='ready')return Promise.resolve(true);
    if(engineStart)return engineStart.promise;
    const generation=++engineGeneration;state.engine='loading';
    const pending={};pending.promise=new Promise(resolve=>pending.resolve=resolve);engineStart=pending;
    if(state.route==='engine'||state.route==='home')render();
    engineTimer=setTimeout(()=>{
      if(generation!==engineGeneration){pending.resolve(false);return;}
      engineTimer=null;engineStart=null;state.engine='ready';
      if(state.route==='engine'||state.route==='home')render();
      pending.resolve(true);
    },1000);
    return pending.promise;
  }
  function openTranscriptionTest(){
    if(state.overlay||state.preflight)return;
    openModal('<h2 id="modal-title">测试转写</h2><div class="tt-panel" id="transcription-test-panel"></div>','transcription-test-modal');
    state.transcriptionTest=window.ChunfeTranscriptionTest.mount({container:$('#transcription-test-panel'),spkDevice:state.systemDevice,micDevice:state.microphone,getEngineStatus:()=>state.engine,startEngine:ensureEngineReady});
  }
  const checks=[['engine','chip','离线转写引擎','本地引擎已就绪'],['system','monitor','系统音频','接收对方的声音'],['mic','mic','麦克风','接收你的声音'],['model','sparkle','模型连接','建议服务已连接'],['file','file','背景资料','资料已准备好']];
  function startPreflight(){
    closeModal();
    state.preflight={scenario:state.startScenario,done:0,checking:true,runId:0};
    openModal(`<h2 id="modal-title">正在准备对话</h2><p class="modal-subtitle">检查完成后将自动进入悬浮辅助。</p><div id="preflight-content"></div>`);
    runPreflight();
  }
  function runPreflight(){
    preflightTimers.forEach(clearTimeout);preflightTimers=[];
    const p=state.preflight;if(!p)return;
    p.done=0;p.checking=true;const runId=++p.runId;
    drawPreflight();
    checks.forEach((_,i)=>preflightTimers.push(setTimeout(()=>{
      if(state.preflight!==p||p.runId!==runId)return;
      p.done=i+1;
      if(i===checks.length-1){p.checking=false;state.engine=p.scenario==='engine'?'error':'ready';}
      drawPreflight();
      if(!p.checking&&p.scenario==='success')preflightTimers.push(setTimeout(()=>{
        if(state.preflight===p&&p.runId===runId)launch('full');
      },350));
    },220+i*260)));
  }
  function preflightError(){const p=state.preflight;if(!p||p.checking||p.scenario==='success')return '';return {model:'模型连接失败，请重试检查。',engine:'转写引擎启动失败，请重试检查。',system:'没有检测到系统音频，请检查设备后重试。',mic:'没有检测到麦克风，请检查设备后重试。'}[p.scenario];}
  function drawPreflight(){
    const p=state.preflight;if(!p)return;const fail=preflightError();
    $('#preflight-content').innerHTML=`<div class="preflight-progress"><span style="width:${p.done/5*100}%"></span></div>${checks.map(([id,i,label,description],idx)=>{
      const checked=idx<p.done,failed=checked&&p.scenario===id;
      return `<div class="check-row ${failed?'failed':!checked?'pending':''}"><span class="check-icon">${icon(i)}</span><div><strong>${label}</strong><small>${id==='file'?(state.background.text?'自动使用已保存的背景':'尚未设置 · 不影响开始'):failed?({engine:'引擎未响应',model:'模型请求超时',system:'未检测到输出信号',mic:'设备未就绪'}[id]):description}</small></div><span class="check-state">${!checked?(idx===p.done?'<span class="spinner"></span>检查中':'等待检查'):failed?`${icon('alert')}需处理`:`${icon('checkCircle')}${id==='file'&&!state.background.text?'可跳过':'已就绪'}`}</span></div>`;
    }).join('')}${fail?`<div class="preflight-warning">${fail}</div>`:''}<div class="modal-footer">${fail?`<button class="btn btn-secondary" data-action="close-modal">取消</button><button class="btn btn-primary" data-action="retry-preflight">重试检查</button>`:`<span class="auto-start-note">${p.checking?'正在检查环境…':'检查通过，正在进入…'}</span><button class="btn btn-secondary" data-action="close-modal">取消</button>`}</div>`;
  }
  function launch(mode){if(!window.ChunfengOverlay){toast('悬浮界面尚未加载，请刷新页面重试。','alert');return;}const session={id:typeof crypto.randomUUID==='function'?crypto.randomUUID():`session-${Date.now()}`,title:state.draft.title.trim()||`对话 · ${stamp(new Date())}`,createdAt:new Date().toISOString(),durationSeconds:0,backgroundName:state.background.name,backgroundText:state.background.text,model:currentModel(),mode,transcript:[],questions:[]};closeModal();upsertSession(session);state.draft=newDraft();$('#app-window').hidden=true;$('#overlay-root').hidden=false;window.scrollTo(0,0);state.overlay=window.ChunfengOverlay.mount({container:$('#overlay-root'),session,onUpdate:upsertSession,onFinish:(result)=>{upsertSession(result);state.overlay?.destroy();state.overlay=null;$('#overlay-root').hidden=true;$('#app-window').hidden=false;state.recordTab='questions';go(`record/${encodeURIComponent(result.id)}`);toast('本次文字记录已保存。');},onBack:(result)=>{upsertSession(result);state.overlay?.destroy();state.overlay=null;$('#overlay-root').hidden=true;$('#app-window').hidden=false;go('home');toast('对话文字已保存，返回工作台。');}});}
  async function readFile(file){
    if(!file)return;
    if(!/\.md$/i.test(file.name)){toast('请选择 .md 格式的 Markdown 文件。','alert');return;}
    if(file.size>256*1024){toast('文件过大，请选择 256 KB 以内的 Markdown 文件。','alert');return;}
    const generation=++fileGeneration;state.fileLoading=true;refreshBackground();
    try{
      const text=await file.text();if(generation!==fileGeneration)return;
      if(!text.trim())throw new Error('empty');
      state.backgroundDraft={name:file.name,text,updatedAt:state.background.updatedAt};state.backgroundDirty=true;
      toast('Markdown 已导入，保存后会自动用于后续对话。');
    }catch(e){toast(e.message==='empty'?'文件内容为空，请换一个 Markdown 文件。':'文件读取失败，请重新选择。','alert');}
    finally{if(generation===fileGeneration){state.fileLoading=false;refreshBackground();}}
  }
  function refreshBackground(){if(state.route==='background')render();}
  function saveBackground(){
    if(state.fileLoading)return;
    const text=state.backgroundDraft.text;
    if(new Blob([text]).size>256*1024){toast('背景资料过长，请精简到 256 KB 以内。','alert');return;}
    const next={name:text.trim()?(state.backgroundDraft.name||'背景资料.md'):'',text:text.trim()?text:'',updatedAt:new Date().toISOString()};
    try{localStorage.setItem(PROFILE,JSON.stringify(next));}
    catch{toast('背景保存失败，浏览器存储空间不足。请精简资料后重试。','alert');return;}
    state.background=next;state.backgroundDraft={...next};state.backgroundDirty=false;render();
    toast(next.text?'背景已保存，之后每次对话会自动使用。':'已清空背景，后续对话将不带入资料。');
  }
  function validateConfig(){const endpoint=state.editConfig.endpoint.trim(),model=state.editConfig.model.trim();try{const url=new URL(endpoint);if(!['http:','https:'].includes(url.protocol)||!url.hostname)throw Error();}catch{return '请输入有效的 HTTP 或 HTTPS API 地址。';}if(!model)return '请填写模型名称。';return '';}
  function testConnection(){const error=validateConfig();if(error){state.testState='error';state.testResult=error;render();return;}clearTimeout(connectionTimer);state.testState='testing';state.testResult='';render();const scenario=state.testScenario;connectionTimer=setTimeout(()=>{state.testState=scenario==='success'?'ready':'error';state.testResult=scenario==='success'?'连接成功':scenario==='auth'?'Key 无效，请检查后重试。':'请求超时，请检查接口地址或稍后重试。';if(state.route==='settings')render();},900);}
  function saveConfig(){const error=validateConfig();if(error){toast(error,'alert');return;}state.config={...state.config,endpoint:state.editConfig.endpoint.trim(),model:state.editConfig.model.trim()};state.editConfig={...state.config};try{localStorage.setItem(CONFIG,JSON.stringify({endpoint:state.config.endpoint,model:state.config.model}));toast('模型配置已保存。');}catch{toast('配置已应用，当前浏览器无法持久化保存。','alert');}}
  function exportRecord(id) {
    const s=sessions.find(x=>x.id===id);if(!s)return;
    const parts=[`# ${s.title}`,`时间：${stamp(s.createdAt)}\n模型：${s.model}\n时长：${minutes(s)}`,'## 回应建议'];
    (s.questions||[]).filter(q=>!q.ignored && q.kind!=='statement').forEach((q,i)=>parts.push(`### ${i+1}. ${q.text}\n\n类型：${kindLabel(q.kind)}\n\n${recordAnswer(q)||'尚未生成回应建议'}`));
    parts.push('## 完整转写',...(s.transcript||[]).map(m=>`**${m.role==='me'?'我':'对方'} ${transcriptTime(m.time)}**\n\n${m.text}`));
    if(s.backgroundName)parts.push('## 本次背景资料',`文件：${s.backgroundName}`,s.backgroundText);
    const blob=new Blob([parts.join('\n\n')],{type:'text/markdown;charset=utf-8'}),url=URL.createObjectURL(blob),a=document.createElement('a');
    a.href=url;a.download=(s.title.replace(/[\\/:*?"<>|]/g,'-').slice(0,70)||'对话记录')+'.md';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);toast('Markdown 记录已导出。');
  }
  function deleteRecord(id){const s=sessions.find(x=>x.id===id);if(!s)return;openModal(`<h2 id="modal-title">删除这段对话？</h2><p class="modal-subtitle">${esc(s.title)}</p><p class="help-text">会移除这段对话的文字记录，保留已保存的背景资料。删除后可通过提示条撤销。</p><div class="modal-footer"><button class="btn btn-secondary" data-action="close-modal">保留</button><button class="btn btn-danger" data-action="confirm-delete" data-id="${esc(id)}">删除对话</button></div>`);}
  function confirmDelete(id){const index=sessions.findIndex(s=>s.id===id);if(index<0)return;const removed=sessions.splice(index,1)[0];saveSessions();closeModal();if(state.route==='record')go('history');else render();toast('对话已删除。','checkCircle',()=>{sessions.splice(Math.min(index,sessions.length),0,removed);saveSessions();render();});}
  function renameRecord(id){const s=sessions.find(x=>x.id===id);if(!s)return;openModal(`<h2 id="modal-title">重命名对话</h2><p class="modal-subtitle">一个容易找到的名字，方便下次回看。</p><label class="field-label" for="rename-input">对话名称</label><input class="text-input" id="rename-input" maxlength="80" value="${esc(s.title)}"><div class="modal-footer"><button class="btn btn-secondary" data-action="close-modal">取消</button><button class="btn btn-primary" data-action="save-rename" data-id="${esc(id)}">保存名称</button></div>`);}
  document.addEventListener('click',e=>{const b=e.target.closest('[data-action]');if(!b||b.disabled||$('#overlay-root').contains(b))return;const a=b.dataset.action,id=b.dataset.id;switch(a){case 'settings':go('settings');break;case 'background':go('background');break;case 'save-background':saveBackground();break;case 'engine':go('engine');break;case 'history':go('history');break;case 'new':fileGeneration++;state.draft=newDraft();state.fileLoading=false;go('home');break;case 'record':go(`record/${encodeURIComponent(id)}`);break;case 'choose-file':$('#markdown-file').click();break;case 'start':startPreflight();break;case 'close-modal':closeModal();break;case 'retry-preflight':state.preflight.scenario='success';runPreflight();break;case 'toggle-key':{const key=$('#api-key');key.type=key.type==='password'?'text':'password';b.setAttribute('aria-label',key.type==='password'?'显示 Key':'隐藏 Key');break;}case 'test-connection':testConnection();break;case 'save-config':saveConfig();break;case 'test-transcription':openTranscriptionTest();break;case 'toggle-engine':if(state.engine==='ready'){cancelEngineStart();state.engine='idle';render();toast('离线引擎已停止。');}else{ensureEngineReady().then(ready=>{if(ready)toast('离线引擎已就绪。');});}break;case 'record-tab':state.recordTab=b.dataset.tab;render();break;case 'export':exportRecord(id);break;case 'delete':deleteRecord(id);break;case 'confirm-delete':confirmDelete(id);break;case 'rename':renameRecord(id);break;case 'save-rename':{const title=$('#rename-input').value.trim();if(!title){toast('对话名称不能为空。','alert');return;}const s=sessions.find(x=>x.id===id);s.title=title;saveSessions();closeModal();render();toast('对话名称已更新。');break;}}});
  document.addEventListener('input',e=>{switch(e.target.id){case 'background-text':state.backgroundDraft.text=e.target.value;state.backgroundDirty=true;const status=$('#background-save-state');if(status)status.textContent='有未保存的修改';break;case 'api-endpoint':state.editConfig.endpoint=e.target.value;state.testResult='';break;case 'api-key':state.key=e.target.value;state.testResult='';break;case 'model-name':state.editConfig.model=e.target.value;state.testResult='';break;case 'history-search':state.historySearch=e.target.value;$('#history-list').innerHTML=historyRows();break;}});
  document.addEventListener('change',e=>{if(e.target.id==='markdown-file'){readFile(e.target.files?.[0]);e.target.value='';}if(e.target.id==='system-device')state.systemDevice=e.target.value;if(e.target.id==='microphone-device')state.microphone=e.target.value;});
  document.addEventListener('dragover',e=>{if(e.target.closest('.profile-card')){e.preventDefault();$('.profile-card').classList.add('drag-over');}});document.addEventListener('dragleave',e=>{if(e.target.closest('.profile-card')&&!$('.profile-card').contains(e.relatedTarget))$('.profile-card').classList.remove('drag-over');});document.addEventListener('drop',e=>{if(e.target.closest('.profile-card')){e.preventDefault();readFile(e.dataTransfer.files?.[0]);}});
  document.addEventListener('keydown',e=>{if(!modalRoot.children.length)return;if(e.key==='Escape'){closeModal();return;}if(e.key==='Enter'&&e.target.id==='rename-input')$('[data-action="save-rename"]').click();if(e.key==='Tab'){const f=[...modalRoot.querySelectorAll('button:not(:disabled),input,select,a[href]')];if(!f.length)return;const first=f[0],last=f[f.length-1];if(e.shiftKey&&document.activeElement===first){e.preventDefault();last.focus();}else if(!e.shiftKey&&document.activeElement===last){e.preventDefault();first.focus();}}});
  window.addEventListener('hashchange',()=>navigate(location.hash.slice(1)||'home'));
  window.addEventListener('beforeunload',()=>{destroyTranscriptionTest();cancelEngineStart();clearTimeout(connectionTimer);preflightTimers.forEach(clearTimeout);state.overlay?.destroy();});
  navigate(location.hash.slice(1)||'home');
})();
