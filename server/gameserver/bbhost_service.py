r"""bbhost players on a BBSC server (2026-10-12).

bbhost (droogie's Bloodborne host, github.com/droogie/bbhost) runs the game's own online code and
answers the PS4's NP libraries (Matching2 rooms, Signaling) with HTTP calls to a private server
instead of PSN. With `[online] host = <this server>` and `scheme = "http"` in its config:

  * the game's own API (ss.info, login, messages, summon signs) comes to the game server port
    (20443) as for shadPS4 players - bbhost marks those requests with X-BBHost-Ruleset;
  * the NP calls (/mp/matching2/*, /np/*) and the account calls (/auth/*) go to port 18671;
  * STUN and the relay go to UDP 3478. Every bbhost player registers a relay port there, and
    all P2P traffic between bbhost players passes through this server, so no NAT can block it.

This file is a fresh implementation of that protocol, written from bbhost's client source
(src/net/session.cpp, account.cpp, stun.cpp, src/hle/net.cpp, np_matching2.cpp) and checked with
its own test bench (tools/nettest). The Hunter's Dream's server is not published and none of its
code is here.

bbhost players play with bbhost players: their P2P wire is not shadPS4's, so the summon list
they see is their own (bbhost requests get an ss.info pointing summon_messenger here, everyone
else keeps shadNet's broker). The BBSC relay (20444) works for both.

Accounts have no passwords, as on The Hunter's Dream: creating one hands the PC a token (sent as
X-BB-Token on every request) and a recovery code. They are kept in bbhost-accounts.json beside
the online data, as SHA-256 hashes only. That file is private.
"""
import base64
import hashlib
import json
import os
import re
import secrets
import socket
import struct
import threading
import time
import uuid

# ---- settings (set by configure()) --------------------------------------------------------------
OPTS = {
    "heartbeat_timeout": 30.0,  # a member silent this long has left (bbhost heartbeats every 5 s)
    "require_account": True,    # NP calls without a token are refused (401), as on The Hunter's Dream
    "public_ip": "",            # the address players reach the STUN port at (relay addresses)
    "relay_ports": (24000, 24999),
    "relay_idle": 600.0,        # a relay port nobody refreshed for this long is released
    "sign_ttl": 120.0,          # a summon sign not re-posted for this long is gone
    "accounts_path": "",
}
LOCK = threading.RLock()
EVENTS_CV = threading.Condition(LOCK)
_log = print


def configure(log=print, **options):
    global _log
    _log = log
    OPTS.update({k: v for k, v in options.items() if v is not None})
    ACCOUNTS.load(OPTS["accounts_path"])
    threading.Thread(target=_reaper, daemon=True, name="bbhost-reaper").start()


def say(text):
    _log("      bbhost: " + text, flush=True)


def is_bbhost(headers):
    return headers.get("X-BBHost-Ruleset") is not None or "bbhost/" in (headers.get("User-Agent") or "")


# ---- accounts -------------------------------------------------------------------------------------
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{2,15}$")  # PSN online id rules: 3-16, letter first


def _hash(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class Accounts:
    def __init__(self):
        self.path = ""
        self.data = {}          # lower-case name -> {Name, Tokens: [hash], Recovery: hash, Created}
        self.by_token = {}      # token hash -> Name

    def load(self, path):
        self.path = path
        if path and os.path.exists(path):
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    self.data = json.load(handle)
            except (OSError, ValueError) as e:
                say("could not read {} ({}); starting with no accounts".format(path, e))
                self.data = {}
        self.by_token = {t: a["Name"] for a in self.data.values() for t in a.get("Tokens", [])}
        if path:
            say("accounts: {} ({} account(s))".format(path, len(self.data)))

    def save(self):
        if not self.path:
            return
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as handle:
            json.dump(self.data, handle, indent=1)
        os.replace(tmp, self.path)

    def owner(self, token):
        return self.by_token.get(_hash(token)) if token else None

    def _issue(self, account):
        token = secrets.token_urlsafe(24)
        code = "-".join(secrets.token_hex(2).upper() for _ in range(4))
        account.setdefault("Tokens", []).append(_hash(token))
        account["Tokens"] = account["Tokens"][-8:]   # 8 PCs per account at most
        account["Recovery"] = _hash(code.upper())
        self.by_token = {t: a["Name"] for a in self.data.values() for t in a.get("Tokens", [])}
        self.save()
        return token, code

    def create(self, name):
        name = (name or "").strip()
        if not NAME_RE.match(name):
            return None, "Names are 3-16 letters, digits, - or _, starting with a letter."
        with LOCK:
            if name.lower() in self.data:
                return None, "That name is taken."
            account = {"Name": name, "Created": int(time.time())}
            self.data[name.lower()] = account
            token, code = self._issue(account)
        say("account created: {}".format(name))
        return {"OnlineId": name, "Token": token, "RecoveryCode": code}, None

    def recover(self, name, code):
        with LOCK:
            account = self.data.get((name or "").strip().lower())
            if not account or account.get("Recovery") != _hash((code or "").strip().upper()):
                return None, "Wrong name or recovery code."
            token, new_code = self._issue(account)
        say("account recovered: {}".format(account["Name"]))
        return {"OnlineId": account["Name"], "Token": token, "RecoveryCode": new_code}, None

    def logout(self, token):
        with LOCK:
            h = _hash(token or "")
            for account in self.data.values():
                if h in account.get("Tokens", []):
                    account["Tokens"].remove(h)
            self.by_token.pop(h, None)
            self.save()


ACCOUNTS = Accounts()


def auth_call(path, body):
    """(status, reply) for /auth/*. Every reply carries ResKind; a refusal says why in Message."""
    if path == "/auth/account/create":
        reply, error = ACCOUNTS.create(body.get("Name"))
    elif path == "/auth/recover":
        reply, error = ACCOUNTS.recover(body.get("Name"), body.get("RecoveryCode"))
    elif path == "/auth/logout":
        ACCOUNTS.logout(body.get("Token"))
        reply, error = {}, None
    elif path in ("/auth/device/start", "/auth/device/poll", "/auth/web_code"):
        reply, error = None, "This server has no Discord linking: create an account on this PC instead."
    else:
        return 404, {"ResKind": 1, "Message": "unknown account call"}
    if error:
        return 200, {"ResKind": 1, "Message": error}
    reply["ResKind"] = 0
    return 200, reply


# ---- who is asking ----------------------------------------------------------------------------------
USERS = {}   # game UserId -> online id (from the game's login with a token)


def online_of(headers, body):
    """The caller's online id: its token's account, else (open server) the body's OnlineId."""
    name = ACCOUNTS.owner(headers.get("X-BB-Token"))
    if name:
        return name
    if not OPTS["require_account"]:
        return str(body.get("OnlineId") or "") or None
    return None


def login_account(headers, platform_id):
    """The account key for a bbhost game login: the token's name, so every login of one player
    gets the same UserId and two players never share one."""
    name = ACCOUNTS.owner(headers.get("X-BB-Token"))
    if name:
        return "bbhost:" + name, name
    if not OPTS["require_account"] and platform_id:
        return "bbhost:" + str(platform_id), str(platform_id)
    return None, None


def note_login(user_id, online):
    with LOCK:
        USERS[user_id] = online


# ---- endpoints: what each player told us -------------------------------------------------------------
CONTEXTS = {}   # online id -> {"local": (ip, port), "mapped": (ip, port), "src": http source ip, "seen": t}


def _endpoint(body, src):
    local = (str(body.get("LocalAddr") or body.get("SignalingAddr") or ""),
             int(body.get("LocalPort") or body.get("SignalingPort") or 0))
    mapped = (str(body.get("MappedAddr") or ""), int(body.get("MappedPort") or 0))
    return {"local": local, "mapped": mapped, "src": src}


def _loopback(ip):
    return not ip or ip.startswith("127.") or ip == "0.0.0.0"


def best_address(target, viewer_src):
    """Where a viewer reaches a target: the target's LAN address when both come from the same
    public address and the target named one, else its STUN/relay address, else what we saw."""
    local, mapped = target["local"], target["mapped"]
    if viewer_src and viewer_src == target["src"] and not _loopback(local[0]) and local[1]:
        return local
    if mapped[0] and mapped[1]:
        return mapped
    if local[1]:
        return (target["src"] if _loopback(local[0]) and not _loopback(target["src"]) else local[0],
                local[1])
    return ("", 0)


def member_record(member, viewer_src, room_id=None):
    addr = best_address(member, viewer_src)
    rec = {"MemberId": member["mid"], "OnlineId": member["online"],
           "Addr": addr[0], "Port": addr[1],
           "LocalAddr": member["local"][0], "LocalPort": member["local"][1],
           # the chosen address again: bbhost prefers MappedAddr when it differs from Addr
           "MappedAddr": addr[0], "MappedPort": addr[1]}
    if room_id is not None:
        rec["RoomId"] = room_id
    return rec


# ---- events --------------------------------------------------------------------------------------------
QUEUES = {}       # online id -> [(event id, name, fields)]
_EVENT_ID = [0]


def push(online, name, **fields):
    with EVENTS_CV:
        _EVENT_ID[0] += 1
        QUEUES.setdefault(online, []).append((_EVENT_ID[0], name, fields))
        del QUEUES[online][:-200]
        EVENTS_CV.notify_all()


def poll(online, cursor, wait=2.0):
    end = time.time() + wait
    with EVENTS_CV:
        while True:
            queue = QUEUES.get(online, [])
            queue[:] = [e for e in queue if e[0] > cursor]
            if queue:
                event_id, name, fields = queue[0]
                reply = {"ResKind": 0, "HasEvent": 1, "EventId": event_id, "Name": name,
                         "NextCursor": event_id, "Payload": dict(fields)}
                reply.update(fields)
                reply["Name"] = name
                return reply
            left = end - time.time()
            if left <= 0:
                return {"ResKind": 0, "HasEvent": 0, "NextCursor": max(cursor, 0)}
            EVENTS_CV.wait(left)


# ---- rooms ----------------------------------------------------------------------------------------------
ROOMS = {}        # room id -> room
SESSIONS = {}     # session id -> room id
WHERE = {}        # online id -> (room id, member id)
_ROOM_ID = [int(time.time()) % 100000 * 1000]


def _member_of(session_id, member_id):
    room = ROOMS.get(SESSIONS.get(session_id or ""))
    if not room:
        return None, None
    return room, room["members"].get(int(member_id or 0))


def _remove_member(room, mid, reason, tell_self=None):
    """Takes a member out; the owner leaving closes the room for everyone."""
    member = room["members"].pop(mid, None)
    if not member:
        return
    if WHERE.get(member["online"]) == (room["id"], mid):
        WHERE.pop(member["online"], None)
    if mid == room["owner"]:
        say("room {} closed ({}: host {} left)".format(room["id"], reason, member["online"]))
        for other in list(room["members"].values()):
            push(other["online"], "room_closed", RoomId=room["id"],
                 Reason="host_left" if reason in ("leave_room", "rejoined") else reason)
            WHERE.pop(other["online"], None)
        room["members"].clear()
        ROOMS.pop(room["id"], None)
        SESSIONS.pop(room["sid"], None)
        return
    say("room {}: {} left ({}), {} in the room".format(room["id"], member["online"], reason,
                                                       len(room["members"])))
    for other in room["members"].values():
        push(other["online"], "room_member_left", RoomId=room["id"], MemberId=mid,
             OnlineId=member["online"], Reason=reason)
        if reason in ("timeout",):
            push(other["online"], "peer_deactivated", OnlineId=member["online"])


def _leave_current(online, reason):
    where = WHERE.get(online)
    if where and where[0] in ROOMS:
        _remove_member(ROOMS[where[0]], where[1], reason)


def _new_member(online, mid, body, src):
    member = _endpoint(body, src)
    member.update({"online": online, "mid": mid, "seen": time.time()})
    ctx = CONTEXTS.get(online)
    if ctx and not member["mapped"][1]:
        member["mapped"] = ctx["mapped"]
    return member


def create_room(online, body, src, ruleset):
    with LOCK:
        _leave_current(online, "leave_room")
        _ROOM_ID[0] += 1
        room_id = _ROOM_ID[0]
        max_members = max(2, min(int(body.get("MaxMembers") or 5), 16))
        member = _new_member(online, 1, body, src)
        room = {"id": room_id, "sid": uuid.uuid4().hex, "owner": 1, "max": max_members,
                "members": {1: member}, "ruleset": ruleset, "created": time.time(),
                "host_extra": {k: body.get(k) for k in ("HostArea", "HostLevel", "HostPos", "MemberTag")
                               if body.get(k) is not None}}
        ROOMS[room_id] = room
        SESSIONS[room["sid"]] = room_id
        WHERE[online] = (room_id, 1)
        say("room {} created by {} ({} slots)".format(room_id, online, max_members))
        # an invite asked for before the host had a room goes out now
        for guest, (host, at) in list(PENDING_INVITES.items()):
            if host == online and time.time() - at < 30:
                PENDING_INVITES.pop(guest, None)
                _send_invite(room, guest)
        return {"ResKind": 0, "RoomId": room_id, "SessionId": room["sid"], "MemberId": 1,
                "OwnerMemberId": 1, "MaxMembers": max_members,
                "Members": [member_record(member, src)]}


def join_room(online, body, src):
    with LOCK:
        room = ROOMS.get(int(body.get("RoomId") or 0))
        if not room:
            return {"ResKind": 1, "Error": "no such room"}
        old = WHERE.get(online)
        if old and old[0] == room["id"]:
            # the same player again (its game restarted before its leave arrived)
            _remove_member(room, old[1], "rejoined")
            room = ROOMS.get(room["id"])
            if not room:
                return {"ResKind": 1, "Error": "room closed"}
        elif old:
            _leave_current(online, "leave_room")
        if len(room["members"]) >= room["max"]:
            say("room {}: {} refused, full ({})".format(room["id"], online, room["max"]))
            return {"ResKind": 1, "Error": "room full"}
        mid = next(m for m in range(1, room["max"] + 1) if m not in room["members"])
        member = _new_member(online, mid, body, src)
        room["members"][mid] = member
        WHERE[online] = (room["id"], mid)
        for other in room["members"].values():
            if other is not member:
                push(other["online"], "room_member_joined", **member_record(member, other["src"], room["id"]))
        say("room {}: {} joined as member {} ({} in the room)".format(
            room["id"], online, mid, len(room["members"])))
        return {"ResKind": 0, "RoomId": room["id"], "SessionId": room["sid"], "MemberId": mid,
                "OwnerMemberId": room["owner"], "MaxMembers": room["max"],
                "HostRuleset": room["ruleset"],
                "Members": [member_record(m, src) for m in room["members"].values()]}


def _reaper():
    while True:
        time.sleep(2)
        now = time.time()
        with LOCK:
            for room in list(ROOMS.values()):
                for mid, member in list(room["members"].items()):
                    if now - member["seen"] > OPTS["heartbeat_timeout"]:
                        if room["id"] in ROOMS:
                            _remove_member(room, mid, "timeout")
            for online, ctx in list(CONTEXTS.items()):
                if now - ctx["seen"] > 3600 and online not in WHERE:
                    CONTEXTS.pop(online, None)
                    QUEUES.pop(online, None)
            for key, sign in list(SIGNS.items()):
                if now - sign["at"] > OPTS["sign_ttl"]:
                    SIGNS.pop(key, None)
        RELAY.expire(now)


# ---- summon signs (bbhost players only) --------------------------------------------------------------
SIGNS = {}            # (UserId, CharaId) -> {"body", "online", "ruleset", "at"}
PENDING_INVITES = {}  # guest online -> (host online, time)


def _same_if_present(request, sign, key):
    want = request.get(key)
    return want is None or want == sign.get(key)


def _matches(request, sign):
    if sign.get("UserId") == request.get("UserId"):
        return False
    for key in ("SummonDataVersion", "AreaId", "AreaRegionId", "ChannelId"):
        if not _same_if_present(request, sign, key):
            return False
    types = {f.get("SummonType") for f in request.get("SummonTypeList") or [] if "SummonType" in f}
    if types and sign.get("SummonType") not in types:
        return False
    # The password must be the same on both sides, as in the game (2026-10-19): a search with no
    # password used to match passworded signs too. None/absent = no password.
    word = request.get("SummonWord") or ""
    if word != (sign.get("SummonWord") or ""):
        return False
    level, theirs = request.get("MatchingLevel"), sign.get("MatchingLevel")
    if not word and isinstance(level, int) and isinstance(theirs, int) and level >= 0 and theirs >= 0:
        if abs(level - theirs) > 10 + level // 5:
            return False
    return True


def _for_search(sign):
    """The sign as a search result: Bloodborne posts its available-result count (0x79) as 0 and
    takes one from it when it picks the sign, so a listed sign must say 1."""
    out = dict(sign)
    try:
        data = bytearray(base64.b64decode(out.get("SummonData") or ""))
        if out.get("SummonDataVersion") == 3 and len(data) == 0xE0 and data[0x79] == 0:
            data[0x79] = 1
            out["SummonData"] = base64.b64encode(bytes(data)).decode()
    except (ValueError, TypeError):
        pass
    out.pop("MessageId", None)
    out.pop("ResKind", None)
    return out


def summon_call(path, body, headers):
    ruleset = headers.get("X-BBHost-Ruleset") or "vanilla"
    online = ACCOUNTS.owner(headers.get("X-BB-Token"))
    user_id = body.get("UserId")
    with LOCK:
        if online and user_id is not None:
            USERS[user_id] = online
        if path.endswith("/create"):
            key = (user_id, body.get("CharaId"))
            SIGNS[key] = {"body": body, "online": online or USERS.get(user_id), "ruleset": ruleset,
                          "at": time.time()}
            return {"ResKind": 0, "MessageId": "SummonDataCreateResponse"}
        if path.endswith("/get"):
            found = sorted((s for s in SIGNS.values()
                            if s["ruleset"] == ruleset and _matches(body, s["body"])),
                           key=lambda s: -s["at"])
            limit = int(body.get("GetMaxCount") or 20)
            return {"SummonDataList": [_for_search(s["body"]) for s in found[:limit]],
                    "ResKind": 0, "MessageId": "SummonDataGetListResponse"}
        if path.endswith("/delete"):
            for key in [k for k in SIGNS if k[0] == user_id]:
                SIGNS.pop(key, None)
            return {"ResKind": 0, "MessageId": "SummonDataRemoveResponse"}
        if path.endswith("/request"):
            target = body.get("TargetUserId")
            chara = body.get("TargetCharaId")
            picked = [k for k in SIGNS if k[0] == target and (chara is None or k[1] == chara)]
            host = online or USERS.get(user_id)
            for key in picked:
                guest = SIGNS.pop(key)["online"] or USERS.get(target)
                say("summon: {} picked {}'s sign".format(host or user_id, guest or target))
                if guest and host:
                    where = WHERE.get(host)
                    room = ROOMS.get(where[0]) if where else None
                    if room and room["owner"] == where[1]:
                        _send_invite(room, guest)
                    else:
                        PENDING_INVITES[guest] = (host, time.time())
            return {"ResKind": 0, "MessageId": "SummonDataSummonResponse"}
    return {"ResKind": 0}


def _send_invite(room, guest):
    """guest_invite: the host's addresses for the guest to open its NAT toward. The summon itself
    is the game's own item, which the host's game sends over P2P once it reaches the guest."""
    host = room["members"][room["owner"]]
    viewer = CONTEXTS.get(guest, {}).get("src", "")
    addr = best_address(host, viewer)
    fields = {"RoomId": room["id"], "HostOnlineId": host["online"],
              "HostAddr": addr[0], "HostPort": addr[1],
              "HostLocalAddr": host["local"][0], "HostLocalPort": host["local"][1],
              "HostMappedAddr": addr[0], "HostMappedPort": addr[1], "MemberTag": 1}
    fields.update(room.get("host_extra") or {})
    push(guest, "guest_invite", **fields)


# ---- the NP surfaces ----------------------------------------------------------------------------------
def np_call(path, body, headers, src):
    """(status, reply) for /mp/* and /np/*."""
    online = online_of(headers, body)
    if not online and not OPTS["require_account"] and body.get("SessionId"):
        # open server: room calls carry no name, the session and member number say who it is
        caller = body.get("KickerMemberId") if path.endswith("/kick_member") else body.get("MemberId")
        with LOCK:
            member = _member_of(body.get("SessionId"), caller)[1]
            online = member["online"] if member else None
        if not online:
            return 200, {"ResKind": 0, "InRoom": 0}
    if not online:
        return 401, {"ResKind": 1, "Message": "Sign in first (bbhost: F10, Account)."}
    now = time.time()
    with LOCK:
        if path == "/mp/matching2/context_start":
            CONTEXTS[online] = dict(_endpoint(body, src), seen=now)
            QUEUES.pop(online, None)        # a new run starts its cursor at 0
            ctx = CONTEXTS[online]
            say("{} online from {} (P2P {}:{}, relay/STUN {}:{})".format(
                online, src, ctx["local"][0], ctx["local"][1], *ctx["mapped"]))
            return 200, {"ResKind": 0, "OnlineId": online,
                         "MappedAddr": ctx["mapped"][0], "MappedPort": ctx["mapped"][1]}
        ctx = CONTEXTS.get(online)
        if ctx:
            ctx["seen"] = now
            ctx["src"] = src
        if path == "/mp/matching2/create_room":
            return 200, create_room(online, body, src, headers.get("X-BBHost-Ruleset") or "vanilla")
        if path == "/mp/matching2/join_room":
            return 200, join_room(online, body, src)
        if path in ("/mp/matching2/leave_room", "/mp/matching2/heartbeat",
                    "/mp/matching2/kick_member", "/mp/matching2/signaling_update"):
            caller = body.get("KickerMemberId") if path.endswith("/kick_member") else body.get("MemberId")
            room, member = _member_of(body.get("SessionId"), caller)
            if member and member["online"] != online:
                member = None
            if path == "/mp/matching2/signaling_update":
                mapped = (str(body.get("MappedAddr") or ""), int(body.get("MappedPort") or 0))
                if ctx:
                    ctx["mapped"] = mapped
                if member:
                    member["mapped"] = mapped
                    member["seen"] = now
                    for other in room["members"].values():
                        if other is not member:   # bbhost takes a known member again as an address refresh
                            push(other["online"], "room_member_joined",
                                 **member_record(member, other["src"], room["id"]))
                return 200, {"ResKind": 0}
            if not member:
                if path == "/mp/matching2/heartbeat":
                    return 200, {"ResKind": 0, "InRoom": 0}
                return 200, {"ResKind": 0}
            member["seen"] = now
            if path == "/mp/matching2/heartbeat":
                return 200, {"ResKind": 0, "InRoom": 1}
            if path == "/mp/matching2/leave_room":
                _remove_member(room, member["mid"], "leave_room")
                return 200, {"ResKind": 0}
            # kick_member: only the room's owner
            if member["mid"] != room["owner"]:
                return 200, {"ResKind": 1, "Error": "only the host can kick"}
            kicked_mid = int(body.get("MemberId") or 0)
            target = room["members"].get(kicked_mid) if kicked_mid != member["mid"] else None
            if target:
                push(target["online"], "room_member_kicked", RoomId=room["id"], MemberId=kicked_mid,
                     OptData=str(body.get("OptData") or ""))
                _remove_member(room, kicked_mid, "kicked")
            return 200, {"ResKind": 0}
        if path == "/np/signaling/resolve":
            who = str(body.get("OnlineId") or "")
            target = CONTEXTS.get(who)
            if not target:
                return 200, {"ResKind": 1, "Error": "not registered"}
            addr = best_address(target, src)
            return 200, {"ResKind": 0, "OnlineId": who, "Addr": addr[0], "Port": addr[1]}
        if path == "/mp/matching2/session_blob":
            return 200, {"ResKind": 0, "OnlineId": online}
    if path == "/np/events/poll":
        return 200, poll(online, int(body.get("Cursor") or 0))
    if path == "/np/events/ack":
        cursor = int(body.get("Cursor") or 0)
        with LOCK:
            queue = QUEUES.get(online, [])
            queue[:] = [e for e in queue if e[0] > cursor]
        return 200, {"ResKind": 0}
    return 404, {"ResKind": 1, "Error": "unknown call"}


def handle(path, query, payload, headers, src):
    """Entry from the game server's HTTP handler. None when the path is not ours."""
    if not (path.startswith("/mp/") or path.startswith("/np/") or path.startswith("/auth/")):
        return None
    try:
        body = json.loads(payload) if payload else {}
    except ValueError:
        body = {}
    if path == "/mp/matching2/session_blob" and not body:
        body = {k: v for k, v in (p.split("=", 1) for p in query.split("&") if "=" in p)}
    if path.startswith("/auth/"):
        return auth_call(path, body)
    return np_call(path, body, headers, src)


SHAD_MAGIC = b"BBR1"


# ---- STUN + relay (UDP) -------------------------------------------------------------------------------
# Request: [0x0001][len][16-byte id][attrs], BBHOST-HELLO 0x8100 = "bbr1" + token (zeros: give me
# one). Answer: [0x0101][len][id] MAPPED-ADDRESS and, with a HELLO, BBHOST-RELAY 0x8101 = "bbr1" +
# token + relay port + where the request came from. The mapped address of a relay client is this
# server's public address and its relay port: peers send there, their bbhost frames it to the
# STUN port as [0xfb]['R'][token][port] and it comes out [0xfb]['r'][source port].
class Relay:
    def __init__(self):
        self.sock = None
        # Observability only. No changes to the BBHOST or BBR1 wire protocols.
        self.datagrams_in = 0
        self.bytes_in = 0
        self.datagrams_out = 0
        self.bytes_out = 0
        self.send_errors = 0
        self.unrecognized_in = 0
        self.by_token = {}   # token -> port
        self.ports = {}      # port -> {"token", "addr": (ip, port), "seen"}
        self.frames = 0
        self.shad = {}       # shadPS4 relay: online id (16 bytes) -> {"addr": (ip, port), "seen"}
        self.shad_frames = 0
        self.shad_missing = {}  # (src, dst) -> (last told, last logged) for unregistered dst

    def start(self, bind, port, rcvbuf_bytes=0):
        # Explicit opt-in only. Linux doubles SO_RCVBUF for accounting and caps
        # the requested value using net.core.rmem_max (unless SO_RCVBUFFORCE).
        # Log the observed value; changing sysctl alone does not resize a socket.
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            if rcvbuf_bytes:
                if not 8192 <= rcvbuf_bytes <= 64 * 1024 * 1024:
                    raise ValueError("UDP receive buffer must be 8192..67108864 bytes")
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf_bytes)
            sock.bind((bind, port))
        except Exception:
            sock.close()
            raise
        self.sock = sock
        actual = sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
        if rcvbuf_bytes:
            say("UDP SO_RCVBUF: requested {} bytes; kernel reports {} bytes (Linux accounting includes 2x)".format(
                rcvbuf_bytes, actual))
            if actual < rcvbuf_bytes * 2:
                say("UDP buffer capped by kernel; check net.core.rmem_max before tuning")
        threading.Thread(target=self._loop, daemon=True, name="bbhost-stun").start()
        say("STUN + relay on udp {}:{} (relay ports {}-{}, players reach it at {})".format(
            bind, port, OPTS["relay_ports"][0], OPTS["relay_ports"][1], OPTS["public_ip"] or "?"))

    def _send_packet(self, data, addr):
        """Exactly the original sendto(data, addr), with counters only."""
        try:
            result = self.sock.sendto(data, addr)
        except OSError:
            self.send_errors += 1
            raise
        self.datagrams_out += 1
        self.bytes_out += result
        return result

    def counters(self):
        """Cumulative traffic counters for read-only periodic diagnostics."""
        return {k: getattr(self, k) for k in (
            "datagrams_in", "bytes_in", "datagrams_out", "bytes_out",
            "send_errors", "unrecognized_in", "frames", "shad_frames")}

    def expire(self, now):
        with LOCK:
            for port, slot in list(self.ports.items()):
                if now - slot["seen"] > OPTS["relay_idle"]:
                    self.ports.pop(port, None)
                    self.by_token.pop(slot["token"], None)
            for npid, entry in list(self.shad.items()):
                if now - entry["seen"] > 60:
                    self.shad.pop(npid, None)
            for key, (told, logged) in list(self.shad_missing.items()):
                if now - max(told, logged) > 300:
                    self.shad_missing.pop(key, None)

    def _port_for(self, token, addr):
        slot_port = self.by_token.get(token)
        if slot_port is None:
            lo, hi = OPTS["relay_ports"]
            free = [p for p in range(lo, hi + 1) if p not in self.ports]
            if not free:
                return None
            slot_port = secrets.choice(free)
            token = secrets.token_bytes(8)
            self.by_token[token] = slot_port
            self.ports[slot_port] = {"token": token}
        slot = self.ports[slot_port]
        slot["addr"] = addr
        slot["seen"] = time.time()
        return slot_port

    def _loop(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(65536)
            except OSError:
                # Do not spin at 100% CPU after a socket is closed.
                if self.sock.fileno() < 0:
                    return
                time.sleep(0.01)
                continue
            self.datagrams_in += 1
            self.bytes_in += len(data)
            try:
                if data[:4] == SHAD_MAGIC:
                    self._shad(data, addr)
                elif len(data) >= 12 and data[0] == 0xFB and data[1] == ord("R"):
                    self._forward(data, addr)
                elif len(data) >= 20 and data[0:2] == b"\x00\x01":
                    self._binding(data, addr)
                else:
                    self.unrecognized_in += 1
            except Exception as e:  # one bad datagram must not stop the relay
                say("stun/relay error from {}: {!r}".format(addr, e))

    def _forward(self, data, addr):
        token, dst = data[2:10], struct.unpack_from("!H", data, 10)[0]
        with LOCK:
            src_port = self.by_token.get(token)
            if src_port is None:
                return
            src = self.ports[src_port]
            src["addr"] = addr
            src["seen"] = time.time()
            target = self.ports.get(dst)
            to = target and target.get("addr")
        if to:
            self._send_packet(bytes([0xFB, ord("r")]) + struct.pack("!H", src_port) + data[12:], to)
            self.frames += 1

    # shadPS4 P2P relay (BBSC fork, 2026-10-12). Players whose routers or CGNAT stop direct P2P
    # send the game's P2P frames here, addressed by online id:
    #   "BBR1" 0x01 id[16]                -> register (every 5 s; keeps the NAT mapping open), ack 0x02
    #   "BBR1" 0x03 dst[16] src[16] ep[6] frame -> sent on to dst as "BBR1" 0x04 src[16] ep[6] frame
    #   (ep = the sender's own public endpoint; passed through as it is)
    #   dst not registered -> "BBR1" 0x05 dst[16] back to the sender (at most every 10 s per pair)
    # src must be registered from the address the frame came from (no sending as someone else).
    def _shad(self, data, addr):
        kind = data[4] if len(data) > 4 else 0
        now = time.time()
        if kind == 0x01 and len(data) >= 21:
            npid = data[5:21]
            with LOCK:
                old = self.shad.get(npid)
                self.shad[npid] = {"addr": addr, "seen": now}
            if old is None or old["addr"] != addr:
                say("shadPS4 relay: {} registered from {}:{}".format(
                    npid.rstrip(b"\0").decode("ascii", "replace"), addr[0], addr[1]))
            self._send_packet(SHAD_MAGIC + b"\x02", addr)
        elif kind == 0x03 and len(data) >= 37:
            dst, src = data[5:21], data[21:37]
            with LOCK:
                sender = self.shad.get(src)
                if sender is None or sender["addr"] != addr:
                    return
                sender["seen"] = now
                target = self.shad.get(dst)
                to = target and target["addr"]
                if not to:
                    # dst never registered: its emulator has no relay (older than 2026-10-12)
                    # or it is not running. Tell the sender (0x05 dst, at most every 10 s per
                    # pair) so its log says so; log it here once a minute per pair.
                    key = (src, dst)
                    last = self.shad_missing.get(key, (0, 0))
                    tell, logged = now - last[0] >= 10, now - last[1] >= 60
                    self.shad_missing[key] = (now if tell else last[0], now if logged else last[1])
            if to:
                self._send_packet(SHAD_MAGIC + b"\x04" + src + data[37:], to)
                self.shad_frames += 1
            else:
                if tell:
                    self._send_packet(SHAD_MAGIC + b"\x05" + dst, addr)
                if logged:
                    say("shadPS4 relay: {} -> {} dropped - {} is not on the relay (older emulator?)".format(
                        src.rstrip(b"\0").decode("ascii", "replace"),
                        dst.rstrip(b"\0").decode("ascii", "replace"),
                        dst.rstrip(b"\0").decode("ascii", "replace")))

    def _binding(self, data, addr):
        length = struct.unpack_from("!H", data, 2)[0]
        txid = data[4:20]
        hello = None
        at = 20
        while at + 4 <= min(len(data), 20 + length):
            kind, size = struct.unpack_from("!HH", data, at)
            value = data[at + 4:at + 4 + size]
            if kind == 0x8100 and len(value) >= 12 and value[:4] == b"bbr1":
                hello = value[4:12]
            at += 4 + ((size + 3) & ~3)
        ip, port = addr
        attrs = b""
        if hello is not None and OPTS["public_ip"]:
            with LOCK:
                token = hello if hello in self.by_token else None
                relay_port = self._port_for(token, addr)
                if relay_port is not None:
                    token = self.ports[relay_port]["token"]
            if relay_port is not None:
                mapped = (OPTS["public_ip"], relay_port)
                value = b"bbr1" + token + struct.pack("!H", relay_port) + socket.inet_aton(ip) + \
                    struct.pack("!H", port)
                attrs += struct.pack("!HH", 0x8101, len(value)) + value
            else:
                mapped = (ip, port)
        else:
            mapped = (ip, port)
        value = struct.pack("!BBH", 0, 1, mapped[1]) + socket.inet_aton(mapped[0])
        attrs = struct.pack("!HH", 0x0001, len(value)) + value + attrs
        self._send_packet(struct.pack("!HH", 0x0101, len(attrs)) + txid + attrs, addr)


RELAY = Relay()


def status():
    with LOCK:
        return {"players": len(CONTEXTS), "rooms": len(ROOMS), "signs": len(SIGNS),
                "relay_ports": len(RELAY.ports), "relayed": RELAY.frames,
                "shad_relay_players": len(RELAY.shad), "shad_relayed": RELAY.shad_frames, "accounts": len(ACCOUNTS.data)}
