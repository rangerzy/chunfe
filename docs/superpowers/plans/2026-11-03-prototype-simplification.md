# Prototype Simplification Implementation Plan

**Goal:** Simplify everyday start to one action, persist background once, and restyle the prototype in shadcn/ui Neutral.
**Spec:** `docs/superpowers/specs/2026-11-03-prototype-simplification.md`
**Architecture:** Keep static vanilla JS/CSS. Root owns state/markup/docs; two parallel agents own main and overlay CSS; a read-only reviewer checks behavior.

- [x] Replace session-bound background draft with an independent persisted profile; snapshot it at session start.
- [x] Reduce home to one launch action and compact statuses; move profile/model/audio to settings navigation.
- [x] Keep check dialog, automatically enter after successful checks, and cancel pending callbacks on close/retry.
- [x] Rewrite main and floating style to Neutral, retaining established responsive and drag behavior.
- [x] Verify background persistence, normal/cancel/failure check paths and main navigation in browser.
- [x] Read independent review, address material bugs, update screenshots and launch docs.

No production Qt changes, deployment, dependency installation, or Git commits are part of this iteration. Browser walkthrough is appropriate verification for this reversible prototype change.

## Verification · 2026-11-03

Both JS syntax checks passed. Browser verified profile save and refresh, automatic profile snapshot on session start, successful checks entering automatically, immediate cancel, failure staying for explicit handling, successful retry entering once, and 600px navigation/profile without horizontal overflow. Neutral main/overlay visuals inspected at default ~785px and 1280px; viewport reset. Added explicit navigation labels so icon-only navigation remains accessible. Independent reviewer exercised controlled timers and profile/session separation and found no blocking defects. Test-only example background cleared after checks; actual previous records retained.
