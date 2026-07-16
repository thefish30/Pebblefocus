# Pebblefocus

A Mark Forster Autofocus-style checklist for Pebble Time 2 (Emery). Touch-enabled, color-coded, dictation-fed, with phone-side persistence.

## Concept

One active list, worked in passes. Dotted items are the current pass's commitments. Checked-off items either move to a Done list or re-append to the end of the active list (recurring). Done is a rolling record of the last 50 completions, cleared manually.

## Item model

- Max 50 active items (configurable via Clay; storage is phone-side, so the cap is UI pragmatism, not memory)
- Each item: text, color, dot (this-pass marker), re-append flag
- Colors: purple, blue, green, yellow, aqua, pink, red, white (default/none)
- Color persists through re-append and copy-back

## Controls

### Active list
| Input | Action |
|---|---|
| Swipe up/down (touch) | Scroll |
| Tap item (touch) | Toggle dot |
| Horizontal swipe on item (touch) | Cycle color: purple → blue → green → yellow → aqua → pink → red → white |
| UP / DOWN click | Move focus |
| UP long press | Add item by dictation (arrives white, focus jumps to it) |
| SELECT click | Check off focused item (500ms checkmark beat, then departs) |
| SELECT long press | Toggle item's check-off destination (Done ⇄ re-append; Done default). Re-append shown by reload icon; flag persists until untoggled |
| DOWN long press | Open Done list |
| BACK | Exit app |

### Done list
| Input | Action |
|---|---|
| Swipe / UP / DOWN | Scroll and focus |
| SELECT click | Copy item back to end of active list (stays in Done, arrives unchecked) |
| DOWN long press | Clear All (press again to confirm) |
| BACK | Return to active list |

Emulator stand-ins (no touch/mic in QEMU): BACK double-click toggles dot; UP long press adds a mock "Dictated item N".

## Entry

- **Watch:** dictation (UP long press)
- **Phone (Clay):** paste full list, append or replace. Sticky color prefixes — a prefix colors its line and all following lines until the next prefix:
  `y:` yellow · `g:` green · `u:` purple · `b:` blue · `a:` aqua · `p:` pink · `r:` red · `w:` white (also breaks a color run)
  Unprefixed leading lines are white.
- At the 50 cap, adds are rejected (short vibe on watch)

## Architecture

- **Phone is the source of truth.** pkjs `localStorage` holds both lists as JSON; the watch holds a RAM working copy.
- **Sync down:** on launch, pkjs streams `SYNC_START` (watch clears lists) → one message per item → `SYNC_COMPLETE`.
- **Sync up:** semantic deltas per mutation (`CHECKOFF`, `TOGGLE_DOT`, `TOGGLE_REAPP`, `ADD`, `COPY_BACK`, `CLEAR_DONE`); pkjs mirrors the watch's list logic exactly.
- Known limitation: mutations made while the phone is unreachable are lost at next sync. Persist-and-replay queue is a possible future step.

## Build

- CloudPebble (cloudpebble.repebble.com), SDK 4.9, C watchapp, Emery target
- Message keys (must exist in project settings, exact names): `CMD`, `INDEX`, `TEXT`, `COLOR`, `FLAGS`, `COUNT`, `LIST`
- Resource: `RELOAD_ICON` — 16×16 PNG (Feather `rotate-cw`), black on transparent
- Header bar shows a version stamp (`v4`, …) — if the stamp doesn't match the source, the emulator is running a stale binary: save, recompile, reinstall

## Status

- [x] Step 1 — watch UI skeleton, mock data, button-driven
- [x] Step 2 — AppMessage sync + pkjs persistence
- [ ] Step 3 — Clay: paste, sticky prefixes, append/replace, cap setting, storage reset
- [ ] Step 4 — Dictation API (real device)
- [ ] Step 5 — Touch: tap-to-dot, swipe-to-scroll, horizontal-swipe color cycling (real device)
