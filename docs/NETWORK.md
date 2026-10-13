# Network

## Channels

| Channel | Transport | Endpoint | Content |
| --- | --- | --- | --- |
| Login + Matching2 | TCP 31313 (unencrypted) | shadNet | protobuf commands (`shadnet.proto`): auth, worlds, rooms, room events |
| STUN | UDP 31314 | shadNet | 21-byte ping `{cmd=0x01, online_id[16], local_ip}` every 5 s; server reflects mapped address |
| Summon broker | HTTP TCP 31315 | shadNet | `/summon_messenger/create|get|request|delete` (game JSON) + placement header |
| FromSoftware online API | HTTPS → HTTP | thehuntersdream.com | messages/bloodstains/etc. (`ss4.scej-network.jp` override) |
| Gameplay | UDP, one physical socket per process | peer | Bloodborne netcode through PS4 P2P sockets |
| Account service (BBSC) | HTTP TCP 31316 | host (`server/account_service`) | `/api/register`, `/api/verify`, `/api/resend`, `/api/status` — email-verified sign-up; creates shadNet accounts with `RegistrationSecretKey` |

Server in this project: `<server-ip>` (Tailscale), seamless enabled. See `server/shadnet.cfg`.

## shadp2p P2P transport (`src/core/libraries/network/p2p_sockets.cpp`) — CONFIRMED

* One UDP socket per emulator process (`SHADPS4_P2P_PORT` or runtime-assigned).
* Every datagram: 4-byte header `[source vport u16 BE][dest vport u16 BE]` + payload (≤ UDP max − 4).
* vport `0xFFFF` is reserved for signaling; game sockets bind a vport (0 = ephemeral from 30000).
* No reliability, ordering, sequence numbers, acks or timestamps at this layer. Anything of that kind is Bloodborne's own protocol inside the payload. UNKNOWN.
* Transport lifetime is the process; it is not tied to a room. STRONGLY INDICATED.

## Matching2 signaling (`np_matching2_signaling.cpp`) — CONFIRMED

Packet (0x32 bytes, packed):

| Offset | Field | Notes |
| --- | --- | --- |
| 0x00 | magic `SHAD` | |
| 0x04 | type `0x21` | |
| 0x05 | kind | 1 Offer, 2 Accept, 3 Check, 4 CheckAck |
| 0x06 | room_id u64 | |
| 0x0E | from_member_id u16 | |
| 0x10 | to_member_id u16 | |
| 0x12 | online_id_from[16] | |
| 0x22 | mapped_addr u32, mapped_port u16, reserved u16 | UDP source endpoint preferred over self-report |
| 0x2A | nonce u64 | RTT from Check → CheckAck |

* Retry 350 ms; **pending** handshake dead after 10 s → `SIGNALING_EVENT_DEAD`.
* Established peers have no emulator-side keepalive timeout. STRONGLY INDICATED.
* `DEAD` is also raised when a room `MEMBER_LEFT` arrives (`np_matching2_mm.cpp`).
* Star topology honored via `signaling_main_member`.

## Summon HTTP (game-owned JSON, relayed by shadNet) — CONFIRMED

| Route | Sender | Purpose |
| --- | --- | --- |
| `create` | guest (repeated) | Advertise; response may carry a claim (`SummonDataCreateResponse` with claim members) or a host placement header |
| `get` | host (repeated) | Search; returns `SummonDataList` |
| `request` | host | Claim `{CharaId, TargetUserId, TargetCharaId, SessionId, UserId}` (serializer `0x01E98650`) |
| `delete` | either | Remove advertisement (retained in seamless active states unless `Force`/`SeamlessLeave`) |

SummonData: base64 of 224 bytes (v3). Transform floats at 0x5C–0x68; online name at 0x44; available-result count at 0x79 (shadNet normalizes 0 → 1 in search responses).

Placement header v1: ASCII `1,<packed map hex>,…` (map, 4 IEEE-754 bit patterns, signed SOS area), ≤ 128 bytes, no CR/LF.

Broker states: `Advertised → Preparing (cross-map) → Claimed → Delivered → Consumed`; TTL 130 s, 15 min for seamless active states.

## Room lifecycle on shadNet (`cmd_matching.cpp`) — CONFIRMED

* `LeaveRoom`: remove member; empty room destroyed; owner leaving → succession list or first member; `MEMBER_LEFT (0x1102)` to others.
* TCP disconnect → same path (`CleanupMatchingOnDisconnect`).
* Rooms are in memory; server restart drops all.

## Bloodborne netcode (inside the P2P payload)

| Question | Finding | Status |
| --- | --- | --- |
| Message types / serialization | Not decoded by anyone | UNKNOWN |
| Behavior sync module | `SprjChrBehaviorSyncModule` | STRONGLY INDICATED exists |
| Event sync | `SyncEvent`, `SetNoNetSync`; EMEVD `Set Network Sync State` | STRONGLY INDICATED |
| Game-level timeouts | `OnRoomTimeOut`, `OnP2PTimeOut`, `OnSummonTimeOut` | CONFIRMED names; timeouts UNKNOWN |
| Wander demon messages | `SendFrpgNet_DeadWanderDemon`, `SendFrpgNet_EscapeWanderDemon` | CONFIRMED names |

## Protocol decision for the project

No new gameplay protocol is justified yet: all in-session sync already works natively. The only
missing information today is *coordination* (who follows whom, destination, session intent).
If a control channel is needed it should ride the existing placement-header mechanism (bump to v2)
or a small shadNet route. Revisit after Phase 1 tests.

## Rates (targets for any control traffic we add)

| Class | Rate |
| --- | --- |
| Continuous state | none (native netcode already does this) |
| Coordination events (travel, death, boss clear) | on event, ≤ 1/s |
| Heartbeat | reuse STUN ping 5 s |
| Resync snapshot | on reconnect only |
