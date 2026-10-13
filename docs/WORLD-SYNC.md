# World synchronization, authority and progression

Status reflects evidence as of Phase 0. "Native" means Bloodborne's own netcode over the emulated P2P transport.

## Players

| State | Synchronized now? | Where | Evidence |
| --- | --- | --- | --- |
| Position / rotation / movement | Yes | Native | CONFIRMED (author live runs; issue #2 same-map play) |
| Animations / attacks / dodges | Yes (observed combat) | Native (`SprjChrBehaviorSyncModule`) | STRONGLY INDICATED |
| Visceral attacks / parries | Not observed | Native presumably | UNKNOWN |
| HP / death | Not captured | Native presumably | UNKNOWN |
| Healing, transformations, firearms | Not captured | Native presumably | UNKNOWN |
| Equipment / weapons / runes | Likely at insertion (`FirstSyncWait`) | Native | UNKNOWN |
| Character progression (level, stats) | Guest keeps own save | Guest save | STRONGLY INDICATED |

## Enemies

| State | Status | Evidence |
| --- | --- | --- |
| Existence / death | Synced | CONFIRMED (guest killed enemies, author) |
| Position, AI, target, aggro, stagger, status effects | Not individually verified | UNKNOWN |
| Authority | Probably host-authoritative with guest-local hit reporting | HYPOTHESIS |
| Respawn | Host world rules (lamp rest) | STRONGLY INDICATED |

## Bosses

| State | Status | Evidence |
| --- | --- | --- |
| Fog entry, spawn, fight, position | Synced | CONFIRMED (author: "boss state and combat remained synchronized") |
| HP / phase | Not individually verified | UNKNOWN |
| Boss death | Ends session (guest thanks-kick, `BlockClear*`) | CONFIRMED names/static |
| Rewards | Guest reward path unknown (Insight to guest in retail) | STRONGLY INDICATED |
| Post-boss state | Host world flags | STRONGLY INDICATED |

## World objects and events

| Item | Status | Evidence |
| --- | --- | --- |
| Doors / shortcuts / elevators / switches | Event-script driven; EMEVD events are network-synced unless `SetNoNetSync` | STRONGLY INDICATED |
| Lamps | Event 7200 synced; prompt suppressed in session | CONFIRMED |
| Pickups / item lots | Per-player | UNKNOWN |
| NPC / quest state | Host world flags | STRONGLY INDICATED |
| Boss completion / area progression | Host world event flags | STRONGLY INDICATED |

Event-flag functions (static, user eboot):

| Function | Address | Status |
| --- | --- | --- |
| Script `SetEventFlag` wrapper | `0x01258310` | CONFIRMED (registered by name) |
| `SprjEventFlagMan` set (flag, value) | `0x013CFCC0` | STRONGLY INDICATED |
| get (flag, width 8) | `0x013CFD80` | STRONGLY INDICATED |
| set value (flag, width 8, value) | `0x013D0060` | STRONGLY INDICATED |
| `ClearMyWorldState` script binding | registered in `0x0133B370` near `0x01338C50` | STRONGLY INDICATED |

## Authority

| Concern | Owner | Status |
| --- | --- | --- |
| NP room | Host (CreateJoinRoom) | CONFIRMED |
| World, map, event flags, progression | Host | CONFIRMED (guest is inserted into host world) |
| Enemy / boss AI | Host | HYPOTHESIS |
| Player character | Each player | STRONGLY INDICATED |
| Area transitions | Host; guest follows only through summon placement | CONFIRMED |

Design rule: keep this model. Seamless co-op = guest continuously lives in the host world.

## Progression model — USER DECISION 2026-09-15

| Item | Host | Guest |
| --- | --- | --- |
| Level, echoes, Insight | Own save | Own save (guest progresses normally; levels at the Doll) |
| Items / pickups | Host can take any item | Guest can take any item |
| **Loot instancing** | An item taken by one player **stays available** for the other (personal loot) | same |
| Items saved | Host save | Guest save |
| Bosses killed together | Normal | Boss-defeated flags applied to guest save (allowlist) |
| NPC / quest flags | Normal | Not copied (save-corruption risk) |
| Shortcuts / lamps | Normal | Not copied unless user decides later |

Implementation requirements (Phase 4), each needs RE before code:

| Requirement | Native behavior | Needed change | Status |
| --- | --- | --- | --- |
| Guest can pick up items | Summoned players likely cannot | Locate pickup/interaction gate for summoned role | STRONGLY INDICATED gate exists (retail); test B03 |
| Pickup does not remove item for partner | Item-lot flag set in host world; event flags network-synced | Exclude item-lot/treasure flags from network sync; keep local pickup | UNKNOWN path (flag setter `0x013CFCC0` known) |
| Guest item persists in guest save | Inventory changes kept | none expected | STRONGLY INDICATED; test |
| Guest levels up with partner present | Doll only in Hunter's Dream | Phase 8 | UNKNOWN |

Rules: allowlist only, idempotent, logged, never on the only copy of a save.
