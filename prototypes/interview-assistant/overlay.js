/* Browser preview of the two floating dialogue windows. Explicitly labelled local samples drive response hints. */
(function () {
  'use strict';

  const icon = name => window.ChunfeIcons.icon(name);
  const brandMark = () => '<img src="assets/chunfe-mark.svg" alt="" aria-hidden="true">';
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const uid = () => 'ov-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 8);
  const kindLabel = kind => ({question: '问题', request: '请求', objection: '异议'})[kind] || '问题';
  const examples = [
  {
    "kind": "question",
    "text": "这次方案最需要先确认的是什么？",
    "answer": "先确认目标、范围和验收方式，再讨论具体方案。\n\n可以这样回应：我想先确认这次要解决的核心问题、需要交付到什么程度，以及怎样判断方案达到了目标。把这三点对齐后，再讨论实现方式。\n\n如果信息还不完整，先列出待确认项，不替对方补充事实。",
    "reply": "我想先确认目标、交付范围和验收标准。"
  },
  {
    "kind": "statement",
    "text": "我把刚才讨论的要点记下来了。"
  },
  {
    "kind": "request",
    "text": "请把下一步的安排和各自负责的事项整理一下。",
    "answer": "把下一步拆成具体事项，并区分已确认和待确认的信息。\n\n可以这样回应：我们先列清交付项，再一起确认每项的负责人和时间点。还没有确定的部分先标为待确认，避免遗漏或误解。\n\n整理后复述一遍分工，确认双方理解一致。",
    "reply": "我们先列清交付项，再一起确认负责人和时间。"
  },
  {
    "kind": "objection",
    "text": "这个时间可能来不及，而且现在的范围有点大。",
    "answer": "先承认时间和范围上的顾虑，再讨论可以调整的部分。\n\n可以这样回应：理解你的担心。我们先核实时间约束，把必须完成和可以延后的内容分开，再比较缩小范围或分阶段交付的可行性。\n\n在工作量和资源还没核实前，不急着承诺新的日期。",
    "reply": "我们先核实时间约束，再讨论缩小范围或分阶段交付。"
  },
  {
    "kind": "statement",
    "text": "我先把相关资料发到群里。"
  }
];

  function mount(options) {
    const { container, session, onUpdate, onFinish, onBack } = options;
    session.transcript = Array.isArray(session.transcript) ? session.transcript : [];
    session.questions = Array.isArray(session.questions) ? session.questions : [];
    const state = {
      paused: false, collapsed: false, answerCollapsed: false,
      currentId: session.questions.at(-1)?.id || null,
      nextIndex: 0, stream: null, reply: null,
      job: null, reveal: null, autoScroll: true, destroyed: false,
      elapsed: (Number(session.durationSeconds) || 0) * 1000,
      lastSecond: -1, nextAuto: 0, drag: null
    };
    const cleanups = [];
    const now = () => new Date().toISOString();
    const save = () => { if (!state.destroyed && onUpdate) onUpdate(session); };
    const current = () => session.questions.find(q => q.id === state.currentId);


    container.innerHTML = `<div class="cf-scene">
      <section class="cf-floating-group" aria-label="实时对话">
        <div class="cf-float-toolbar" data-drag="group">
          <button class="cf-float-ball" data-action="collapse" aria-label="收起对话" title="收起对话">${brandMark()}</button>
          <div class="cf-float-meta"><strong>春风正在聆听</strong><span><time class="cf-session-time">00:00</time> · <span class="cf-listening-state">实时转写</span></span></div>
          <div class="cf-window-tools">
            <button class="cf-icon-button cf-pause-button" data-action="pause" aria-label="暂停对话" title="暂停对话">${icon('pause')}</button>
            <button class="cf-icon-button cf-caption-collapse" data-action="collapse" aria-label="收起对话" title="收起对话">${icon('collapse')}</button>
            <button class="cf-icon-button" data-action="finish" aria-label="结束对话" title="结束对话">${icon('stop')}</button>
          </div>
        </div>
        <div class="cf-caption-wrap">
          <div class="cf-caption-scroll" tabindex="0" aria-label="双方字幕，向上滚动查看历史"></div>
          <button class="cf-latest-button" data-action="latest" hidden>回到最新 ${icon('arrow-right')}</button>
        </div>
      </section>
      <section class="cf-answer-card" aria-label="回应建议">
        <div class="cf-answer-heading" data-drag="answer">
          <div class="cf-answer-title">${icon('bolt')}<strong>回应建议</strong></div>
          <div class="cf-window-tools">
            <button class="cf-icon-button cf-answer-collapse" data-action="collapse-answer" aria-label="收起建议" title="收起建议">${icon('collapse')}</button>
            <button class="cf-icon-button" data-action="finish" aria-label="结束对话" title="结束对话">${icon('close')}</button>
          </div>
        </div>
        <div class="cf-answer-content"><div class="cf-answer-status" aria-live="polite"></div><div class="cf-answer-body" tabindex="0" aria-label="对应对话与回应建议"></div></div>
      </section>
    </div>`;
    const root = container.querySelector('.cf-scene');
    const $ = selector => root.querySelector(selector);
    const group = $('.cf-floating-group');
    const answer = $('.cf-answer-card');
    const scroller = $('.cf-caption-scroll');
    const listen = (element, type, fn, opts) => {
      element.addEventListener(type, fn, opts);
      cleanups.push(() => element.removeEventListener(type, fn, opts));
    };

    function renderTranscript() {
      const previous = scroller.scrollTop;
      scroller.innerHTML = session.transcript.map(line => `<div class="cf-caption-line cf-caption-${line.role === 'me' ? 'me' : 'other'}"><span class="cf-speaker-mark">${line.role === 'me' ? '我' : '对方'}</span><div class="cf-caption-bubble" data-line="${esc(line.id)}">${esc(line.text)}</div></div>`).join('');
      scroller.scrollTop = state.autoScroll ? scroller.scrollHeight : previous;
    }
    function renderAnswer() {
      const question = current();
      const body = $('.cf-answer-body');
      const previous = body.scrollTop;
      const changed = body.dataset.questionId !== (question?.id || '');
      body.dataset.questionId = question?.id || '';
      $('.cf-answer-status').innerHTML = state.job
        ? `<div class="cf-pending-question">正在准备新的回应建议…<span>${esc(state.job.question.text)}</span></div>`
        : state.reveal ? '<div class="cf-generating">正在生成回应建议…</div>' : '';
      body.innerHTML = question
        ? `<div class="cf-matched-question"><span>对应对话 · ${kindLabel(question.kind)}</span><p>${esc(question.text)}</p></div><div class="cf-answer-text${state.reveal ? ' cf-streaming' : ''}">${esc(question.fullAnswer || '')}</div>`
        : `<div class="cf-answer-empty">${icon('bolt')}<strong>等待需要回应的内容</strong></div>`;
      body.scrollTop = changed ? 0 : previous;
    }
    function renderState() {
      root.classList.toggle('cf-is-paused', state.paused);
      group.classList.toggle('cf-is-collapsed', state.collapsed);
      answer.classList.toggle('cf-is-collapsed', state.answerCollapsed);
      $('.cf-float-meta strong').textContent = state.paused ? '春风已暂停' : '春风正在聆听';
      $('.cf-listening-state').textContent = state.paused ? '暂停接收新内容' : '实时转写';
      const setButton = (selector, name, label) => {
        const button = $(selector);
        button.innerHTML = icon(name); button.setAttribute('aria-label', label); button.title = label;
      };
      setButton('.cf-pause-button', state.paused ? 'play' : 'pause', state.paused ? '继续对话' : '暂停对话');
      setButton('.cf-caption-collapse', state.collapsed ? 'expand' : 'collapse', state.collapsed ? '展开对话' : '收起对话');
      setButton('.cf-answer-collapse', state.answerCollapsed ? 'expand' : 'collapse', state.answerCollapsed ? '展开建议' : '收起建议');
      $('.cf-float-ball').setAttribute('aria-label', state.collapsed ? '展开对话' : '收起对话');
      $('.cf-float-ball').title = state.collapsed ? '展开对话' : '收起对话';
      renderAnswer(); clamp(group); clamp(answer);
      if (!state.collapsed && state.autoScroll) scroller.scrollTop = scroller.scrollHeight;
    }
    function acceptSuggestion(template) {
      const question = { id: uid(), kind: template.kind, text: template.text, summary: '', points: [], experience: '', fullAnswer: '', createdAt: now() };
      session.questions.push(question);
      // Keep the prior answer on screen until the first characters of the new answer arrive.
      state.job = { question, answer: template.answer, progress: 0 };
      save(); renderAnswer();
    }
    function startAnswer() {
      const job = state.job;
      state.job = null;
      state.currentId = job.question.id;
      state.reveal = { question: job.question, text: job.answer, index: 0 };
    }
    function advance() {
      if (state.paused || state.stream || state.reply || state.nextIndex >= examples.length) return;
      const template = examples[state.nextIndex];
      // Statements only add transcript text; they never replace an active suggestion.
      if (template.kind !== 'statement' && (state.job || state.reveal)) return;
      state.nextIndex++;
      const line = { id: uid(), role: 'other', text: '', time: now() };
      session.transcript.push(line);
      state.stream = { line, text: template.text, index: 0, template };
      state.nextAuto = state.elapsed + 15000;
      renderTranscript();
    }
    function finish() {
      session.durationSeconds = Math.floor(state.elapsed / 1000);
      session.endedAt = now();
      save(); destroy();
      (onFinish || onBack)?.(session);
    }
    listen(root, 'click', event => {
      const button = event.target.closest('[data-action]');
      if (!button || button.disabled) return;
      switch (button.dataset.action) {
        case 'finish': finish(); break;
        case 'pause': state.paused = !state.paused; renderState(); save(); break;
        case 'collapse': state.collapsed = !state.collapsed; renderState(); break;
        case 'collapse-answer': state.answerCollapsed = !state.answerCollapsed; renderState(); break;
        case 'latest': state.autoScroll = true; scroller.scrollTop = scroller.scrollHeight; $('.cf-latest-button').hidden = true; break;
      }
    });
    listen(scroller, 'scroll', () => {
      state.autoScroll = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 24;
      $('.cf-latest-button').hidden = state.autoScroll;
    });

    function clamp(element) {
      if (!element?.style.left || window.matchMedia('(max-width: 639px)').matches) return;
      const maxX = Math.max(16, root.clientWidth - element.offsetWidth - 16);
      const maxY = Math.max(16, root.clientHeight - element.offsetHeight - 16);
      element.style.left = Math.max(16, Math.min(parseFloat(element.style.left), maxX)) + 'px';
      element.style.top = Math.max(16, Math.min(parseFloat(element.style.top), maxY)) + 'px';
    }
    function initialPosition() {
      state.layoutWidth = root.clientWidth;
      state.layoutHeight = root.clientHeight;
      const left = Math.max(16, (root.clientWidth - group.offsetWidth - answer.offsetWidth - 16) / 2);
      const top = Math.max(16, (root.clientHeight - Math.max(group.offsetHeight, answer.offsetHeight)) / 2 - 12);
      group.style.left = left + 'px'; group.style.top = top + 'px';
      answer.style.left = (left + group.offsetWidth + 16) + 'px'; answer.style.top = top + 'px';
      clamp(group); clamp(answer);
    }
    listen(root, 'pointerdown', event => {
      const handle = event.target.closest('[data-drag]');
      if (!handle || event.target.closest('button') || event.button !== 0 || window.matchMedia('(max-width: 639px)').matches) return;
      const element = handle.dataset.drag === 'group' ? group : answer;
      state.drag = { element, x: event.clientX, y: event.clientY, left: parseFloat(element.style.left) || 0, top: parseFloat(element.style.top) || 0 };
      root.classList.add('cf-dragging'); handle.setPointerCapture(event.pointerId); event.preventDefault();
    });
    listen(root, 'pointermove', event => {
      if (!state.drag) return;
      const drag = state.drag;
      drag.element.style.left = drag.left + event.clientX - drag.x + 'px';
      drag.element.style.top = drag.top + event.clientY - drag.y + 'px';
      clamp(drag.element);
    });
    const endDrag = () => { state.drag = null; root.classList.remove('cf-dragging'); };
    listen(root, 'pointerup', endDrag); listen(root, 'pointercancel', endDrag);
    function resizeLayout() {
      if (state.destroyed) return;
      if (root.clientWidth !== state.layoutWidth) initialPosition();
      else if (root.clientHeight !== state.layoutHeight) {
        state.layoutHeight = root.clientHeight; clamp(group); clamp(answer);
      }
    }
    listen(window, 'resize', resizeLayout);
    let resizeObserver;
    if (window.ResizeObserver) { resizeObserver = new ResizeObserver(resizeLayout); resizeObserver.observe(root); }

    let previousTick = Date.now();
    const timer = setInterval(() => {
      const time = Date.now();
      const delta = Math.min(time - previousTick, 250);
      previousTick = time;
      if (state.destroyed || state.paused) return;
      state.elapsed += delta;
      const elapsed = Math.floor(state.elapsed / 1000);
      if (elapsed !== state.lastSecond) {
        state.lastSecond = elapsed; session.durationSeconds = elapsed;
        $('.cf-session-time').textContent = String(Math.floor(elapsed / 60)).padStart(2, '0') + ':' + String(elapsed % 60).padStart(2, '0');
        if (elapsed % 5 === 0) save();
      }
      if (state.stream) {
        state.stream.index += 2;
        state.stream.line.text = state.stream.text.slice(0, state.stream.index);
        const bubble = scroller.querySelector('[data-line="' + state.stream.line.id + '"]');
        if (bubble) bubble.textContent = state.stream.line.text;
        if (state.autoScroll) scroller.scrollTop = scroller.scrollHeight;
        if (state.stream.index >= state.stream.text.length) {
          const done = state.stream;
          state.stream = null;
          if (done.template && ['question', 'request', 'objection'].includes(done.template.kind)) {
            acceptSuggestion(done.template);
            state.reply = { text: done.template.reply, remaining: 3000 };
          }
          save();
        }
      }
      if (state.reply) {
        state.reply.remaining -= delta;
        if (state.reply.remaining <= 0 && !state.stream) {
          const line = { id: uid(), role: 'me', text: '', time: now() };
          session.transcript.push(line);
          state.stream = { line, text: state.reply.text, index: 0 };
          state.reply = null; renderTranscript();
        }
      }
      if (state.job) {
        state.job.progress += delta;
        if (state.job.progress >= 1300) startAnswer();
      }
      if (state.reveal) {
        const reveal = state.reveal;
        reveal.index += 5;
        reveal.question.fullAnswer = reveal.text.slice(0, reveal.index);
        if (reveal.index >= reveal.text.length) { state.reveal = null; save(); }
        renderAnswer();
      }
      if (state.elapsed >= state.nextAuto) advance();
    }, 90);
    function destroy() {
      if (state.destroyed) return;
      state.destroyed = true; clearInterval(timer); cleanups.forEach(fn => fn()); resizeObserver?.disconnect();
    }
    renderTranscript(); renderState(); initialPosition(); advance(); save();
    return { destroy };
  }
  window.ChunfengOverlay = { mount };
}());
