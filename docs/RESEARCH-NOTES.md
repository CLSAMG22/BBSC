# Research notes

## 2026-09-15 — Seamless summon stall (guest never joins)

Five same-map attempts (two instances): OK 15:13, OK 15:44 (and 15:32 after a retry); FAIL 14:54, 15:29, 20:20.

| Step | Successful attempts | Stalled attempts | Status |
| --- | --- | --- | --- |
| Host search → claim → `sceNpMatching2CreateJoinRoom` | yes | yes | CONFIRMED |
| Both `RequestSignalingInfos` (host→guest by NPID, guest→host mid 17) | yes | yes | CONFIRMED |
| NpSignaling `MUTUAL_ACTIVATED` pair | yes | yes | CONFIRMED (shad_log) |
| **Guest `sceNpMatching2JoinRoom`** | within 0.2–3 s | **never** | CONFIRMED |
| Guest `CSMultiPlayerIns.*` | starts immediately | never | CONFIRMED (captures) |
| Server "delivered claim … host-placement-bytes 53" | not needed | ~25–30 s later, repeated, never leads to a join | CONFIRMED |
| Room 3 (15:32) joined only after a **second** signaling connection pair (new connIds) ~72 s later | — | — | CONFIRMED |

Conclusion: the stall is between NpSignaling activation and the guest game deciding to call JoinRoom — i.e. the
game-level summon message exchanged right after activation is not acted on. Hypotheses (untested): (1) the first
P2P game datagram is dropped because the receiving side's signaling/vport isn't ready yet (`P2P RX no socket for
dst_vport` is TRACE-only), (2) the guest's seamless pre-match state (`pre_match_guest_warp=destination_reached`,
placement header from a delivered claim) interferes with native summon handling, (3) host retries reuse the active
signaling connection, so no new activation is delivered and the game never re-sends.

Isolation test: traditional mode (`start-server-traditional.bat` + `-Traditional`). If it also stalls → transport
race (1)/(3); if not → seamless path (2).

## 2026-09-15 — First Phase 1 test: guest death with `-Persist`

Two instances, room 4, persistence enabled (10 sites). Guest killed at 15:45:53.

| Observation | Evidence | Status |
| --- | --- | --- |
| Guest `StageTransition` Stop + cleanup skipped (`PERSIST-KEPT` from `0x013CDF1C`), no LeaveRoom, host never stopped, room 4 kept | capture + server log | **CONFIRMED live** |
| Host still sees the guest character and its movement after the guest respawned | user observation | **CONFIRMED live** — guest→host replication survives |
| Guest sees no host; guest's enemies frozen, kills have no effect | user observation | **CONFIRMED live** — guest's world rebuilt by the load; host character and synced state not re-created on the guest |
| Enemy simulation is host-authoritative (client waits for host state) | frozen enemies while guest remains in client state 6 | STRONGLY INDICATED |

Phase 2/3 target narrowed: **guest-side only** re-insertion of the existing host member + world resync after the guest's own load. Host side needs nothing.
Hypothesis to test next: after the guest's load completes, re-deliver the Matching2 member-joined notification for the host member (HLE side, `np_matching2_mm.cpp`) so Bloodborne runs `CSMultiPlayerIns` for it natively.

## 2026-09-15 — First live kick capture (two instances, baseline build)

Capture `test-results/bbsc-logs-20260915-152140` (guest p5404, host p5364). Guest was summoned
as a Vileblood enemy (SummonType 5) and killed by the host.

| Time | Process | Event | Status |
| --- | --- | --- | --- |
| 15:13:33 | guest | `SummonedMapReload.Native` (join) | CONFIRMED live |
| 15:18:57.400 | guest | `Warp.StageTransition` with `state_1592 = 0` → `CSMultiPlayMan::Stop` from `0x013CDEF6` in state 6 → stop task reason `0xFF000023` → remote cleanup from `0x013CDF17` → `LeaveRoom` room 2 | **CONFIRMED live** — the static prediction for death/reload kicks |
| 15:19:08.934 | guest | Stop from `0x0178A6BE` (in `0x01789B60`) while idle — no-op | CONFIRMED live |
| 15:19:10.270 | host | Stop from `0x01789F99` (in `0x01789B60`) in state 3 → leave room 2 | CONFIRMED live — host-side reaction to the guest leaving |

Consequences:
* `SHADPS4_BBSC_PERSIST` patches exactly the guest-side site observed (`0x013CDEF6`/`0x013CDF17`).
* Two of the six `0x01789B60` sites are now identified: `0x01789F99` = host ends session after partner left; `0x0178A6BE` = follow-up no-op. `0x01789F99` must stay native only if the partner genuinely left; with persistence the guest should no longer leave, so it should not fire.

Static analysis of the user eboot (CUSA03173 01.09) with `tools/bloodborne-xref-scan.exe`
(fork's Zydis tool, built locally via `build/build-xref-scan.cmd`).

## 2026-09-15 — Lamp prompt second gate (Phase 7)

Lamp action update `0x012F5C40`, availability block `0x012F8260`–`0x012F83F0`:

| Byte | Written at | Meaning | Status |
| --- | --- | --- | --- |
| `+0x48` | `0x012F8260/0x012F82BD` | lamp event-flag / area condition (`0x01399BF0`) | CONFIRMED static |
| `+0x49` | `0x012F8323` | `[[global+0x16F8]+0x14] > 1` (session players) **or** `CSMultiPlayMan` pending vector non-empty | CONFIRMED static |
| `+0x4A` | `0x012F836A` | lamp entity object missing / `+0x270` null | CONFIRMED static |
| `+0x4B` | `0x012F83C5` | result of `call 0x0191B6E0` (only evaluated when 48/49/4A are clear) | CONFIRMED static |

* The shadp2p hook at `0x012F836E` clears `+0x49` for the host (SpEffect 9001) **before** `+0x4B`
  is computed. Its reported "all four bytes zero" read a stale `+0x4B`. STRONGLY INDICATED.
* The prompt path at `0x012F83CF`–`0x012F83F0` jumps to the enabled branch only when
  `+0x4A == 0`, `+0x48 == 0`, and `(+0x49 | al) == 0`; `al` is the `+0x4B` value. CONFIRMED static.
* `0x0191B6E0(WorldChrMan, lampEntityId)` iterates WorldChrMan character groups (`+0x278` count,
  `+0x280` entries, 0x18 stride), skips one group (likely local), and returns 1 if any character
  bound to that entity has a flag set in table `0x05127B90+0x260` indexed via `0x018C2910`.
  Gameplay meaning: HYPOTHESIS (character of blocking type bound to the lamp entity during multiplayer).
* **Implemented (experimental):** `SHADPS4_BBSC_LAMP=1` retargets the single call at
  `0x012F83BC` to native `xor eax,eax; ret` `0x01300AA0`. Guest `+0x49` remains native (guest
  still cannot use lamps); host needs the shadp2p seamless hook for `+0x49`.

## 2026-09-15 — Item pickup (Phase 4)

| Finding | Evidence | Status |
| --- | --- | --- |
| Debug display `0x018CF620` prints `EnableGetItem: %d` from `bextr [obj+0x1E1], bit 13` → byte `obj+0x1E2`, mask `0x20` | disassembly `0x018CFC9E`–`0x018CFCA8` | CONFIRMED static |
| The displayed object is a character instance (same function reads `[r12+0x1C8]`) | disassembly | STRONGLY INDICATED |
| No direct `or/and [..+0x1E2], 0x20/0xDF` writer found; writes are likely register-computed bitfield stores | memory-displacement scan (77 `+0x1E2`, 343 `+0x1E1`, 3859 `+0x1E0` operands) | UNKNOWN writer |
| Object-action replication through event flags: `RepolicateObjActInfo by event flg`, `Target ObjActIdx Network State` in `0x013AC610` | string xrefs | CONFIRMED names |
| Treasure/pickup script bindings `EnableObjTreasure`, `AddTreasureEvent` registered in `0x0133B370` | string xrefs | CONFIRMED |
| `ObjTreasureInfo` referenced in `0x01EF5070`; `Playlog_PickUpItem` only in playlog table `0x01EB65E0` | string xrefs | CONFIRMED |
| Item lots are `ItemLotParam` / `ItemLotLvdepParam` | UTF-16 names | CONFIRMED |

Next: runtime read of `playerChr+0x1E2 & 0x20` for host vs summoned guest (needs a small read-only
observer on the character update), and a trace on flag setter `0x013CFCC0` during a pickup to get
the item-lot flag range for "personal loot".

## 2026-09-15 — Character re-insertion (Phases 2–3)

| Finding | Evidence | Status |
| --- | --- | --- |
| `CSMultiPlayerInsTask` step table registered in static init at `0x01E50E30`+ (steps `0x01E4FCD0`, `0x01E4FCF0` … `0x01E50D70`) | `lea` sequence `0x01E50FD0`–`0x01E51156` | CONFIRMED static |
| `0x01E50D70` (DeleteWait) resets `+0x54 = -1` and clears `global+0x1521` for roles 1–2 | disassembly | CONFIRMED static |
| Task instance creator | not found statically (steps reached via scheduler / vtables) | UNKNOWN |
| `ReqReloadRemoteProbe` referenced by 20+ character/map functions (`0x01547650`, `0x01595AD0`, `0x015B7570`, `0x01889520`, `0x01913A40`, `0x01F0FD30` …) | string xrefs | CONFIRMED names; semantics UNKNOWN |
| Script bindings `OnSessionJustIn`, `OnSessionIn`, `IsWhiteGhost_NetPlayer` in `0x0133B370` | string xrefs | CONFIRMED |

Next: with Phase 1 persistence on, capture whether `CSMultiPlayerIns.*` steps run again after a
kept stage load (M4). If they do not, locate the creator from a live frame chain on `Init`.
