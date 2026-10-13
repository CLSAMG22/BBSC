# Phase 1 design — persistent connection (Stop policy)

Status: **IMPLEMENTED (experimental, Option A)** in `src-shadp2p/src/core/bbsc_persistence.cpp`
on branch `bbsc/phase0-observers`, 2026-09-15. Off by default. Not yet tested in game.

| Switch | Groups | Call sites |
| --- | --- | --- |
| `SHADPS4_BBSC_PERSIST=1` | stage loads, revive | 10 |
| `+ SHADPS4_BBSC_PERSIST_BOSS=1` | boss-area leave | +2 |
| `+ SHADPS4_BBSC_PERSIST_MATCHCHECK=1` | OnMatchingCheck | +2 |
| `+ SHADPS4_BBSC_LAMP=1` | lamp 4th gate (RESEARCH-NOTES.md) | +1 |

All 15 sites byte-verified against the user eboot; all-or-nothing install. Telemetry: every skipped
cleanup hits trace site `BBSC.Persist.SkippedCleanup` (`0x013001E0`), summarized by
`tools/analyze_captures.py`.

Goal (spec §26 Phase 1): host + guest connection survives Bloodborne's normal session termination.
Non-goal for Phase 1: re-inserting characters after a load (Phase 2/3), world sync.

## 1. The choke point

Every voluntary session end is `CSMultiPlayMan::Stop 0x01ED07A0` followed by WorldChrMan remote
cleanup `0x015BED10` (SESSION-LIFECYCLE.md). CONFIRMED static. Phase 1 decides, per call, whether
that pair runs.

## 2. Classification

| Class | Meaning | Action in persistent mode |
| --- | --- | --- |
| **structural** | Normal gameplay boundary: stage transition, stage policy/destructor, boss clear, revive/death reload | Keep session: skip Stop + cleanup, emit `persist` event |
| **genuine** | Real network loss or explicit leave: LAN cut, room/P2P timeout, irregular leave, kicked, failed join, Silencing Blank | Run native Stop (unchanged) |
| **join** | Aborts inside the summon/join flow | Native (unchanged) — protects join correctness |
| **conditional** | `OnMatchingCheck`: missing remote character | Structural only while a coordinated load is in progress (grace window); otherwise genuine |
| **unknown** | Not yet mapped by captures | Native (unchanged) until classified |

Initial table (to be confirmed by captures; `tools/analyze_captures.py` prints these labels):

| Caller function | Stop site | Cleanup site | Proposed class |
| --- | --- | --- | --- |
| `StageTransition 0x013CDE30` | `0x013CDEF6` | `0x013CDF17` | structural |
| stage helper `0x013CDDA0` | `0x013CDDF5` | `0x013CDE26` | structural |
| stage update `0x0193AC10` | `0x0193C657` | `0x0193C689` | structural |
| stage flow `0x01939470` | `0x01939876` | — | structural |
| stage task destructor `0x019470F0` | `0x019471B1` | — | structural (fork already retargets) |
| `0x0138B2D0` BlockClearAnim / boss-area leave | `0x0138B410` | `0x0138B437` | structural |
| `OnReviveMagic_1 0x01389D20` | `0x01389DDD` | `0x01389DFE` | structural |
| `OnMatchingCheck 0x013808B0` | `0x013809B7` | `0x013809E8` | conditional |
| `0x01789B60` network events | 6 sites | — | per-site after capture (contains `OnBeThxKickOut` = structural, `OnRoomTimeOut` = genuine) |
| `0x0178B5E0` join/kick events | `0x0178C118` | — | genuine / join |
| `OnLanCutError` `0x01E57210`, `0x013160F0` | `0x01E57528`, `0x0131616B` (jmp) | — | genuine |
| `OnIrregularLeaveSession` `0x017894E0`, `0x0178F9A0` | `0x0178980D`, `0x0178FA3D` | — | genuine |
| `OnMatchingError 0x013809F0` | `0x01380A65` | `0x01380A8D` | genuine |
| SummonReloadStart family, `ForceSummonSuccess` | 12 sites | 12 sites | join |
| `OnLeave_Limit` ×2, helpers `0x01332F60`, `0x013858A0`, `0x013859F0`, `0x015BEC90`, `0x01789190`, `0x0178F650`, `SosStatus.Update`, `0x013CBAD0` | — | — | unknown → native |

## 3. Mechanism options

The fork's `InstallGuestCodeHook` passes registers **read-only** (`cpu_patches.h`); all existing
behavior changes are byte patches. Two ways to act:

### Option A — per-site call retarget (proven pattern)

Rewrite a structural `call 0x01ED07A0` to `call 0x01ED00A0` (native "has matching controller"
predicate), exactly as the fork does at `0x019471B1`, and the paired `call 0x015BED10` to a no-op
target. Applied at load only when `SHADPS4_BBSC_PERSIST=1`.

* Pro: uses a validated technique; byte-signature checked; no emulator core change.
* Con: static for the whole run — cannot switch per situation (e.g. host actually wants to leave through a
  structural path); the predicate returns *true* when a room exists, so callers take their
  "stopped successfully" branch — each site's follow-up code must be checked by disassembly.

### Option B — decision hook (new, emulator core)

Extend `cpu_patches` with `InstallGuestDecisionHook(address, expected, tag, hook)` where the host
callback returns `{continue | skip_call(rax=value)}`. Install at each classified call site (5-byte
`call rel32`, relocated as an absolute call in the trampoline).

* Pro: runtime policy (grace windows, party state, user leave intent, telemetry per decision).
* Con: new trampoline code path in the emulator core; must preserve red-zone/stack alignment rules
  used by the existing hook generator; needs its own tests.

**Recommendation:** Option A for the first proof on the structural stage sites only (smallest
change, spec §30), with telemetry via existing observers. Move to Option B when Phase 2 needs the
`OnMatchingCheck` grace window, which Option A cannot express.

## 4. What Phase 1 must prove (acceptance)

| ID | Test | Pass condition |
| --- | --- | --- |
| P1-1 | B01 same-map join, then host silences | native leave still works (genuine path untouched) |
| P1-2 | Structural stage load in session (host death reload or event warp; lamp gate is out of scope) | Stop recorded as `skipped`, NP room still present on shadNet, signaling not DEAD |
| P1-3 | Guest network pull 30 s | genuine timeout still ends session |
| P1-4 | Regression B01–B18 | no new crashes; join flow unchanged |

Phase 1 success is **connection persistence only**. Expect the partner to be invisible after a
load until Phase 2/3 re-insertion exists; that is the measurement M4 is for.

## 5. Risks

| Risk | Mitigation |
| --- | --- |
| Skipping Stop leaves `CSMultiPlayMan` in an established state the game no longer expects after a load → hangs or later crashes | Structural sites only; run behind env flag; capture `CSMultiPlayerIns` states (M4) |
| `OnMatchingCheck` stops the session anyway after the load | Expected in Phase 1; addressed by Phase 2 grace window |
| Boss-clear skip breaks reward/progression events | Keep `0x0138B2D0` native until B06 capture shows its follow-up branch |
| Save corruption | Test saves only; no flag writes in Phase 1 |

## 6. Telemetry (spec §28)

New JSONL record `bbsc_stop_decision`: time, caller site, class, decision, `CSMultiPlayMan` state,
room id, `state_1592`, reload phase, map. One record per Stop call (not per frame).
