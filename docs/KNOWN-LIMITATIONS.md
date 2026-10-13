# Known limitations

## Hard (engine/executable)

| Limitation | Detail | Status |
| --- | --- | --- |
| Session bound to one loaded stage | 43 Stop callers; stage transition, stage policy, stage destructor and `OnMatchingCheck` all end the session around a load | CONFIRMED static |
| No native "follow host to new stage" | Guest only enters host world through summon insertion + `SummonedMapReload` | CONFIRMED |
| Players on different maps simultaneously | Not supported by Bloodborne's session model (unlike Elden Ring's open world) | STRONGLY INDICATED |
| Player count | Host + 2 cooperators (native capacity/role rules) | STRONGLY INDICATED |
| Gameplay packet format undocumented | Blocks any approach that edits or replays game state | CONFIRMED |
| Single executable | All offsets/signatures for CUSA03173 01.09 sha256 `d65f0b4f…` only | CONFIRMED |

## Foundation (shadp2p / shadNet)

| Limitation | Detail | Status |
| --- | --- | --- |
| Cross-map seamless summon disputed | shadp2p#2 | CONFIRMED report |
| Lamp prompt suppressed in session | second gate unknown after `+0x49` | CONFIRMED |
| Death persistence unfinished | shadnet-p2p#3 | CONFIRMED |
| "Failed to acquire Bloodborne Server information" for some users | shadp2p#1, cause unknown | CONFIRMED report |
| No relay | peers need direct reachability (Tailscale) | STRONGLY INDICATED |
| Rooms/summons in memory | server restart ends all sessions | CONFIRMED |
| Game TCP unencrypted | private tailnet only | CONFIRMED |
| Pre-existing crash | `GXRenderThread` access violation (author notes) | CONFIRMED report |

## Open research blockers (Phase 1 gating)

1. Native re-insertion of an existing room member after a load (SESSION-LIFECYCLE requirement D).
2. Meaning/runtime value of `global+0x1592`.
3. Second lamp prompt gate.
4. Host migration: room ownership moves in shadNet, but Bloodborne world authority cannot move to the guest's save. Considered **not realistic**; documented, not pursued.

## Environment

| Item | Status |
| --- | --- |
| Fork build | Works: clang-cl 19.1.5 + NMake (single-threaded, full build ≈ 1 h, incremental minutes) |
| Account registration tool | Built (`server/tools/shadnet-sample.exe`) |
