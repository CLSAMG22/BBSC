# Architecture

## Version matrix

| Component | Version | Notes |
| --- | --- | --- |
| Bloodborne | CUSA03173 EU GOTY, app 01.09, FW 4.50 | `eboot.bin` SHA-256 `d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9` |
| shadp2p (shadPS4 fork) | `6e97b40` (2026-08-16) | Release `shadps4-win64-sdl-2026-08-16-6e97b40.zip` |
| shadnet-p2p | `5a849a1` (2026-08-16), server version 0.0.13 | Release `shadnet-win64-2026-08-16-5a849a1.zip` |
| Launcher | shadPS4QtLauncher 2026-09-01 `bf2c711` | Upstream; loads the fork as a custom build |
| Protocol | shadp2p host-placement header v1 | `X-ShadPS4-Bloodborne-Host-Placement` |

Every offset in these docs is valid only for the executable hash above.

## Stack

```
Bloodborne (eboot)          session policy, summon, insertion, netcode, world, events
    │  PS4 NP / net / HTTP calls
shadPS4 fork (shadp2p)      HLE: NpManager, NpMatching2, signaling, P2P sockets, libSceHttp
    │                       + bloodborne_re.cpp: byte patches, hooks, trace observers
    ├─ TCP 31313 ─────────► shadNet: accounts, Matching2 worlds/rooms
    ├─ UDP 31314 ─────────► shadNet: STUN reflector
    ├─ TCP 31315 ─────────► shadNet: WebAPI + Bloodborne SummonBroker (/summon_messenger)
    ├─ HTTPS ─────────────► thehuntersdream.com (other FromSoftware online APIs, via host_overrides)
    └─ UDP (P2P) ─────────► remote shadPS4 process (direct, over Tailscale; no relay)
```

shadp2p is **the shadPS4 fork**, not a layer between shadNet and the peer. CONFIRMED.

## Layer responsibilities

| Concern | Owner | Evidence |
| --- | --- | --- |
| Bell eligibility, SOS state | Bloodborne (patched by shadp2p in seamless mode) | CONFIRMED |
| Summon advertisement storage, search, claim delivery | shadNet `SummonBroker` | CONFIRMED |
| NP room create/join/leave | Bloodborne `CSMultiPlayMan` → shadPS4 HLE → shadNet Matching2 | CONFIRMED |
| Peer address discovery | shadNet STUN + shadp2p handshake | CONFIRMED |
| Gameplay packet transport | shadPS4 `P2PTransport` (UDP) | CONFIRMED |
| Gameplay packet contents | Bloodborne (format undocumented) | UNKNOWN |
| Character insertion, placement, map reload | Bloodborne `CSMultiPlayerIns`, `SummonedMapReload` | CONFIRMED |
| Session termination policy | Bloodborne (`Stop 0x01ED07A0` + cleanup `0x015BED10`) | CONFIRMED (static) |
| World events network sync | Bloodborne EMEVD (`SyncEvent`, `SetNoNetSync`) | STRONGLY INDICATED |
| Accounts/persistence | shadNet SQLite (`db/shadnet.db`); rooms and summons in memory only | CONFIRMED |

## shadp2p additions (fork vs upstream)

| Kind | Items | Status on user eboot |
| --- | --- | --- |
| Byte patches (seamless) | 13: bell area ×4, active bell ×2, responder range, SOS status, candidate area, builder area/world/negative-event, stage-destructor Stop retarget `0x019471B1` | 13/13 original bytes match. CONFIRMED |
| Native calls | forced placement setters, selector `0x01332BC0`, `SummonedMapReload 0x01336B90`, `StageTransition 0x013CDE30`, reload state `0x0178D9A0`, `UseItem 0x018F9720` | 9/9 signatures match. CONFIRMED |
| Functional hooks | `SummonBuild.Entry 0x01874710`, `SosStatus.Update 0x01872360`, `CrossMapGuestHandoff 0x01E4FF3A`, `StageDescriptor.Finalize 0x01944F23`, `HealingFountain.Availability.Host 0x012F836E` | Prologues match. Logic unverified by us |
| Trace observers | 146 read-only sites, JSONL under `captures/bloodborne-re/` | 146/146 match. CONFIRMED |
| Transport | P2P sockets (vport multiplexing), Matching2 signaling handshake | CONFIRMED |

## Field reliability of the foundation

* Same-map co-op through the fork works for third parties. CONFIRMED (issue shadp2p#2 reporter).
* Cross-map seamless summon: author reports success; issue shadp2p#2 reports it never joins. **Disputed — must be reproduced.**
* Death persistence: never finished (shadnet-p2p#3 open). CONFIRMED.
* Lamp travel in session: not working (author). CONFIRMED.
