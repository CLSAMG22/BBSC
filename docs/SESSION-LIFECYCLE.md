# Session lifecycle

## Bloodborne session objects

| Object | Address | Role | Status |
| --- | --- | --- | --- |
| `CSMultiPlayMan` singleton | `[0x05540290]` | Session policy; state at `+0x124` | CONFIRMED |
| Matching controller | wrapper at `CSMultiPlayMan+0x18` | NP room; states 0 idle, 1 start, 2/3 established, 4 stopping | CONFIRMED |
| `SprjSessionManager` | singleton used by all stop callers | Event callbacks (`OnMatchingCheck`, `OnMatchingError`, …) | CONFIRMED |
| `CSMultiPlayerIns` | `0x01E4FCF0`–`0x01E50D70` | Remote character insertion task | CONFIRMED |
| WorldChrMan | `[0x0553E878]` | Character table (remote players live here) | CONFIRMED |
| Global game state | `[0x05556678]` | Forced warp `+0x1520`, placement banks, `+0x1592` | CONFIRMED |

`CSMultiPlayMan+0x124`: 0 idle · 1 create starting · 2 create failed · 3 create established (host) · 4 join starting · 5 join failed · 6 join established (guest) · 7 leaving · 8 unknown writer. CONFIRMED.

## Lifecycle

```
Guest rings Small Resonant ─► /create (Advertised)
Host rings Beckoning ───────► /get ─► candidate filters ─► request builder ─► /request (Claimed)
Host CreateJoinRoom [0→1→3] ─► guest /create receives claim (Delivered) ─► JoinRoom [0→4→6]
Signaling ESTABLISHED / MUTUAL_ACTIVATED
CSMultiPlayerIns: Init → StartNotifyWait → StartWait → FirstSyncWait → Create → Update
Guest SummonedMapReload (guest world replaced by host world)
PLAYING
  └─► any kick path ─► CSMultiPlayMan::Stop [→7] ─► WorldChrMan remote cleanup 0x015BED10
                     ─► stop task (reason 0xFF000023) ─► member notify ─► LeaveRoom ─► [0]
```

## THE KICK — CONFIRMED (static, user eboot)

Every voluntary session end is the pair **`Stop 0x01ED07A0` → `0x015BED10`**.

* `Stop` is called from **43** sites; `0x015BED10` from **26**.
* `Stop` does nothing in states 0/2/5 (mask `0x25`), otherwise sets state 7 and enqueues the stop task.
* `0x015BED10` (1216 bytes, WorldChrMan) removes remote characters. STRONGLY INDICATED.

| Trigger | Caller(s) | Evidence |
| --- | --- | --- |
| Any stage transition (lamp, headstone, event warp, death respawn load) | `StageTransition 0x013CDE30` → Stop + cleanup, **skipped when `byte [global+0x1592] != 0`**; helper `0x013CDDA0` unconditional | CONFIRMED static |
| Stage change while host (states 1/3) | stage update `0x0193AC10` @ `0x0193C657` | CONFIRMED static |
| Stage task destructor (reload phase ≠ 3) | `0x019470F0` @ `0x019471B1` (retargeted by shadp2p) | CONFIRMED + live (author) |
| Remote character missing (e.g. during reload) | `OnMatchingCheck 0x013808B0` @ `0x013809B7` | CONFIRMED + live (author) |
| Boss clear / guest "thanks" kick | `0x0138B2D0` (`BlockClearAnim`, `Failed_BossAreaMission_LeaveMap`); `OnBeThxKickOut` in `0x01789B60` | CONFIRMED names |
| Death / revive | `OnReviveMagic_1 0x01389D20`; `HostDead`, `PartyGhostDeath` (stage-load paths) | CONFIRMED names |
| Limits | `OnLeave_Limit` `0x01388510`, `0x01389FB0` | CONFIRMED names |
| Join-flow aborts | 10 `SummonReloadStart` handlers `0x01388680…0x013892E0`, `0x01389440`, `0x01389620`, `0x013849A0` | CONFIRMED static |
| Network failure | `0x01789B60` (`OnRoomTimeOut`, `OnIrregularLeaveSession`), `0x0178B5E0` (`OnFailedJoinSession`, `OnBeKickOut`, `OnJoinClearedRoom`), `0x01E57210` + `0x013160F0` (`OnLanCutError`), `OnP2PTimeOut` | CONFIRMED names |
| Error/consistency | `OnMatchingError 0x013809F0`, `0x01332F60`, `0x013858A0`, `0x013859F0`, `0x015BEC90`, `0x01789190`, `0x0178F650`, `SosStatus.Update 0x01872360` | CONFIRMED static |
| Room kicked/destroyed | Matching2 events `0x1103`/`0x1104` → controller stop | CONFIRMED static |

Not the source: shadNet (never kicks; retains), shadp2p signaling (only pending timeouts), emulator timeouts. STRONGLY INDICATED.

## Persistent session — can it exist?

### What the kick destroys

1. **Game session state** (`CSMultiPlayMan` state, members, requests).
2. **NP room + signaling** (LeaveRoom).
3. **Remote characters** in WorldChrMan (`0x015BED10`).
4. **The guest's borrowed world** (guest reloads its own save world).

### What must be true for "one continuous session"

| Requirement | Assessment | Status |
| --- | --- | --- |
| A. Room + signaling survive a structural stop | Single choke point: a policy hook at `Stop` entry can classify the caller (return address) as *structural* (stage transition, stage policy/destructor, boss clear, death, OnMatchingCheck during load) vs *genuine* (LAN cut, room/P2P timeout, kicked, failed join, explicit leave) | STRONGLY INDICATED feasible |
| B. Game session state stays established while a map loads | Must skip Stop **and** keep state 3/6 consistent with an empty WorldChrMan; `OnMatchingCheck` must tolerate the load window | HYPOTHESIS |
| C. Both processes load the same destination | Host's destination is known at `StageTransition`/WarpParam; guest can be driven to it with the fork's forced-placement + `StageTransition` calls (author proved cross-map warp) | STRONGLY INDICATED |
| D. Remote characters re-created after load without a new summon | Needs a native re-insertion path (`CSMultiPlayerIns` re-run for an existing member, `SummonReloadStart`/`ReqReloadRemoteProbe`) | **UNKNOWN — the critical research question** |
| E. Guest keeps using host world across loads | Guest must load *host* world data again, not its own save world (`SummonedMapReload` semantics) | UNKNOWN |

Author's earlier experiments that only retained the room (fake leave, no callback, aborted callback) failed — room retention alone is insufficient; B, D and E are required. CONFIRMED (author notes).

### Architecture if D/E are solvable (target)

```
Persistent layer (shadp2p client + shadNet party)
   ├─ keeps NP room/signaling (suppress structural Stop)
   ├─ coordinates destination (host → guest)
   ├─ drives both loads
   └─ re-runs native insertion for the existing member
Bloodborne native session: established throughout; characters recreated after each load
```

### Fallback if D/E are not solvable

Keep the room and party, but on each structural event re-run the **join** (not matchmaking): guest
re-joins the same room and runs a native summon insertion. No bells, no search, no new room.
Visible cost: one loading screen plus insertion. This still meets "no unnecessary re-summoning"
from the user's perspective.

### Answer

A persistent external session is realistic (A, C). Whether Bloodborne's **internal** session can
stay continuous across a load depends on D/E, which only live traces can settle. Phase 1 must be
designed so both the target and the fallback share the same code (Stop classification + coordination).
