# BBSC development phases

From the project specification (section 26). Status as of 2026-09-15; phase 5 updated 2026-09-23. **Check CHANGELOG.md for anything newer - this table lags it.**

| # | Phase | Goal | Status |
| --- | --- | --- | --- |
| 0 | **Audit** | Understand shadPS4, shadNet, shadp2p and Bloodborne's online flow; find every kick path. No code changes. | ✅ Done (report, docs, 43 kick paths, tools) |
| 1 | **Persistent connection** | Host + guest stay connected when Bloodborne would normally end the session (loads, death, boss). | 🟡 First success: guest death kept the session alive (host still sees guest) |
| 2 | **Area transition** | Area A → loading → Area B with both players still in the same session. | 🟡 Experiment built (member replay after a kept load) — testing now |
| 3 | **Player resync** | Players see each other again after loading, death, reconnect, different loading times. | 🟡 Same experiment as Phase 2 (guest must see host again) |
| 4 | **World state** | Enemies, objects, interactions and progression synced; personal loot; guest progression; bosses killed together count for guest. | ⬜ Research started (pickup flag found, event-flag functions found) |
| 5 | **Boss continuity** | Boss dies → no kick → keep playing together. | ✅ Works in play (2026-09-20, two PCs). Done by the BlockClear2 hooks, which install with SPECTATE and so are on by default; the separate `-Boss` byte patches (`PERSIST_BOSS`) are not needed for it |
| 6 | **Death / respawn** | Anyone dies → respawns → still together. | 🟡 Depends on Phase 2/3 result |
| 7 | **Lamp / fast travel** | Host uses a lamp → both travel → still together. | 🟡 Second lamp gate found, patch built (`-Lamp`, host only), not tested |
| 8 | **Hunter's Dream** | Go to and leave the Dream together (guest can level up). | ⬜ Not started |
| 9 | **DLC / Chalice Dungeons** | Everything works in DLC areas and Chalice Dungeons. | ⬜ Not started |
| 10 | **Reconnect / recovery** | A player disconnects, reconnects and resyncs into the same session. | ⬜ Not started |
| 11 | **Polish** | Stability, performance, edge cases, logging, settings, error messages. | ⬜ Ongoing (account system, email verification, tools done) |

Legend: ✅ done · 🟡 in progress / built but untested · ⬜ not started

## Release roadmap (decided 2026-09-23)

| Release | Contents |
| --- | --- |
| **Open beta 1** | Core session: deaths, lamp warping together, boss kills, full echoes, fog walls, cross-map join - **plus Hunter's Dream together (phase 8), The Old Hunters DLC and Chalice Dungeons (phase 9)** - plus the release work in `DISTRIBUTION.md` (items 1-7) |
| **Open beta 2** | Shared progress (phase 4): bosses killed together count for the guest, personal loot, guest progression. Writes saves - test on copies only, tell players to back up. Reconnect (phase 10) |
| Found 2026-09-23 (first Radmin session) | **(a)** "Failed to connect to Bloodborne server" kicks: the game re-checks the game server (port 20443) periodically and returns to title when it cannot reach it. Serve the host's own game `127.0.0.1` in ss.info, keep the VPN address for others; make `bbsc_gameserver.py` log to a file. **(b)** The game server (like fhody125's NoDreamForHunter it came from) answers everything except login/ss.info/summons with empty replies: no messages, bloodstains, ghosts, Chalice glyph channels. Real implementations need the response formats reverse-engineered from the eboot. **Chalice channels are required for beta 1**; messages/bloodstains beta 1 or 2 (user to decide). **(c)** Cross-map summon failed: host m23 (`385875968`) searching, guest m24_01 (`402718720`) advertised; shadnet moved the sign to Preparing and sent the 53-byte placement, the guest never travelled. Needs the guest's shadPS4 log. Same-map summons work (21 Sept). |
| Anytime | Third-player test, polish (phase 11) |
| Proposed for beta 2 | **Per-player enemy/boss scaling** (user idea 2026-09-23): host picks off/normal/hard; both sides must apply the same multiplier from the same member count or enemy HP desyncs. First measure vanilla boss max HP solo vs co-op (boss gauge `[0x05562878]`) - vanilla may already scale bosses |

Dream, DLC and Chalice were moved into beta 1 by the user (2026-09-23). Chalice needs a
`SHADPS4_BBSC_TRACE_WALLS` session inside a dungeon for its co-op wall ids; DLC areas may work with
the existing patches - test before building anything.

Donation pages are advertised only once open beta 1 is out (`DISTRIBUTION.md` item 8).

## Feasibility (assessed 2026-09-23, before any Dream/DLC/Chalice test)

| Item | Verdict | Main unknown |
| --- | --- | --- |
| Old Hunters DLC | Very likely, low effort | none known - vanilla allows co-op there; m34/m35/m36 already in the fog-wall list |
| Hunter's Dream together | Likely, medium | vanilla never allows co-op in m21; there may be extra session-ending gates. Doll level-up for a guest must be verified on a save copy |
| Chalice Dungeons | Doable, riskiest of beta 1 | wall ids need a live trace (list is empty on purpose); dungeons are generated from a glyph, so following the host *into* a dungeon from the Dream means the guest must build the same dungeon |
| UPnP / no Tailscale | Partly | no relay: strict/carrier-grade NAT still cannot connect directly |
| Reconnect | Likely, medium | host session must stay open past the 90 s partner-gone safety net |
| Personal loot | Likely, medium-high | the guest pickup gate (`EnableGetItem`, obj+0x1E2 & 0x20) and keeping item-lot flags out of network sync |
| Bosses count for the guest | Doable, hardest | story bosses set more than a defeat flag (e.g. Rom -> Blood Moon); copying only the defeat flag can soft-lock the guest's world. Per-boss research of the death scripts; save copies only |

Hard limits: same map for all players (session model), host + 2, CUSA03173 v1.09 only.

## First milestone (spec section 34)

1. Host enters another area → loading → guest follows → both remain connected → continue. *(Phases 1–3)*
2. Boss dies → no forced co-op termination → continue exploring. *(Phase 5)*
3. Player dies → respawns → session remains → continue. *(Phases 1, 3, 6)*

Only after these work does the project expand to full-game coverage (Phases 7–11).

## Deferred (decided 2026-09-24)
- **Separate co-op save + session backups.** Emulator-side: BBSC reads/writes its own copy of the
  save (started from the vanilla save the first time), so the vanilla save is never touched; plus an
  automatic backup of the co-op save at each session start (keep ~10). Chosen over blocking the
  guest's autosave, which would lose guest progress, risk save-wait hangs and hide snapshot bugs.
  A "guest saves blocked" switch stays an emergency option only.
