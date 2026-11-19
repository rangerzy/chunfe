/* Isolated transcription-test state and view. All audio/text are fixed local samples. */
(() => {
  'use strict';
  const samples = [
    {source:'spk',text:'我们先确认一下今天讨论的内容。'},
    {source:'mic',text:'好的，我这边可以清楚地听到。'},
    {source:'spk',text:'接下来一起核对时间和安排。'},
    {source:'mic',text:'我会把确认的事项整理下来。'}
  ];
  const labels = {starting:'正在启动',waiting:'等待声音',receiving:'有声音',error:'设备异常',stopped:'已停止'};
  const formatTime = milliseconds => {
    const seconds = Math.floor(milliseconds / 1000);
    return String(Math.floor(seconds / 60)).padStart(2,'0') + ':' + String(seconds % 60).padStart(2,'0');
  };
  const escapeText = value => String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));

  function createController(options = {}) {
    const interval = Math.max(20, Number(options.interval) || 200);
    const getEngineStatus = options.getEngineStatus || (() => 'ready');
    const schedule = options.schedule || ((fn, ms) => setInterval(fn, ms));
    const unschedule = options.unschedule || (handle => clearInterval(handle));
    const available = {spk: options.spkAvailable !== false, mic: options.micAvailable !== false};
    let status = 'idle', opened = false, destroyed = false, elapsed = 0, ticks = 0;
    let generation = 0, timer = null, lines = [], lineSequence = 0, currentLine = null;
    let spkLevel = 0, micLevel = 0, errorMessage = '';
    function channelState(source) {
      if (!available[source]) return 'error';
      if (status === 'starting') return 'starting';
      if (status === 'error') return 'error';
      if (status !== 'running') return status === 'idle' ? 'waiting' : 'stopped';
      return (source === 'spk' ? spkLevel : micLevel) > 0 ? 'receiving' : 'waiting';
    }
    function snapshot() {
      return {opened,status,elapsedText:formatTime(elapsed),spkDevice:String(options.spkDevice || '系统默认输出'),micDevice:String(options.micDevice || '系统默认麦克风'),spkState:channelState('spk'),micState:channelState('mic'),spkLevel,micLevel,errorMessage,lines:lines.map(line => ({...line}))};
    }
    function emit() { if (!destroyed) options.onChange?.(snapshot()); }
    function clearTimer() { if (timer !== null) { unschedule(timer); timer = null; } }
    function fail(message) { clearTimer();status='error';spkLevel=0;micLevel=0;if(currentLine)currentLine.final=true;errorMessage=message;emit(); }
    function run(runId) {
      if (destroyed || !opened || generation !== runId || status !== 'starting') return;
      if (getEngineStatus() !== 'ready') { fail('转写引擎启动失败，请重新测试。');return; }
      status='running';timer=schedule(advanceTick,interval);emit();
    }
    function restart() {
      if (destroyed || !opened) return;
      clearTimer();const runId=++generation;
      lines=[];elapsed=0;ticks=0;currentLine=null;spkLevel=0;micLevel=0;errorMessage='';status='starting';emit();
      if (getEngineStatus()==='ready') { run(runId);return; }
      Promise.resolve().then(() => options.startEngine?.()).then(() => run(runId)).catch(() => {
        if (!destroyed && opened && generation===runId && status==='starting') fail('转写引擎启动失败，请重新测试。');
      });
    }
    function open() { if(destroyed)return;opened=true;restart(); }
    function stop() { if(destroyed)return;++generation;clearTimer();status='stopped';spkLevel=0;micLevel=0;if(currentLine)currentLine.final=true;errorMessage='';emit(); }
    function close() { if(destroyed)return;opened=false;++generation;clearTimer();status='idle';spkLevel=0;micLevel=0;errorMessage='';lines=[];elapsed=0;ticks=0;currentLine=null;emit(); }
    function destroy() { if(destroyed)return;close();destroyed=true; }
    function advanceTick() {
      if (destroyed || !opened || status!=='running') return;
      if (getEngineStatus()!=='ready') { fail('转写引擎已停止，请重新测试。');return; }
      elapsed+=interval;
      const sample=samples[Math.floor(ticks/16)%samples.length], phase=ticks%16;
      ticks++;
      spkLevel=0;micLevel=0;
      if (phase>=3 && phase<=11 && available[sample.source]) {
        const level=[.34,.65,.48,.82,.56,.72,.42,.64,.3][phase-3];
        if(sample.source==='spk')spkLevel=level;else micLevel=level;
      }
      if(phase===3) {
        currentLine=null;
        if(available[sample.source]) {
          currentLine={id:++lineSequence,source:sample.source,time:formatTime(elapsed),text:'',final:false};
          lines.push(currentLine);if(lines.length>40)lines.shift();
        }
      }
      if(currentLine && phase>=3 && phase<=13) {
        const count=Math.min(sample.text.length,(phase-2)*3);
        currentLine.text=sample.text.slice(0,count);currentLine.final=count===sample.text.length;
      }
      emit();
    }
    return {open,stop,restart,close,destroy,snapshot,advanceTick};
  }

  function mount(options) {
    const {container}=options;
    const icon=window.ChunfeIcons.icon;
    let followLatest=true, controller;
    const rows=new Map();
    const channelMarkup=(source,name,iconName)=>`<div class="tt-channel" data-channel="${source}"><span class="tt-channel-icon">${icon(iconName)}</span><div class="tt-device"><strong>${source.toUpperCase()} <span>· ${name}</span></strong><span class="tt-device-name"></span></div><div class="tt-level" role="meter" aria-label="${source.toUpperCase()} 音量" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"><span></span></div></div>`;
    container.innerHTML=`<div class="tt-channels">${channelMarkup('spk','系统声音','headphones')}${channelMarkup('mic','麦克风','mic')}</div><div class="tt-transcript-section"><div class="tt-transcript-heading"><strong>转写文本</strong><span class="tt-running-state" role="status"></span></div><div class="tt-scroll" tabindex="0" aria-label="测试转写文本"><div class="tt-empty"></div><div class="tt-lines"></div></div><button class="tt-latest btn btn-secondary btn-small" data-test-action="latest" hidden>回到最新 ${icon('arrow-right')}</button></div><div class="tt-footer"><span class="tt-error" role="status"></span><button class="btn btn-secondary" data-test-action="stop">停止测试</button><button class="btn btn-primary" data-test-action="restart">重新测试</button></div>`;
    const $=selector=>container.querySelector(selector), scroll=$('.tt-scroll'), list=$('.tt-lines');
    const latest=$('.tt-latest');
    const render=state=>{
      const previousTop=scroll.scrollTop;
      const ids=new Set(state.lines.map(line=>line.id));
      const scrollTop=scroll.getBoundingClientRect().top;
      const anchor=!followLatest && [...rows.entries()].find(([id,row])=>ids.has(id) && row.getBoundingClientRect().bottom>scrollTop);
      const anchorOffset=anchor ? anchor[1].getBoundingClientRect().top-scrollTop : 0;
      for(const source of ['spk','mic']) {
        const row=$(`[data-channel="${source}"]`), channel=state[source+'State'], level=state[source+'Level'];
        row.dataset.state=channel;
        const device=row.querySelector('.tt-device-name');device.textContent=state[source+'Device'];device.title=state[source+'Device'];
        const meter=row.querySelector('.tt-level');meter.setAttribute('aria-valuenow',String(Math.round(level*100)));meter.setAttribute('aria-valuetext',`${labels[channel] || labels.waiting}，${Math.round(level*100)}%`);meter.querySelector('span').style.width=(level*100)+'%';
      }
      container.dataset.status=state.status;
      $('.tt-running-state').textContent=({starting:'正在启动引擎',running:'测试中',stopped:'已停止',error:'测试中断',idle:'未开始'})[state.status]+' · '+state.elapsedText;
      $('.tt-error').textContent=state.errorMessage;
      const empty=$('.tt-empty');empty.hidden=state.lines.length>0;
      empty.textContent=state.status==='starting'?'正在启动转写引擎…':state.status==='stopped'?'测试已停止':state.status==='error'?'等待重新测试':'暂无转写文本';
      for(const [id,row] of rows)if(!ids.has(id)){row.remove();rows.delete(id);}
      for(const line of state.lines) {
        let row=rows.get(line.id);
        if(!row){row=document.createElement('div');row.className='tt-line';row.dataset.source=line.source;row.innerHTML=`<div class="tt-line-meta"><span>${line.source==='spk'?'SPK':'MIC'}</span><time>${escapeText(line.time)}</time></div><p></p>`;rows.set(line.id,row);list.appendChild(row);}
        row.dataset.final=String(line.final);row.querySelector('p').textContent=line.text;
      }
      $('[data-test-action="stop"]').disabled=!['starting','running'].includes(state.status);
      $('[data-test-action="restart"]').disabled=state.status==='starting';
      if(followLatest)scroll.scrollTop=scroll.scrollHeight;
      else if(anchor)scroll.scrollTop+=anchor[1].getBoundingClientRect().top-scroll.getBoundingClientRect().top-anchorOffset;
      else scroll.scrollTop=previousTop;
      latest.hidden=followLatest || state.lines.length===0;
      options.onChange?.(state);
    };
    controller=createController({...options,onChange:render});
    const onScroll=()=>{followLatest=scroll.scrollHeight-scroll.scrollTop-scroll.clientHeight<16;latest.hidden=followLatest;};
    const onClick=event=>{
      const action=event.target.closest('[data-test-action]')?.dataset.testAction;
      if(action==='stop')controller.stop();
      if(action==='restart'){followLatest=true;controller.restart();}
      if(action==='latest'){followLatest=true;scroll.scrollTop=scroll.scrollHeight;latest.hidden=true;}
    };
    container.addEventListener('click',onClick);scroll.addEventListener('scroll',onScroll);
    const destroy=()=>{controller.destroy();container.removeEventListener('click',onClick);scroll.removeEventListener('scroll',onScroll);rows.clear();};
    controller.open();
    return {...controller,destroy};
  }
  window.ChunfeTranscriptionTest={createController,mount};
})();
