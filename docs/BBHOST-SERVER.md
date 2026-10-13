# bbhost on a BBSC server - what it would take (research 2026-10-12)

Source read: droogie/bbhost HEAD (src/net/session.*, account.*, stun.*, http.*, src/hle/np_matching2.cpp,
np_signaling.cpp, tools/nettest/*). The Hunter's Dream's server ("the FastAPI private server") is not
published - droogie's only public repo is bbhost.

## What bbhost talks to
One HTTP(S) origin (`[online] host` + `scheme`, account calls to `auth_server`), JSON bodies, an account
token header. Plus UDP: STUN on 3478 (+ bbhost's relay extension) and P2P between players on 9307.

Session service (sceNpMatching2 / sceNpSignaling are answered by these):

| Endpoint | Body -> reply (fields bbhost reads) |
|---|---|
| POST /mp/matching2/context_start | OnlineId, SignalingAddr/Port, MappedAddr/Port |
| POST /mp/matching2/create_room | our_endpoint (OnlineId, LocalAddr/Port, PublicAddr/Port, MappedAddr/Port) + MaxMembers + extra -> RoomId, SessionId, MemberId, OwnerMemberId, Members[] |
| POST /mp/matching2/join_room | our_endpoint + RoomId -> same as create (Members[] = {MemberId, OnlineId, Addr, Port, LocalAddr, LocalPort, MappedAddr, MappedPort}), HostRuleset |
| POST /mp/matching2/leave_room | SessionId, MemberId |
| POST /mp/matching2/heartbeat | SessionId, MemberId -> InRoom (0 = dropped) |
| POST /mp/matching2/kick_member | SessionId, MemberId, KickerMemberId, OptData (base64, <=16 bytes) |
| POST /mp/matching2/signaling_update | OnlineId, MappedAddr/Port, SessionId, MemberId |
| GET  /mp/matching2/session_blob?OnlineId= | - |
| POST /np/signaling/resolve | OnlineId -> ResKind 0, Addr, Port |
| POST /np/events/poll | OnlineId, Cursor, Categories ["matching2"] -> HasEvent, EventId, Name, NextCursor, + event fields |
| POST /np/events/ack | OnlineId, Cursor |

Events (Name): `guest_invite` (RoomId, MemberTag, HostOnlineId, HostAddr/Port, HostLocalAddr/Port,
HostMappedAddr/Port, HostArea, HostLevel, HostPos[3]) - the summon itself: the guest's game gets its
type-1 item from it; `room_member_joined` (member record + RoomId); `room_member_left` (MemberId,
Reason kicked/leave_room/...); `peer_deactivated` (OnlineId); `room_closed` (Reason host_left/...);
`room_member_kicked` (MemberId, OptData).

Accounts: /auth/account/create (name -> token + recovery code), /auth/recover, /auth/device/start +
/auth/device/poll (Discord link), /auth/web_code, /auth/logout. `require_account = false` skips them.

STUN: RFC 3489/5389 Binding on 3478 from the P2P port; BBHOST-HELLO (0x8100 "bbr1"+token) asks for a
relay port, BBHOST-RELAY (0x8101) answers; relayed datagrams go to the STUN port framed
[0xfb]['R'][token][port u16], come back [0xfb]['r'][source port u16] (hle/net.cpp).

## Where it meets BBSC's server
- The game API (ss.info, messages, summon_messenger) is the same HTTP API our bbsc_gameserver.py serves.
- `guest_invite` is the join: when a host's summon_messenger/request picks a bbhost guest's sign, the
  server must push guest_invite (with the host's room and addresses) into that guest's event queue.
  Our summons live in shadNet's C++ broker (31315), which knows nothing of bbhost's queue - so bbhost
  players would get their own summon broker in Python (ss.info for requests carrying X-BBHost-Ruleset
  pointing summon_messenger at it).
- No cross-play with shadPS4/BBport players: different P2P wire (bbhost UDP 9307 vs shadp2p frames).

## Blockers
- bbhost only accepts the US 1.09 eboot (sha256 941f887a...); our EU copy runs only with
  BBHOST_ANY_EBOOT=1 and none of bbhost's patches - online untested that way. Needs a US tester or
  EU support from droogie.
- BBSC itself is EU-tested; US code layout unknown (bbhost's patch sites match EU at the same offsets).
- The Hunter's Dream rules / droogie's view on BBSC.

## Size
Python service in bbsc_gameserver.py: rooms + events + signaling + simple accounts ~700 lines; bbhost
summon broker ~300; STUN + relay ~200. Several sessions incl. tests (tools/nettest/simclient.py can
drive it without the game).

## bbbridge (MaskZeus, GPL-3.0, read 2026-10-12)
github.com/MaskZeus/bbbridge: a local shadNet server for bbport that speaks The Hunter's Dream's REST
API behind it (the reverse of what a bbhost-on-our-server would need). Useful as a reference:
- P2P framing (src/p2p.hpp): shadp2p/bbport = [src vport u16 be][dst vport u16 be][payload], vport
  0xffff = "SHAD" signaling (0x05/0x06 echo, 0x07 handshake, 0x10 control, 0x21 matching2); bbhost =
  [0xff][flags][src][dst][payload] (0xC3 one-byte vports 40/30, 0x83 two-byte, +4 with 0x20), probe
  "\xfebbhp\0\0\0", relay [0xfb]['R'|'r']... It answers shadp2p's signaling handshakes on the bbhost
  peer's behalf (bbhost has none; its connection state comes from the server).
- Summons: the game's /summon_messenger/* go to THD's community server (18671) with X-BB-Token and
  X-BBHost-Ruleset headers - the server maps signs to accounts and pushes guest_invite.
- CONFIRMED from our source: our fork's p2p_sockets.cpp uses the same 4-byte vport header and 0xffff
  signaling vport as bbport -> BBport and shadPS4-BBSC players share one P2P format (no bridge).
Not needed for BBSC itself. For bbhost players on BBSC servers we would build either a server-side
bbhost session service (above) or a "reverse bbbridge" on the bbhost player's PC (bbhost -> shadNet),
the latter with this code as the template (GPL-3.0).

## Built 2026-10-12: bbhost players on a BBSC server
`tools/gameserver/bbhost_service.py` (own implementation of bbhost's client protocol), loaded by
`bbsc_gameserver.py` (on by default, `--no-bbhost` off):
- Ports: game API as before (20443, bbhost requests marked by `X-BBHost-Ruleset`), sessions and
  accounts on TCP 18671 (`--bbhost-port`, bbhost's default np port), STUN + relay on UDP 3478.
  Ports in use -> logged, the rest of the server keeps running.
- Accounts: `/auth/account/create`, `/auth/recover`, `/auth/logout` (no Discord linking); tokens and
  recovery codes stored as SHA-256 in `bbhost-accounts.json` beside `--data` (private). bbhost game
  requests carry only `X-BB-Token` (no player name), so accounts are required (`--bbhost-open` =
  test mode, the body's OnlineId names the player). Game login with a token -> UserId from
  `identity_for("bbhost:<name>")`; without -> 401.
- Rooms/events/resolve as the table above; members get per-viewer addresses (LAN address only when
  both share a public IP and the target set `online.p2p_addr`; else the relay address).
- Relay: a HELLO gets a token + relay port (24000-24999, virtual); MAPPED-ADDRESS answered as
  public IP:relay port, so all bbhost P2P goes through the server (no NAT issues). Public IP =
  `--bbhost-public-ip`, else `--advertise` resolved (re-resolved every 5 min for DuckDNS).
- Summons: bbhost requests get an ss.info whose summon_messenger points at this server; own sign
  list per ruleset, vanilla matching (area, type, level window), result count byte 0x79 set to 1
  like shadNet. `request` pushes `guest_invite` (host's room + relay address) to the guest. The
  summon itself is the host game's type-1 item over P2P (bbhost's own delivery is off by default,
  `src/engine/summon_invite.cpp`).
- Tested: bbhost's own bench (`tools/bbhost-nettest`, simulated clients) - coop2/3, five (2 co-op +
  2 invaders, 6th refused), leave+rejoin, churn, guest crash timeout, host leaves, kick, lost leave,
  8 NAT pairs: all PASS, open and with accounts, script and frozen exe (E263B6E3).

## Player side: `share/BBSC-0.0.1-bbhost-test(.zip)` (NOT in the 0.0.1 release)
`PLAY BBHOST + BBSC.cmd` -> `tools/play-bbhost.ps1`: asks for bbhost.exe once; `BBHOST_CONFIG_DIR`
= `bbhost-settings\` in the package (bbhost "imports" a --config into the per-user folder, so the
player's own %APPDATA%\bbhost stays untouched; bbhost also keeps a separate account per --config
name: `bbhost-options-bbsc-server.toml`); writes `bbsc-server.toml` ([online] host, scheme http,
require_account, auth_server/np_server :18671, stun_server :3478) and passes it with --config
(layer 3, wins); env `BBSC_EMULATOR`, `BBSC_RELAY=<server>:20444` (else the DLL used shadPS4's
host_overrides), `BBSC_MOD_SEAMLESS=0` (vanilla sign list), `SHADPS4_BBSC_FIVE_HUNTERS=0` (bbhost
has its own five_players on US). DLL 8015B615 (writable-code fix).
Smoke test (EU eboot, BBHOST_ANY_EBOOT=1, local server): bbhost read bbsc-server.toml, BBSC loaded
(52 sites, 159/159 hooks), relay to the bbhost server. Account creation in bbhost's UI and online
play NOT tested.

## EU copies
bbhost hashes the whole eboot file; every engine patch (incl. the NpMatching2 signaling gate,
crash fixes like the command-arena wait) is off for anything but US 1.09 (`hle_runtime.cpp`
hle_patch_guest). The EU code layout matches US at -0x400000 (24 patch sites + the gate byte:
EU 0x0546CC09, `cmp byte [..],1` in 3 callbacks, set 1 in 0x00CBE940, 0 in 0x00CBECE0 - CONFIRMED
statically). Ask droogie to accept the EU 1.09 sha256 (cec1b276... for the decrypted ELF) as the
same build. Update 2026-10-12: droogie (bbhost's author) says the region does not matter, and a tester ran it on EU.
The package sets BBHOST_ANY_EBOOT=1 so bbhost starts EU copies. Online co-op on EU not yet seen in a log.
