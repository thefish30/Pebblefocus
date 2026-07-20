# Pebblefocus

**Scan. Tap. Do.** — A Mark Forster Autofocus-style checklist for Pebble Time 2 (Emery). Touch-enabled, color-coded, dictation-or-text-fed, with phone-side persistence.

Published on the Rebble appstore / Pebble Appstore, v1.0.0.

## Concept

One active list, worked in passes. Scan the list; when a task stands out, tap it to mark it with a dot, then work your dotted tasks. Every check-off records to a rolling Done list (last 50, cleared manually); tasks flagged with the reload icon (↻) additionally re-enter at the end of the list — recurring until untoggled. You can also simply use it as a flexible color-coded to-do list without the Autofocus method.

## Item model

- Max 50 active items
- Each item: text, color, dot (this-pass marker), reload flag
- Colors: purple, blue, green, yellow, aqua, pink, red, white (default/none)
- Color persists through re-append and copy-back

## Controls

### Active list
| Input | Action |
|---|---|
| Tap item (touch) | Focus + dot/undot |
| Swipe up/down (touch) | Scroll (focus follows) |
| Swipe right/left on item (touch) | Cycle color forward/back: purple → blue → green → yellow → aqua → pink → red → white |
| UP / DOWN click | Move focus |
| UP long press | Add item by dictation (arrives white, focus jumps to it) |
| SELECT click | Check off focused item (500ms checkmark beat, then departs) |
| SELECT long press | Toggle item's reload flag (↻); flagged items re-append after check-off, flag persists until untoggled |
| DOWN long press | Open Done list |
| BACK | Exit app |

### Done list
| Input | Action |
|---|---|
| Swipe / UP / DOWN | Scroll and focus |
| SELECT click | Copy item back to end of active list (stays in Done, arrives unchecked) |
| DOWN long press | Clear All (second hold confirms; header shows the prompt) |
| BACK | Return to active list |

Header: `•3/8` = dotted count / total items, plus a clock. Touch is inert for dotting in the Done list by design.

## Entry

- **Watch:** dictation (UP long press), with confirmation screen
- **Phone (Clay settings page):** paste a full list, append or replace. Sticky color prefixes — a prefix colors its line and all following lines until the next prefix:
  `y:` yellow · `g:` green · `u:` purple · `b:` blue · `a:` aqua · `p:` pink · `r:` red · `w:` white (also breaks a color run). Unprefixed leading lines are white.
- At the 50-item cap, adds are rejected (short vibe on watch; dropped-line notification for oversized pastes)

## Architecture

- **Phone is the source of truth.** pkjs `localStorage` holds both lists as JSON; the watch holds a RAM working copy.
- **Sync down:** on launch, pkjs streams `SYNC_START` (watch clears lists) → one message per item → `SYNC_COMPLETE`.
- **Sync up:** semantic deltas per mutation (`CHECKOFF`, `TOGGLE_DOT`, `TOGGLE_REAPP`, `ADD`, `COPY_BACK`, `CLEAR_DONE`, `SET_COLOR`); pkjs mirrors the watch's list logic exactly.
- Known limitation: mutations made while the phone is unreachable are lost at next sync. Persist-and-replay queue is a possible future step.

## Build

- CloudPebble (cloudpebble.repebble.com), SDK 4.9, C watchapp, **Emery target only** (pebble-clay does not declare the gabbro/flint platforms; aplite/basalt/diorite intentionally dropped — no touch, and the store requires per-platform assets)
- Dependency: `pebble-clay` (^1.0.4) — version field must not be blank in CloudPebble
- Message keys (must exist in project settings, exact names): `CMD`, `INDEX`, `TEXT`, `COLOR`, `FLAGS`, `COUNT`, `LIST`, `PASTE`, `MODE`, `CAP`, `RESET` (last four are Clay-side only)
- Resource: `RELOAD_ICON` — 16×16 PNG (Feather `rotate-cw`), black on transparent
- Dev affordances left in source: `USE_DICTATION_STUB` toggle (emulator has no mic) and a `TOUCH_TAP_SLOP` / `TOUCH_HSWIPE_MIN` pair for gesture tuning

## History

Written in collaboration with Claude AI. The original UI design chat was accidentally deleted before any code existed; the app was reconstructed from memory and built to release in under a week. v1.0.0 published July 2026.
