# Testing

## Setup

| Item | Value |
| --- | --- |
| Host PC | `<server-ip>` — runs shadNet (`server/start-server-seamless.bat`) and shadPS4 |
| Guest PC | friend, same tailnet |
| Client | `client-dev/shadPS4.exe` (send the same file to the guest) added in QtLauncher; start via `client/start-launcher-seamless-trace.ps1` |
| Env (both) | `SHADPS4_BLOODBORNE_SEAMLESS_COOP=1`, `SHADPS4_BLOODBORNE_RE_TRACE=1`, `SHADPS4_CAPTURE_BLOODBORNE_SUMMON=1` |
| Saves | **Copies only.** Never the 1222 h save |
| Collect | `captures/bloodborne-re/*.jsonl`, `summon-*.txt` (both PCs), `server/logs/*` |

Each run: note wall-clock start time, who is host, map/area, exact actions.

## Phase 0 behavior matrix (baseline, unmodified fork)

Result column: PASS / FAIL / PARTIAL / NOT RUN. Record stop caller + reason from trace.

| # | Scenario | Expected (from static analysis) | Result | Stop caller / reason |
| --- | --- | --- | --- | --- |
| B01 | Host starts, guest joins, same map | Joins, synced | PARTIAL 2026-09-15 (two instances, one PC): attempt 1 claim delivered but guest never joined; attempt 2 joined 15:13:33 but as covenant enemy (SummonType 5, same Vileblood character in both windows) | — |
| B08* | Guest killed (PvP, as enemy summon) | Session ends | PASS as predicted 2026-09-15 15:18:57 | Guest: `StageTransition` stop site `0x013CDEF6`, state 6, `state_1592=0`, reason `0xFF000023`, cleanup `0x013CDF17`, LeaveRoom room 2. Host 13 s later: `0x01789B60` site `0x01789F99` (state 3). |
| B02 | Cross-map summon (host Great Bridge, guest Dream) | Disputed (#2) | NOT RUN | |
| B03 | Both explore same map, doors/elevator/shortcut | Synced | NOT RUN | |
| B03a | Guest tries to pick up an item on the ground | Probably blocked for summoned player | NOT RUN | |
| B03b | Host picks up an item — does it disappear on the guest's screen? | Probably disappears (shared world flag) | NOT RUN | |
| B03c | Guest returns home — are echoes/items gained during co-op kept? | Probably kept | NOT RUN | |
| B04 | Enemy encounter, guest kills enemy | Synced | NOT RUN | |
| B05 | Boss fog + fight | Synced | NOT RUN | |
| B06 | Boss dies | Session ends (thanks-kick) | NOT RUN | |
| B07 | Host dies | Session ends | NOT RUN | |
| B08 | Guest dies | Guest leaves | NOT RUN | |
| B09 | Host lamp interaction in session | No prompt | NOT RUN | |
| B10 | Host silences, then lamp travel | Session ends, travel works | NOT RUN | |
| B11 | Hunter's Dream summon | Works (patched) | NOT RUN | |
| B12 | DLC area summon / transition | Unknown | NOT RUN | |
| B13 | Chalice Dungeon summon / floor transition | Unknown | NOT RUN | |
| B14 | Guest network cable pull 30 s | Game timeout path | NOT RUN | |
| B15 | Host closes game | Guest removed | NOT RUN | |
| B16 | Guest closes game | Host continues | NOT RUN | |
| B17 | shadNet restart mid-session | Unknown | NOT RUN | |
| B18 | Simultaneous death | Unknown | NOT RUN | |

## Phase 0 measurements to capture (read-only)

| ID | Value | Why |
| --- | --- | --- |
| M1 | `byte [[0x05556678]+0x1592]` before/after a lamp warp in session | Decides whether `StageTransition` itself kicks |
| M2 | Stop caller return address + `CSMultiPlayMan+0x124` for B06–B10 | Maps each gameplay event to its kick path |
| M3 | WorldChrMan remote entries after `0x015BED10` | Confirms remote cleanup semantics |
| M4 | `CSMultiPlayerIns` states on a same-map re-join | Feasibility of re-insertion without matchmaking |

M1 and M2 are **already recorded** by the release build's trace: every stage-warp record carries `stage_warp_state.state_1592`, and every `CSMultiPlayMan.StopRequest` record carries `caller_offset` and `multi_play_state` (`bloodborne_re.cpp` `WriteStageWarpState`, `MatchingLeaveDecision`). CONFIRMED (source). The fork's documents never mention `0x1592`.

M3 is recorded by the dev build `client-dev/shadPS4.exe` (trace name `WorldChrMan.RemoteCleanup`). M4 is covered by the existing `CSMultiPlayerIns.*` trace sites. Both players should use the dev build for baseline captures; it contains no protocol or behavior change, so it is compatible with the release build.

## Part 2 — Phase 1 persistence (same session, after Part 1)

Restart both games with the listed switch (`start-bbsc.ps1` / `start-launcher-seamless-trace.ps1`).

| # | Switches | Scenario | Pass | Result |
| --- | --- | --- | --- | --- |
| P1 | `-Persist` | Same-map join, host uses Silencing Blank | Native leave still happens | NOT RUN |
| P2 | `-Persist` | Host dies (reload) with guest present | Analyzer shows `PERSIST-KEPT`; shadNet room still exists; note whether partner visible after load | NOT RUN |
| P3 | `-Persist` | Guest dies | same | NOT RUN |
| P4 | `-Persist -Boss` | Kill a boss together | No forced leave; note guest state | NOT RUN |
| P5 | `-Persist -Lamp` | Host tries lamp while guest present | Prompt appears? travel? | NOT RUN |
| P6 | `-Persist -MatchCheck` | P2 again | Session survives load window | NOT RUN |
| P7 | `-Persist` | Guest network pull 30 s | Genuine timeout still ends session | NOT RUN |

Record for each: freezes/crashes, black screens, partner visible (yes/no), and whether rejoin by bells still works afterwards.

## Future feature tests (per phase)

Every feature: unit/logic test (shadNet broker tests, pure helpers) → controlled single-emulator test → two-player test → re-run full baseline matrix (regression).
