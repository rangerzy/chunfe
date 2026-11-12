# Interview Prototype Implementation Plan

> **For agentic workers:** Use the approved prototype brief and scoped parallel work. Track the steps below.

**Goal:** Deliver a polished clickable 春风 prototype that validates the complete interview assistance workflow.

**Architecture:** Static HTML, CSS and vanilla JavaScript, with an isolated overlay module and main app coordinator. Browser localStorage stores text sessions; no network/model/audio APIs are called. Classic scripts allow both a local server and opening index.html directly.

**Tech Stack:** HTML/CSS/JavaScript, Python local static server, browser UI verification.

**Spec:** `docs/superpowers/specs/2026-10-28-interview-prototype-design.md`

## Global Constraints

- User approved execution; no additional implementation-method approval needed.
- Isolate all product files under `prototypes/interview-assistant/`; do not change Qt production sources.
- Prototype only: real local Markdown input and local history; simulated ASR/API/device states clearly marked.
- Do not persist API keys or execute imported Markdown as HTML.
- Fixed compact transparent floating container, approximately six visible short utterances.

## Review Focus

- Long/imported HTML-looking Markdown stays inert and does not overflow.
- Pause/collapse/pin are distinct; pinned answers survive new questions.
- Old answers remain labelled and are not shown as answers to failed new questions.
- Navigation/reloads preserve text records without copying old background into new sessions.
- Window resizing and dragging preserve reachability of all controls.

## Task 1 — Main app and session flow (root)

Files: `index.html`, `styles.css`, `app.js`.
Interface: global `ChunfengOverlay.mount({container,session,onUpdate,onFinish,onBack})` returns `{destroy()}`. Session: `{id,title,createdAt,endedAt?,durationSeconds,backgroundName,backgroundText,model,mode,transcript:[],questions:[]}`. Transcript entries: `{id,role:'interviewer'|'me',text,time}`; questions: `{id,text,summary,points:[],experience,fullAnswer,code?,ignored?,createdAt}`.

- [x] Build cohesive navigation, new session/Markdown upload, model configuration, preflight checks and local records.
- [x] Use safe text rendering, limited Markdown file size, session-isolated data, and memory-only Key input.
- [x] Verify new session, model validation, file import, degraded start and record operations through visible browser UI.

## Task 2 — Floating interview simulation (parallel agent)

Files: `overlay.js`, `overlay.css` only. Implements the mount contract above, owns its demo timers and DOM, releases them in destroy(). Calls onUpdate(session) on transcript/answer changes; onFinish(session) on explicit end; onBack(session) for returning to workbench while ending overlay rendering.

- [x] Build a plausible meeting desktop canvas plus compact draggable transparent group and independently draggable answer window.
- [x] Simulate streaming transcription/answers; expose advance, pause, collapse, pin, pending question, manual/correct/ignore and ASR/model fault/retry behavior.
- [x] Verify reachable controls, fixed height, and stopped timers when destroyed.

## Task 3 — Integration and verification (root + reviewer)

- [x] Run JavaScript syntax checks and serve locally.
- [x] Walk full flow in browser, including pin/next/unpin, pause/resume, manual question, fault/retry, finish/history/export, long text and narrow viewport.
- [x] Request independent read-only review; fix actionable issues and recheck changed paths.
- [x] Save screenshots, a concise README with launch command/limitations, and open the finished prototype.

## Execution notes

This is a reversible design prototype, so use browser behavior checks rather than a production test framework. No commits or deployment are required. The user's final confirmation authorizes implementation of the agreed design.

## Verification outcome · 2026-10-28

Both JavaScript syntax checks and git diff --check passed. Browser walkthrough verified Markdown import/long text, startup checks, model-failure transcript-only mode, ASR-failure manual mode, pin/pending question, pause/resume/collapse, hand-entered questions, fault/retry, end/save, reload persistence, rename, search, delete/undo and Markdown export. Actual export contained the manual question, transcript and background. At 768px, floating windows have a verified 20px gap; at 600px they stack without horizontal overflow. New sample questions also produce delayed candidate-side captions. Independent reviewer found and re-tested an edit-question race; it is fixed. No browser console errors observed. API/audio/ASR remain simulated.
