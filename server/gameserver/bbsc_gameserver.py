r"""BBSC local game server - the piece that makes Bloodborne think it is online.

Before the game enables anything online it fetches /bb-eu/ss.info, which is base64-encoded XML
holding a <gameurl2> block: one URL per game API, plus the timing and matching parameters the
online systems run on. Everything the game does afterwards goes to whatever that file says.

So this serves it, and points it where we want:
  * the four api_SummonData* endpoints  -> the real shadnet server (co-op actually happens there)
  * everything else (messages, ghosts,  -> this process, which answers with empty-but-valid JSON
    notices, play logs, channels)          so the game stops asking

Each player runs their own copy on 127.0.0.1. Only the summon endpoints are shared, which is why
the shadnet address is the one thing you have to set.

    python bbsc_gameserver.py --shadnet 26.1.2.3

Then in %APPDATA%\shadPS4\host_overrides.json:

    { "https://ss4.scej-network.jp:20443": "http://127.0.0.1:20443" }

The ss.info format, the endpoint list and the JSON response shapes come from NoDreamForHunter by
fhody125 (MIT, see LICENSE.NoDreamForHunter). This is a standalone reimplementation: no Flask, so
it runs on a stock Python, and it can retune the summon parameters on the way out.
"""
import argparse
import base64
import collections
import datetime
import hashlib
import json
import os

import re
import sys
import threading
import time
import traceback
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import bbhost_service

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "ss_info_template.xml")

# Vanilla values, straight out of the retail ss.info. The defaults below keep them, because
# changing matching behaviour is a separate decision from getting online at all.
#   SummonDataGetListInterval2            35   how often a guest polls for summon signs
#   SummonDataCreateInterval2             30   how often a host re-posts its sign
#   SummonDataCoopMatchingLevelUpperAbs2  20   co-op level window, upper
#   SummonDataCoopMatchingLevelLowerAbs2 -20   co-op level window, lower
ARGS = None
SS_INFO_B64 = b""        # for the other player: everything at the advertised (VPN) address
SS_INFO_LOCAL_B64 = b""  # for the game on this PC: the non-summon endpoints at 127.0.0.1
# bbhost players (bbhost_service.py): the same, but summon_messenger here, not shadNet's broker
SS_INFO_BBHOST_B64 = b""
SS_INFO_BBHOST_LOCAL_B64 = b""

# Addresses that mean "the game is running on this same PC".
LOCAL_ADDRESSES = {"127.0.0.1", "::1", "::ffff:127.0.0.1"}

def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]

def set_tag(xml, tag, value):
    """Replace <tag>...</tag>, reporting whether it was actually there."""
    pattern = r"<{0}>[^<]*</{0}>".format(re.escape(tag))
    replacement = "<{0}>{1}</{0}>".format(tag, value)
    out, count = re.subn(pattern, replacement, xml)
    if count == 0:
        print("  WARNING: <{}> not found in the template".format(tag))
    return out

def build_ss_info(local=False, quiet=False, bbhost=False):
    """The ss.info to serve. local=True is the copy for a game running on this PC.

    The game re-reads ss.info every ReloadServerStatusInfoInterval2 (300 s) and checks in with
    login / get_datetime on the addresses it names. If one check-in fails it returns to the title
    with "Failed to connect to Bloodborne server". Serving the host's own game its check-ins on
    127.0.0.1 means the host is never kicked because the VPN adapter dropped or was switched off.
    The summon endpoints keep the shadnet address either way: co-op needs the VPN regardless.
    """
    global print
    real_print = print
    if quiet:
        print = lambda *a, **k: None
    try:
        return _build_ss_info(local, bbhost)
    finally:
        print = real_print


def _build_ss_info(local, bbhost=False):
    with open(TEMPLATE, "r", encoding="utf-8") as handle:
        xml = handle.read().strip()

    # 1. the summon endpoints - the only ones that must reach the shared server. bbhost players
    #    get this server's own summon list (bbhost_service.py): their P2P is not shadPS4's.
    before = xml.count("{YOUR SHADNET SERVER}")
    if bbhost:
        xml = xml.replace("http://{YOUR SHADNET SERVER}:31315", "http://127.0.0.1:20443")
        print("  summon endpoints -> this server (bbhost players)")
    else:
        xml = xml.replace("{YOUR SHADNET SERVER}", ARGS.shadnet)
        print("  summon endpoints -> {}:31315  ({} replaced)".format(ARGS.shadnet, before))

    # 2. everything else -> this process. The template ships pointing at 127.0.0.1:20443; if we
    #    are listening somewhere else, move them with it.
    here = "http://127.0.0.1:{}".format(ARGS.port) if local else \
        "http://{}:{}".format(ARGS.advertise or ARGS.host, ARGS.port)
    if here != "http://127.0.0.1:20443":
        xml = xml.replace("http://127.0.0.1:20443", here)
    print("  other endpoints  -> {}".format(here))

    # 3. optional retuning
    if ARGS.summon_interval:
        xml = set_tag(xml, "SummonDataGetListInterval2", ARGS.summon_interval)
        xml = set_tag(xml, "SummonDataCreateInterval2", ARGS.summon_interval)
        print("  summon poll      -> every {}s (vanilla 35 / 30)".format(ARGS.summon_interval))
    if ARGS.level_range:
        span = abs(int(ARGS.level_range))
        for tag in ("SummonDataCoopMatchingLevelUpperAbs2",
                    "SummonDataCoopMatchingLevelUpperRel2"):
            xml = set_tag(xml, tag, span)
        for tag in ("SummonDataCoopMatchingLevelLowerAbs2",
                    "SummonDataCoopMatchingLevelLowerRel2"):
            xml = set_tag(xml, tag, -span)
        print("  co-op level range-> +/-{} (vanilla +/-20)".format(span))

    # 4. play logs. The retail file leaves 17 of these on, and the game then uploads to
    #    bb-playlog-test.s3.amazonaws.com with no connect timeout set - an outside server we do
    #    not control, on the game's main thread. Nothing reads those logs any more.
    if ARGS.no_playlog:
        xml, count = re.subn(r"<(Playlog[A-Za-z_]*2)>1</\1>", r"<\1>0</\1>", xml)
        print("  play logs        -> {} disabled (no more S3 uploads)".format(count))

    if "{YOUR" in xml:
        print("  WARNING: an unfilled placeholder is still in the XML")
    return base64.b64encode(xml.encode("utf-8"))

def ok(message_id, **extra):
    body = {"ResKind": 0, "MessageId": message_id}
    body.update(extra)
    return body

# One identity per account, reused for every login.
#
# This used to hand out random.randint() on each call. The game logs in more than once per run -
# 16 times in a 2026-09-21 session - and every login changed its UserId underneath it (255, 2029,
# 2782, 5447, 5840 ...). Requests then carried an id the game no longer had, and it gave up with
# "Failed to connect to Bloodborne server. Returning to title menu." It happened in solo play too,
# which is what ruled out anything co-op related.
_IDENTITIES = {}
_IDENTITY_LOCK = threading.Lock()

def identity_for(account):
    """Stable (user_id, session_id) for an account, created once and kept."""
    with _IDENTITY_LOCK:
        found = _IDENTITIES.get(account)
        if found is None:
            # Derived from the account so it also survives a restart of this server mid-session.
            digest = hashlib.sha256(account.encode("utf-8")).digest()
            user_id = 1000 + (int.from_bytes(digest[:4], "big") % 9000)
            found = (user_id, str(uuid.uuid4()))
            _IDENTITIES[account] = found
            print("    identity: {} -> user_id {}".format(account, user_id), flush=True)
        return found

def login(req, account=None):
    try:
        platform_id = json.loads(req or "{}").get("PlatformAccountId")
    except ValueError:
        platform_id = None
    account = account or str(platform_id or "bbsc-default-player")
    user_id, session_id = identity_for(account)
    return {
        "ServerVersion": 1,
        "UserStatus": 0,
        "SessionId": session_id,
        "PlatformAccountId": str(platform_id or account),
        "LanguageId": 19.0,
        "UserId": user_id,
        "MessageId": "LoginResponse",
        "IssuerId": 100.0,
        "ResKind": 0,
    }

# Every list field a reply can carry, as the game names them (UTF-16 key strings in the eboot, in the
# block after "ResKind" / "WarningMessage"). A reply without the list the game reads is treated as a
# failed call: the 2026-09-23 "Failed to connect to Bloodborne server. Returning to title menu." came
# straight after /wandering_ghost/get got a bare {"ResKind": 0} - the game read WanderingGhostList,
# found nothing, and quit to the title. The game asks for ghosts every WanderingGhostGetInterval2
# (120 s), which is why it looked like an AFK kick: standing still is when that timer runs out.
EMPTY_LISTS = {
    "PublishCharacterIdList": [],
    "BloodMessList": [],
    "BloodMessEvaluationList": [],
    "LostBloodMessIdList": [],
    "ChairMessList": [],
    "WanderingGhostList": [],
    "ChannelList": [],
    "ChannelInfoList": [],
    "LostChannelIdList": [],
    "TombMessList": [],
    "NoticeList": [],
}

_NEXT_ID = [int(time.time()) % 1000000 * 10]
_ID_LOCK = threading.Lock()


def new_id():
    """Ids for things the game creates (messages, bloodstains). Nothing is stored yet."""
    with _ID_LOCK:
        _NEXT_ID[0] += 1
        return _NEXT_ID[0]


def empty(message_id, **fields):
    """A well-formed 'nothing here' reply: ResKind 0, the message id, and every list empty."""
    body = {"ResKind": 0, "MessageId": message_id}
    body.update({key: [] for key in EMPTY_LISTS})
    body.update(fields)
    return body


# ---- Messages and bloodstains, shared by everyone on this server (2026-09-29) ------------------
# Report: "messages are only seeable to the guy who wrote it, bloodstains are not there". Both
# were stubs: create answered with an id and stored nothing, message_area always came back empty,
# so each game only ever showed what it had written itself.
#
# Field names are the game's own. The reply parser 0x01E7F240 reads, per BloodMessList entry,
# BloodMessId, PrevBloodMessId, BloodDataVersion, BloodData, MessShellInfo, MessShellInfoVersion
# and per BloodMessEvaluationList entry BloodMessId, EvaluatePlus, EvaluateMinus; per TombMessList
# entry TombMessId, TombData, MessShellInfo, DeathVisionDataVersion; DeathVisionData for a death
# vision. A key it does not find reads as 0 / empty, so extra keys are harmless. The position of a
# message or bloodstain is inside its BloodData / TombData blob, so the server only has to group
# them by the (AreaId, AreaRegionId, ChannelId) the game asks for.
MESSAGES_PER_REGION = 100   # newest kept per region
TOMBS_PER_REGION = 40
TOMBS_PER_USER_REGION = 3   # a player's older bloodstains in the same region drop out
TOMB_MAX_AGE = 7 * 24 * 3600
# Reply size cap (2026-10-18). Reports (valriv55, Gascoigne / Cathedral Ward, host and guest, solo
# too): "[Dantelion2 Panic] (null)(265) Transaction failed" every time right after a
# tomb_messenger/message_area reply of ~155 KB; replies of 97 KB / 106 KB never crashed.
# Lists are cut, newest first, so the whole reply stays under this many bytes of JSON.
# --tomb-max-response-bytes (SeamlessHunt's flag, 2026-10-17) sets it for messages and bloodstains;
# 0 there = no cap. Without the flag the 64 KB default applies.
AREA_REPLY_BUDGET = 64 * 1024

def _within_budget(entries, budget=None):
    budget = AREA_REPLY_BUDGET if budget is None else budget
    if not budget:
        return list(entries)
    kept, size = [], 512   # room for the reply's other fields
    for entry in entries:
        size += len(json.dumps(entry, separators=(",", ":"))) + 1
        if size > budget:
            break
        kept.append(entry)
    return kept

def _as_int(value, default=0):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default

def _region(entry):
    return (_as_int(entry.get("AreaId")), _as_int(entry.get("AreaRegionId")),
            _as_int(entry.get("ChannelId")))


class OnlineStore:
    """Messages, ratings, bloodstains and each player's messenger look, kept in one JSON file."""

    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.data = {"next_id": 0, "messages": {}, "tombs": {}, "deleted": [], "shells": {},
                     "channels": {}}
        if path and os.path.exists(path):
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    self.data.update(json.load(handle))
            except Exception as error:
                print("  WARNING: could not read {} ({}), starting empty".format(path, error))
        self.data.setdefault("channels", {})  # files written before chalice glyphs were stored
        # ids carry on from the last run so an old id is never handed out again
        self.data["next_id"] = max(self.data.get("next_id", 0), new_id())

    def save(self):
        if not self.path:
            return
        temp = self.path + ".tmp"
        with open(temp, "w", encoding="utf-8") as handle:
            json.dump(self.data, handle)
        os.replace(temp, self.path)

    def next_id(self):
        self.data["next_id"] += 1
        return self.data["next_id"]

    def shell(self, user_id):
        found = self.data["shells"].get(str(user_id))
        return found if found else {"ShellData": "", "ShellDataVersion": 0}


STORE = None  # OnlineStore, set up in main()

def _request(req):
    try:
        body = json.loads(req or "{}")
        return body if isinstance(body, dict) else {}
    except ValueError:
        return {}

def _ids(body, list_key, one_key):
    ids = [_as_int(x) for x in body.get(list_key) or [] if not isinstance(x, dict)]
    ids += [_as_int(x.get(one_key)) for x in body.get(list_key) or [] if isinstance(x, dict)]
    if one_key in body:
        ids.append(_as_int(body[one_key]))
    return [i for i in ids if i]

def _message_entry(msg):
    shell = STORE.shell(msg["UserId"])
    # PrevBloodMessId is the writer's own "message this one replaces". A guest's create carried
    # 9287298313769999166 (2026-09-29), past the signed 64-bit range, and the host got the message
    # once and never showed it. Other players have no use for it: 0.
    return {"BloodMessId": msg["BloodMessId"], "PrevBloodMessId": 0,
            "AreaId": msg["AreaId"], "AreaRegionId": msg["AreaRegionId"],
            "ChannelId": msg["ChannelId"], "BloodData": msg["BloodData"],
            "BloodDataVersion": msg.get("BloodDataVersion", 0),
            "CharaData": msg.get("CharaData", True),
            "CharaDataVersion": msg.get("CharaDataVersion", 1),
            "EvaluatePlus": msg.get("EvaluatePlus", 0), "EvaluateMinus": msg.get("EvaluateMinus", 0),
            "MessShellInfo": shell["ShellData"], "MessShellInfoVersion": shell["ShellDataVersion"]}

def _evaluation(msg):
    return {"BloodMessId": msg["BloodMessId"], "EvaluatePlus": msg.get("EvaluatePlus", 0),
            "EvaluateMinus": msg.get("EvaluateMinus", 0)}

def blood_create(req):
    body = _request(req)
    user = _as_int(body.get("UserId"))
    created = []
    with STORE.lock:
        for entry in body.get("BloodMessList") or []:
            if not isinstance(entry, dict) or not entry.get("BloodData"):
                continue
            msg_id = STORE.next_id()
            area, region, channel = _region(entry)
            msg = {"BloodMessId": msg_id, "UserId": user, "Created": time.time(),
                   "AreaId": area, "AreaRegionId": region, "ChannelId": channel,
                   "BloodData": entry["BloodData"],
                   "BloodDataVersion": _as_int(entry.get("BloodDataVersion")),
                   "CharaData": entry.get("CharaData", True),
                   "CharaDataVersion": _as_int(entry.get("CharaDataVersion"), 1),
                   "PrevBloodMessId": _as_int(entry.get("PrevBloodMessId")),
                   "EvaluatePlus": 0, "EvaluateMinus": 0}
            STORE.data["messages"][str(msg_id)] = msg
            created.append(msg_id)
            # keep the newest MESSAGES_PER_REGION of this region
            same = sorted((m for m in STORE.data["messages"].values()
                           if _region(m) == (area, region, channel)),
                          key=lambda m: m["BloodMessId"])
            for old in same[:-MESSAGES_PER_REGION]:
                del STORE.data["messages"][str(old["BloodMessId"])]
        STORE.save()
    print("      messages: user {} wrote {}".format(user, created), flush=True)
    return empty("BloodMessCreateResponse", BloodMessId=created[0] if created else 0,
                 BloodMessIdList=created)

def blood_area(req):
    body = _request(req)
    user = _as_int(body.get("UserId"))
    found = []
    with STORE.lock:
        messages = list(STORE.data["messages"].values())
        seen = set()
        for area in body.get("AreaInfoList") or []:
            key = _region(area)
            if key in seen:
                continue
            seen.add(key)
            limit = _as_int(area.get("GetCount"), 30) or 30
            here = [m for m in messages if _region(m) == key and m["UserId"] != user]
            here.sort(key=lambda m: m["BloodMessId"], reverse=True)
            found += [_message_entry(m) for m in here[:limit]]
    total = len(found)
    found = _within_budget(found)
    if len(found) < total:
        print("      messages: reply cut to {} of {} (size cap)".format(len(found), total), flush=True)
    areas = ["{}/{}".format(*_region(a)[:2]) for a in body.get("AreaInfoList") or []]
    stored = sorted({"{}/{}".format(m["AreaId"], m["AreaRegionId"]) for m in messages})
    print("      messages: {} for user {} (asked {}; stored in {})".format(
        len(found), user, ",".join(areas) or "-", ",".join(stored) or "-"), flush=True)
    return empty("BloodMessGetListResponse", BloodMessList=found)

def blood_exist(req):
    """The game checks the messages it knows about: ratings for those that exist, 'lost' for
    those deleted here. Ids this server never saw (written before it stored anything) are left
    alone rather than reported lost, so nobody's own messages vanish."""
    body = _request(req)
    with STORE.lock:
        ids = _ids(body, "BloodMessIdList", "BloodMessId")
        messages = STORE.data["messages"]
        deleted = set(STORE.data["deleted"])
        evaluations = [_evaluation(messages[str(i)]) for i in ids if str(i) in messages]
        lost = [i for i in ids if i in deleted]
    return ok("BloodMessSearchAddResponse", BloodMessEvaluationList=evaluations,
              LostBloodMessIdList=lost)

def blood_get_evaluation(req):
    body = _request(req)
    with STORE.lock:
        messages = STORE.data["messages"]
        evaluations = [_evaluation(messages[str(i)])
                       for i in _ids(body, "BloodMessIdList", "BloodMessId")
                       if str(i) in messages]
    return empty("BloodMessGetEvaluateResponse", BloodMessEvaluationList=evaluations)

def blood_evaluate(req):
    body = _request(req)
    msg_id = _as_int(body.get("BloodMessId"))
    kind = _as_int(body.get("EvaluateKind"))
    with STORE.lock:
        msg = STORE.data["messages"].get(str(msg_id))
        if msg:
            # EvaluateKind: the game's own values are not confirmed yet - the request sample in the
            # log shows them. 0 is taken as "good", anything else as "poor".
            msg["EvaluatePlus" if kind == 0 else "EvaluateMinus"] += 1
            STORE.save()
    print("      messages: {} rated (kind {}){}".format(
        msg_id, kind, "" if msg else " - unknown id, request: " + req[:300]), flush=True)
    return empty("BloodMessEvaluateResponse", BloodMessId=msg_id)

def blood_delete(req):
    body = _request(req)
    ids = _ids(body, "BloodMessIdList", "BloodMessId")
    user = _as_int(body.get("UserId"))
    with STORE.lock:
        for msg_id in ids:
            msg = STORE.data["messages"].get(str(msg_id))
            if msg and (not user or msg["UserId"] == user):
                del STORE.data["messages"][str(msg_id)]
                STORE.data["deleted"] = (STORE.data["deleted"] + [msg_id])[-1000:]
        STORE.save()
    print("      messages: user {} removed {}".format(user, ids), flush=True)
    return empty("BloodMessRemoveResponse")

def shell_upload(req):
    body = _request(req)
    user = _as_int(body.get("UserId"))
    if user and body.get("ShellData"):
        with STORE.lock:
            shell = {"ShellData": body["ShellData"],
                     "ShellDataVersion": _as_int(body.get("ShellDataVersion"))}
            if STORE.data["shells"].get(str(user)) != shell:
                STORE.data["shells"][str(user)] = shell
                STORE.save()
    return empty("MessengerShellUploadResponse")

def _tomb_entry(tomb):
    shell = STORE.shell(tomb["UserId"])
    entry = {key: value for key, value in tomb.items()
             if key not in ("UserId", "Created", "DeathVisionData", "CharaId", "SessionId",
                            "MessageId")}
    entry.update({"MessShellInfo": shell["ShellData"],
                  "MessShellInfoVersion": shell["ShellDataVersion"]})
    return entry

def tomb_create(req):
    body = _request(req)
    user = _as_int(body.get("UserId"))
    area, region, channel = _region(body)
    with STORE.lock:
        tomb_id = STORE.next_id()
        tomb = {key: value for key, value in body.items()
                if key not in ("CharaId", "SessionId", "MessageId", "UserId")}
        tomb.update({"TombMessId": tomb_id, "UserId": user, "Created": time.time(),
                     "AreaId": area, "AreaRegionId": region, "ChannelId": channel})
        tombs = STORE.data["tombs"]
        tombs[str(tomb_id)] = tomb
        now = time.time()
        same = sorted((t for t in tombs.values() if _region(t) == (area, region, channel)),
                      key=lambda t: t["TombMessId"])
        mine = [t for t in same if t["UserId"] == user]
        drop = mine[:-TOMBS_PER_USER_REGION] + same[:-TOMBS_PER_REGION] + \
            [t for t in tombs.values() if now - t.get("Created", now) > TOMB_MAX_AGE]
        for old in drop:
            tombs.pop(str(old["TombMessId"]), None)
        STORE.save()
    print("      bloodstains: user {} died in {}/{} -> {} (fields: {})".format(
        user, area, region, tomb_id, ",".join(sorted(k for k in body
                                                      if k not in ("SessionId",)))), flush=True)
    return empty("TombMessCreateResponse", TombMessId=tomb_id)

def tomb_area(req):
    body = _request(req)
    user = _as_int(body.get("UserId"))
    found = []
    with STORE.lock:
        tombs = list(STORE.data["tombs"].values())
        seen = set()
        for area in body.get("AreaInfoList") or []:
            key = _region(area)
            if key in seen:
                continue
            seen.add(key)
            limit = _as_int(area.get("GetCount"), 30) or 30
            here = [t for t in tombs if _region(t) == key and t["UserId"] != user]
            here.sort(key=lambda t: t["TombMessId"], reverse=True)
            found += [_tomb_entry(t) for t in here[:limit]]
    total = len(found)
    found = _within_budget(found)
    if len(found) < total:
        print("      bloodstains: reply cut to {} of {} (size cap)".format(len(found), total),
              flush=True)
    areas = ["{}/{}".format(*_region(a)[:2]) for a in body.get("AreaInfoList") or []]
    print("      bloodstains: {} for user {} (asked {}; {} stored)".format(
        len(found), user, ",".join(areas) or "-", len(tombs)), flush=True)
    return empty("TombMessGetListResponse", TombMessList=found)

def tomb_death_vision(req):
    body = _request(req)
    ids = _ids(body, "TombMessIdList", "TombMessId")
    with STORE.lock:
        tomb = next((STORE.data["tombs"].get(str(i)) for i in ids
                     if str(i) in STORE.data["tombs"]), None)
    if not tomb:
        print("      bloodstains: death vision for {} - not found (request keys: {})".format(
            ids, ",".join(sorted(body))), flush=True)
        return empty("DeathVisionGetResponse")
    return empty("DeathVisionGetResponse", TombMessId=tomb["TombMessId"],
                 DeathVisionData=tomb.get("DeathVisionData", ""),
                 DeathVisionDataVersion=tomb.get("DeathVisionDataVersion", 0))

def tomb_delete(req):
    body = _request(req)
    ids = _ids(body, "TombMessIdList", "TombMessId")
    user = _as_int(body.get("UserId"))
    with STORE.lock:
        for tomb_id in ids:
            tomb = STORE.data["tombs"].get(str(tomb_id))
            if tomb and (not user or tomb["UserId"] == user):
                del STORE.data["tombs"][str(tomb_id)]
        STORE.save()
    return empty("TombMessRemoveResponse")


# ---- Chalice dungeons (channels), shared (2026-10-06) ------------------------------------------
# A "channel" is one generated chalice dungeon; its DiscernmentWord is the glyph players type in.
# These were stubs: /channel/upload answered without a ChannelId, so no dungeon ever had one and a
# glyph search found nothing. Entry fields come from the game's channel entry reader 0x01E89DA0
# (FormDataVersion, FormData, ChannelId, ShareLevel, Status, DiscernmentWord, RitualLevel,
# HolyGrailTypeId, FixedOrGeneral, SubFeatureFlag, CreateUserId, CreateCharaId, CreateDate,
# LastPlayDate, TurnoutLevel, WishMaterialList); the reply parser 0x01E7F240 reads ChannelId and
# DiscernmentWord at the top level of an upload / share reply. Search answers only the fixed
# dungeons (FIXED_CHALICES below); any other search stays empty on purpose: answering it could swap
# a player's freshly made dungeon for someone else's. Unknown ids are never reported in
# LostChannelIdList (dungeons made before this server stored them must not be dropped from anyone's
# save).
GLYPH_LETTERS = "abcdefghijkmnpqrstuvwxyz23456789"
CHANNEL_SKIP = ("SessionId", "MessageId", "UserId", "CharaId")
# keys the server owns: an upload never overwrites them
CHANNEL_SERVER_KEYS = ("ChannelId", "DiscernmentWord", "CreateUserId", "CreateCharaId",
                       "CreateDate", "LastPlayDate")

# Fixed chalice dungeons (2026-10-19). The altar ritual of a FIXED chalice (Pthumeru, Hintertomb,
# Loran, Isz) asks /channel/search for its dungeon: FixedOrGeneralList [{"FixedOrGeneral": 2}],
# HolyGrailTypeId, RitualLevel, SubFeatureFlag 256, GetCount 1 (server log 2026-09-29 00:17:07,
# Pthumeru level 1; that player was back at the login 18 s later). This server answered with an
# empty list, so the ritual had no dungeon. The ten fixed dungeons' FormData is from
# NoDreamForTheHunterBB by Fahad (github.com/Mob100percent/NoDreamForTheHunterBB, GPL-3.0), taken
# from thehuntersdream.com. Each becomes one shared channel here (made at its first search, owner 0
# so no player's share / upload changes it), so every player gets the same ChannelId and glyph.
# Every entry: SubFeatureFlag 256, FixedOrGeneral 2, FormDataVersion 0, ShareLevel 2, Status 1,
# UnlockFlagList [0, <bit>, 0] (the bit the game sends in UnlockedFlagList), no WishMaterialList.
FIXED_CHALICE_SUB_FEATURE = 256
FIXED_CHALICES = [
    # (HolyGrailTypeId, RitualLevel, unlock bit, FormData)
    (0, 1, 1, "BQAAAIwAAAAAWgod3RcAAP////////////////////8+pk47WgAAAP////////////////////+g3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAtAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (0, 2, 128, "BQAAAIwAAAAAWhQdQRgAAP////////////////////8+pk47WgAAAEimTjtbAAAA//////////+g3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (0, 3, 16384, "BQAAAIwAAAAAWh4dpRgAAKBHXzs9AAAAoOaDO1UAAAA+pk47WgAAAEimTjtbAAAA//////////+g3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (0, 4, 2097152, "BQAAAIwAAAAAWigdCRkAAEDOYDs+AAAAQANtO0YAAAA+pk47WgAAAEimTjtbAAAAXKZOO10AAACg3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (0, 5, 268435456, "BQAAAIwAAAAAWjIdbRkAAEDOYDs+AAAA4POGO1cAAAA+pk47WgAAAEimTjtbAAAAXKZOO10AAACg3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAtAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (1, 2, 256, "BQAAAIwAAAAAWhUdSxgAAP////////////////////+spk47XgAAAMB1ETsKAAAA//////////+g3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (1, 3, 32768, "BQAAAIwAAAAAWh8drxgAAP//////////QG2FO1YAAACspk47XgAAALamTjtfAAAAwHUROwoAAACg3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (2, 4, 8388608, "BQAAAIwAAAAAWiodHRkAAP////////////////////8kp047YAAAAP////////////////////+g3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (2, 5, 1073741824, "BQAAAIwAAAAAWjQdgRkAAP//////////gHqIO1gAAAAkp047YAAAAC6nTjthAAAA//////////+g3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
    (3, 5, 2147483648, "BQAAAIwAAAAAWjUdixkAAD6mTjtaAAAAIAGKO1kAAACSp047YgAAAEimTjtbAAAArKZOO14AAACg3UY7LQAAAP///////////////////////////////y0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="),
]


def _date(when):
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(when))


def _new_glyph(taken):
    while True:
        word = "".join(GLYPH_LETTERS[b % len(GLYPH_LETTERS)] for b in os.urandom(8))
        if word not in taken:
            return word


def _channel_entry(channel):
    entry = {"FormDataVersion": 0, "FormData": "", "ShareLevel": 0, "Status": 1, "RitualLevel": 1,
             "HolyGrailTypeId": 0, "FixedOrGeneral": 0, "SubFeatureFlag": 0, "TurnoutLevel": 0,
             "WishMaterialList": []}
    entry.update({key: value for key, value in channel.items() if not key.startswith("_")})
    entry["CreateDate"] = _date(channel.get("_created", time.time()))
    entry["LastPlayDate"] = _date(channel.get("_played", time.time()))
    return entry


def _channel_ids(body):
    return _ids(body, "ChannelIdList", "ChannelId")


def channel_upload(req):
    body = _request(req)
    user = _as_int(body.get("UserId"))
    now = time.time()
    with STORE.lock:
        channels = STORE.data["channels"]
        known = channels.get(str(_as_int(body.get("ChannelId"))))
        if known is not None and known.get("CreateUserId") == user:
            channel = known  # the same dungeon uploaded again (resume / share change)
        else:
            channel_id = STORE.next_id()
            channel = {"ChannelId": channel_id, "_created": now,
                       "DiscernmentWord": _new_glyph({c.get("DiscernmentWord")
                                                      for c in channels.values()}),
                       "CreateUserId": user, "CreateCharaId": _as_int(body.get("CharaId"))}
            channels[str(channel_id)] = channel
        channel.update({key: value for key, value in body.items()
                        if key not in CHANNEL_SKIP and key not in CHANNEL_SERVER_KEYS})
        channel["_played"] = now
        STORE.save()
    print("      chalice: user {} uploaded dungeon {} glyph {} (fields: {})".format(
        user, channel["ChannelId"], channel["DiscernmentWord"],
        ",".join(sorted(k for k in body if k != "SessionId"))), flush=True)
    return empty("ChannelUploadResponse", ChannelId=channel["ChannelId"],
                 DiscernmentWord=channel["DiscernmentWord"])


def channel_share(req):
    body = _request(req)
    user = _as_int(body.get("UserId"))
    with STORE.lock:
        channel = STORE.data["channels"].get(str(_as_int(body.get("ChannelId"))))
        if channel is not None and channel.get("CreateUserId") == user:
            if "ShareLevel" in body:
                channel["ShareLevel"] = _as_int(body["ShareLevel"])
            STORE.save()
    print("      chalice: share {} -> {}{}".format(
        body.get("ChannelId"), body.get("ShareLevel"),
        "" if channel else " (unknown dungeon, request: {})".format(req[:300])), flush=True)
    if channel is None:
        return empty("ChannelShareResponse")
    return empty("ChannelShareResponse", ChannelId=channel["ChannelId"],
                 DiscernmentWord=channel["DiscernmentWord"])


def _channels_by_id(body):
    with STORE.lock:
        channels = STORE.data["channels"]
        ids = _channel_ids(body)
        return ids, [_channel_entry(channels[str(i)]) for i in ids if str(i) in channels]


def channel_get_info(req):
    ids, found = _channels_by_id(_request(req))
    if ids:
        print("      chalice: info for {} -> {} known".format(ids, len(found)), flush=True)
    return empty("ChannelGetInfoResponse", ChannelInfoList=found)


def channel_get_details(req):
    ids, found = _channels_by_id(_request(req))
    print("      chalice: details for {} -> {} known".format(ids, len(found)), flush=True)
    return empty("ChannelGetDetailsInfoResponse", ChannelList=found)


def channel_word_search(req):
    body = _request(req)
    word = str(body.get("SearchWord") or body.get("DiscernmentWord") or "").strip().lower()
    with STORE.lock:
        found = [_channel_entry(c) for c in STORE.data["channels"].values()
                 if word and c.get("DiscernmentWord") == word]
    print("      chalice: glyph search '{}' -> {} found".format(word, len(found)), flush=True)
    # Both shapes: NoDreamForTheHunterBB answers a glyph search with the dungeon's fields at the top
    # level, this server used a ChannelList; which one the game reads is not checked yet. Unread keys
    # are ignored by the game's parser.
    return empty("ChannelWordSearchResponse", ChannelList=found, **(found[0] if found else {}))


def _fixed_channel(grail, level, bit, form_data):
    """The shared channel of one fixed chalice dungeon, made on first use. Call with STORE.lock."""
    channels = STORE.data["channels"]
    key = "{}-{}".format(grail, level)
    known = channels.get(str(STORE.data.setdefault("fixed_channels", {}).get(key)))
    if known is not None:
        return known, False
    channel_id = STORE.next_id()
    channel = {"ChannelId": channel_id, "_created": time.time(), "_played": time.time(),
               "DiscernmentWord": _new_glyph({c.get("DiscernmentWord") for c in channels.values()}),
               "CreateUserId": 0, "CreateCharaId": 0,
               "FixedOrGeneral": 2, "FormData": form_data, "FormDataVersion": 0,
               "HolyGrailTypeId": grail, "RitualLevel": level,
               "SubFeatureFlag": FIXED_CHALICE_SUB_FEATURE, "ShareLevel": 2, "Status": 1,
               "TurnoutLevel": 0, "WishMaterialList": [],
               "UnlockFlagList": [{"UnlockFlag": 0}, {"UnlockFlag": bit}, {"UnlockFlag": 0}]}
    channels[str(channel_id)] = channel
    STORE.data["fixed_channels"][key] = channel_id
    return channel, True


def channel_search(req):
    body = _request(req)
    kinds = {_as_int(x.get("FixedOrGeneral"), -1) for x in body.get("FixedOrGeneralList") or []
             if isinstance(x, dict)}
    # a field the game leaves out or sends as null does not narrow the search
    wanted = {key: _as_int(body[key], -1) for key in ("HolyGrailTypeId", "RitualLevel",
                                                      "SubFeatureFlag", "Status")
              if body.get(key) is not None}
    limit = max(_as_int(body.get("GetCount"), 1), 1)
    found, made = [], []
    if 2 in kinds:
        with STORE.lock:
            for grail, level, bit, form_data in FIXED_CHALICES:
                have = {"HolyGrailTypeId": grail, "RitualLevel": level,
                        "SubFeatureFlag": FIXED_CHALICE_SUB_FEATURE, "Status": 1}
                if any(have[key] != value for key, value in wanted.items()):
                    continue
                channel, new = _fixed_channel(grail, level, bit, form_data)
                if new:
                    made.append(channel["ChannelId"])
                found.append(_channel_entry(channel))
                if len(found) >= limit:
                    break
            if made:
                STORE.save()
    print("      chalice: search {} {} -> {}{}".format(
        sorted(kinds), wanted,
        ", ".join("fixed dungeon {} glyph {}".format(c["ChannelId"], c["DiscernmentWord"])
                  for c in found) or "nothing (only fixed dungeons are answered)",
        " (new: {})".format(made) if made else ""), flush=True)
    return empty("ChannelSearchResponse", ChannelList=found)


# Server notice (welcome message): server-notice.txt next to the messages file (--data). First line =
# title, the rest = the text. Read at every request, so editing it needs no restart; no file = no
# notice. Entry fields from the game's reply parser 0x01E7F240 (NoticeList, CheckTime, then
# Title / Notice per entry; the parser logs them as "[%llu]%s:%s").
def normal_notice(req):
    path = os.path.join(os.path.dirname(os.path.abspath(ARGS.data)), "server-notice.txt")
    notices = []
    try:
        with open(path, encoding="utf-8-sig") as f:
            lines = f.read().replace("\r\n", "\n").strip().split("\n")
        title, text = lines[0].strip(), "\n".join(lines[1:]).strip()
        if title or text:
            stamp = int(os.path.getmtime(path))
            # Id + base64 text: confirmed by a server owner (2026-10-16) - with NoticeId and plain
            # text the game showed "There is no new information."
            notices.append({"Id": stamp, "NoticeId": stamp,
                            "Title": base64.b64encode(title.encode("utf-8")).decode("ascii"),
                            "Notice": base64.b64encode(text.encode("utf-8")).decode("ascii"),
                            "CheckTime": stamp})
    except OSError:
        pass
    return ok("NoticeNormalGetResponse", CheckTime=int(time.time()), NoticeList=notices)


# Endpoint -> response. Paths are the game's own ("Name:%hS/path" strings in the eboot). Messages and
# bloodstains are stored (OnlineStore); the rest
# these features store anything yet: they answer "empty" in the shape the game expects, so the game
# carries on instead of dropping to the title.
ROUTES = {
    "/basic_utils/login": login,
    "/basic_utils/get_normal_notice": normal_notice,
    "/basic_utils/get_emergency_notice":
        lambda req: ok("EmergencyNoticeGetResponse", NoticeList=[]),
    "/basic_utils/get_user_agreement": lambda req: ok("UserAgreementGetResponse", Agreements=[]),
    "/basic_utils/get_datetime":
        lambda req: ok("DateTimeResponse", Timestamp=int(time.time() * 1000)),
    "/basic_utils/sync_chara_id": lambda req: ok("SyncCharaIdResponse", PublishCharacterIdList=[]),
    "/penalty/check_user_priority_move_count":
        lambda req: ok("UserPropertiesMoveCountCheckResponse"),
    "/blood_messenger/exist_messages": blood_exist,

    # blood messages, shared (see OnlineStore)
    "/blood_messenger/create": blood_create,
    "/blood_messenger/message_area": blood_area,
    "/blood_messenger/evaluate_message": blood_evaluate,
    "/blood_messenger/evaluation": blood_get_evaluation,
    "/blood_messenger/delete": blood_delete,

    # bloodstains and death visions, shared
    "/tomb_messenger/create": tomb_create,
    "/tomb_messenger/message_area": tomb_area,
    "/tomb_messenger/death_vision_get": tomb_death_vision,
    "/tomb_messenger/delete": tomb_delete,

    # wandering ghosts - the one that caused the kicks
    "/wandering_ghost/create": lambda req: empty("WanderingGhostCreateResponse"),
    "/wandering_ghost/get": lambda req: empty("WanderingGhostGetResponse"),

    # lamp / respawn point messages
    "/chair_messenger/get": lambda req: empty("ChairMessGetListResponse", ChairMessCount=0),
    "/chair_messenger/update": lambda req: empty("ChairMessRespawnPointNoticeResponse"),

    # chalice dungeon glyphs (channels), shared; search answers only the fixed dungeons
    "/channel/upload": channel_upload,
    "/channel/share": channel_share,
    "/channel/search": channel_search,
    "/channel/word_search": channel_word_search,
    "/channel/get_details_info": channel_get_details,
    "/channel/get_info": channel_get_info,
    "/channel/random_join": lambda req: empty("ChannelRandomJoinResponse"),
    "/channel/add_material": lambda req: empty("ChannelAddMaterialResponse"),
    "/channel/notify_add_material_complete":
        lambda req: empty("ChannelAddMaterialCompleteNotifyResponse"),

    # the rest
    "/messenger_shell/upload": shell_upload,
    "/penalty/notify_multi_play_error": lambda req: empty("MultiPlayNetErrorResponse"),
    "/penalty/notify_user_properties_move_count":
        lambda req: empty("UserPropertiesMoveCountResponse"),
}

# How many request bodies to keep in the log per path. The game's request formats are what the
# real messages / bloodstains / glyph services will be built from, so the first few of each are
# worth having without switching on --verbose.
BODY_SAMPLES = 3
_BODY_COUNTS = {}

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        pass

    def log_error(self, fmt, *a):
        print("[{}] http error from {}: {}".format(stamp(), self.client_address[0], fmt % a),
              flush=True)

    def _send(self, body, content_type, status=200):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("SP-RES-KIND", "0")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _handle(self):
        try:
            self._handle_inner()
        except Exception:  # a broken reply must never take the server down mid-session
            print("[{}] ERROR handling {} {} from {}:\n{}".format(
                stamp(), self.command, self.path, self.client_address[0],
                traceback.format_exc()), flush=True)
            try:
                self._send(json.dumps({"ResKind": 0}).encode("utf-8"), "application/json")
            except Exception:
                pass

    def _handle_inner(self):
        caller = self.client_address[0]
        is_local = caller in LOCAL_ADDRESSES or (ARGS.advertise and caller == ARGS.advertise)
        path = self.path.split("?", 1)[0]
        length = int(self.headers.get("Content-Length") or 0)
        payload = self.rfile.read(length).decode("utf-8", "replace") if length else ""

        bbhost = ARGS.bbhost and bbhost_service.is_bbhost(self.headers)

        if path.rstrip("/").endswith("/ss.info"):
            if bbhost:
                info = SS_INFO_BBHOST_LOCAL_B64 if is_local else SS_INFO_BBHOST_B64
            else:
                info = SS_INFO_LOCAL_B64 if is_local else SS_INFO_B64
            print("[{}] {} {} {}  -> ss.info ({}{}), {} bytes".format(
                stamp(), caller, self.command, path, "this PC" if is_local else "remote",
                ", bbhost" if bbhost else "", len(info)), flush=True)
            self._send(info, "text/plain; charset=utf-8")
            return

        if ARGS.bbhost:
            query = self.path.split("?", 1)[1] if "?" in self.path else ""
            answer = bbhost_service.handle(path, query, payload, self.headers, caller)
            if answer is not None:
                status, reply = answer
                if ARGS.verbose or (status != 200 and path not in ("/np/events/poll",)):
                    print("[{}] {} {} {} -> {} {}".format(stamp(), caller, self.command, path, status,
                                                         json.dumps(reply)[:200]), flush=True)
                self._send(json.dumps(reply).encode("utf-8"), "application/json", status)
                return

        handler = ROUTES.get(path)
        if bbhost and path.startswith("/summon_messenger/"):
            try:
                reply = bbhost_service.summon_call(path, json.loads(payload or "{}"), self.headers)
            except ValueError:
                reply = {"ResKind": 1}
            handler = True
        elif bbhost and path == "/basic_utils/login":
            try:
                platform_id = json.loads(payload or "{}").get("PlatformAccountId")
            except ValueError:
                platform_id = None
            account, online = bbhost_service.login_account(self.headers, platform_id)
            if account is None:
                print("[{}] {} bbhost login without an account - refused".format(stamp(), caller),
                      flush=True)
                self._send(json.dumps({"ResKind": 1}).encode("utf-8"), "application/json", 401)
                return
            reply = login(payload, account)
            bbhost_service.note_login(reply["UserId"], online)
        else:
            reply = handler(payload) if handler else empty("UnknownResponse")
        body = json.dumps(reply).encode("utf-8")
        label = "" if handler else "  (UNKNOWN path - answered with every list empty)"
        print("[{}] {} {} {}{}".format(stamp(), caller, self.command, path, label), flush=True)
        seen = _BODY_COUNTS.get(path, 0)
        if payload and not ARGS.verbose and seen < BODY_SAMPLES and "summon" not in path \
                and path != "/basic_utils/login":
            _BODY_COUNTS[path] = seen + 1
            print("      request sample {}/{}: {}".format(seen + 1, BODY_SAMPLES, payload[:600]),
                  flush=True)
        if ARGS.verbose:
            for key, value in self.headers.items():
                print("      {}: {}".format(key, value), flush=True)
            if payload:
                print("      body: {}".format(payload[:400]), flush=True)
        self._send(body, "application/json")

    do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = _handle

class Tee:
    """Everything printed goes to the log file at once and to the console window through a queue.

    2026-10-20 (Eryk's server, 17:19:22-17:21:26): nothing was answered for two minutes while
    shadNet ran on, and both players' games hung waiting. Every request prints before it replies,
    and a paused console window (a click into it - Windows QuickEdit - stops every write until a
    key is pressed) stopped every one of them. The console now has its own writer thread and a
    bounded queue, so a paused window only delays the console; the log file keeps every line.
    """

    CONSOLE_QUEUE_MAX = 5000

    def __init__(self, console, log):
        self.console = console
        self.log = log
        self.lock = threading.Lock()
        self.queue = collections.deque()
        self.dropped = 0
        self.wake = threading.Event()
        threading.Thread(target=self._console_writer, daemon=True).start()

    def write(self, text):
        with self.lock:
            try:
                self.log.write(text)
            except Exception:
                pass
            if len(self.queue) < self.CONSOLE_QUEUE_MAX:
                self.queue.append(text)
            else:
                self.dropped += 1
        self.wake.set()
        return len(text)

    def flush(self):
        with self.lock:
            try:
                self.log.flush()
            except Exception:
                pass

    def _console_writer(self):
        while True:
            self.wake.wait()
            with self.lock:
                text = "".join(self.queue)
                self.queue.clear()
                dropped, self.dropped = self.dropped, 0
                self.wake.clear()
            if dropped:
                text += ("      (console: {} line(s) not shown while this window was paused - they "
                         "are in the log file)\n".format(dropped))
            if self.console is None or not text:
                continue
            try:
                self.console.write(text)
                self.console.flush()
            except Exception:
                pass


def disable_quick_edit():
    """Windows: a click into the console window must not pause the server (QuickEdit off).
    Text can still be selected through the window menu (Edit > Mark)."""
    if os.name != "nt":
        return
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-10)  # STD_INPUT_HANDLE
        mode = ctypes.c_uint32()
        if kernel32.GetConsoleMode(handle, ctypes.byref(mode)) and mode.value & 0x0040:
            # clear ENABLE_QUICK_EDIT_MODE (0x40), keep ENABLE_EXTENDED_FLAGS (0x80) so it sticks
            kernel32.SetConsoleMode(handle, (mode.value & ~0x0040) | 0x0080)
            print("console: click-to-select (QuickEdit) is off - a click in this window no longer "
                  "pauses the server")
    except Exception:
        pass


def open_log(log_dir):
    os.makedirs(log_dir, exist_ok=True)
    name = "gameserver-{}.log".format(datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    path = os.path.join(log_dir, name)
    handle = open(path, "a", encoding="utf-8", buffering=1)
    sys.stdout = Tee(sys.__stdout__, handle)
    sys.stderr = Tee(sys.__stderr__, handle)
    return path



# ---- BBSC relay (2026-09-29) --------------------------------------------------------------------
# The BBSC mod DLL sends BBSC's own packets (host position for following, world flags, Dream state)
# here instead of over the emulator's P2P signaling, which a mod cannot reach. One TCP connection
# per player on --relay-port; frames are [u32 little-endian length][payload], and every frame from
# one player goes to every other player connected to this server. Payloads are opaque to the
# server. It also tells each player how many others are connected (frame 0x7F, count).
import socket
import struct

RELAY_CLIENTS = {}  # conn -> RelayClient
RELAY_LOCK = threading.Lock()
# Immutable snapshot rebuilt on connects/disconnects, not on every frame.
# Keep connection keys, so the sender exclusion remains exactly "is not conn".
RELAY_ROSTER = ()
RELAY_QUEUE_LIMIT = 512 * 1024  # bytes waiting for one player before he is dropped as too slow
# Stage 3A: coalesce ALREADY-QUEUED frames for each client into bounded TCP writes.
# No queue wait is introduced. Wire bytes, per-peer FIFO order and recipient list do not change.
RELAY_WRITE_BATCH_MAX_BYTES = 64 * 1024
RELAY_WRITE_BATCH_MAX_FRAMES = 64

# Share a single immutable encoded frame across recipient queues. With
# --relay-session-routing, high-rate session-state packets are sent only to the
# host and guests that advertised the same session in the existing 0x40 envelope.
# Discovery/follow packets and old or malformed frames retain global delivery.
RELAY_STATS_LOCK = threading.Lock()
RELAY_STATS = collections.Counter()
# Index of currently recorded session hints, protected by RELAY_LOCK.
RELAY_HINT_COUNTS = collections.Counter()
RELAY_HINTED_CLIENTS = 0
RELAY_SESSION_TYPES = frozenset((0x22, 0x23, 0x24, 0x25, 0x26, 0x27))
RELAY_DISCOVERY_TYPES = frozenset((0x20, 0x30))


def _relay_stats_enabled():
    return bool(getattr(ARGS, "relay_tcp_stats_interval", 0) > 0)


def _relay_parse_envelope(payload):
    """Untrusted observational hint, NOT an authentication/permission check.

    Client format: SHAD 0x40, sender uint32 LE, host-session uint32 LE,
    nested SHAD plus inner packet type. Older/malformed frames stay global.
    """
    if (len(payload) < 18 or payload[:5] != b"SHAD\x40" or
            payload[13:17] != b"SHAD"):
        return None
    sender, session = struct.unpack_from("<II", payload, 5)
    if not sender:
        return None
    return sender, session, payload[17]


def _relay_record(**values):
    with RELAY_STATS_LOCK:
        RELAY_STATS.update(values)


def _relay_snapshot():
    with RELAY_STATS_LOCK:
        return RELAY_STATS.copy()


def _relay_log_stats(interval):
    previous = _relay_snapshot()
    while True:
        time.sleep(interval)
        now = _relay_snapshot()
        delta = now.copy()
        delta.subtract(previous)
        previous = now
        with RELAY_LOCK:
            clients = len(RELAY_CLIENTS)
        print(("bbsc TCP relay stats {}s: clients={}; rx_frames={} / {} bytes; "
               "enqueued_frames={} / {} bytes; fanout={:.2f}; "
               "observed_session_frames={}; discovery_frames={}; opaque_frames={}; "
               "shadow_would_skip={} / {} bytes; slow_disconnects={}; send_errors={}").format(
                   interval, clients, delta["rx_frames"], delta["rx_bytes"],
                   delta["queued_frames"], delta["queued_bytes"],
                   delta["queued_frames"] / max(delta["rx_frames"], 1),
                   delta["session_frames"], delta["discovery_frames"],
                   delta["opaque_frames"], delta["shadow_skip_frames"],
                   delta["shadow_skip_bytes"], delta["slow_disconnects"],
                   delta["send_errors"]), flush=True)



def _relay_register_client(conn, client):
    """Register a socket, refreshing the snapshot and observational hints."""
    global RELAY_ROSTER, RELAY_HINTED_CLIENTS
    with RELAY_LOCK:
        prior = RELAY_CLIENTS.get(conn)
        if prior is not None and prior.session_hint is not None:
            RELAY_HINT_COUNTS[prior.session_hint] -= 1
            RELAY_HINTED_CLIENTS -= 1
            if not RELAY_HINT_COUNTS[prior.session_hint]:
                del RELAY_HINT_COUNTS[prior.session_hint]
        RELAY_CLIENTS[conn] = client
        if client.session_hint is not None:
            RELAY_HINT_COUNTS[client.session_hint] += 1
            RELAY_HINTED_CLIENTS += 1
        RELAY_ROSTER = tuple(RELAY_CLIENTS.items())
        return len(RELAY_CLIENTS)


def _relay_unregister_client(conn):
    """Drop a socket, including its monitoring-only session-hint entry."""
    global RELAY_ROSTER, RELAY_HINTED_CLIENTS
    with RELAY_LOCK:
        old_client = RELAY_CLIENTS.pop(conn, None)
        if old_client is not None and old_client.session_hint is not None:
            RELAY_HINT_COUNTS[old_client.session_hint] -= 1
            RELAY_HINTED_CLIENTS -= 1
            if RELAY_HINT_COUNTS[old_client.session_hint] <= 0:
                del RELAY_HINT_COUNTS[old_client.session_hint]
        RELAY_ROSTER = tuple(RELAY_CLIENTS.items())
        return len(RELAY_CLIENTS)


def _relay_forward(conn, payload):
    """Forward one unchanged frame globally or to its advertised session.

    Session hints are routing hints, not authentication. They only narrow who
    receives ordinary state frames; discovery, follow and opaque traffic remain
    global for compatibility with joining and older clients.
    """
    global RELAY_HINTED_CLIENTS
    observe = _relay_stats_enabled()
    skip_count = 0
    category = "opaque_frames"
    routing = bool(getattr(ARGS, "relay_session_routing", False))
    envelope = _relay_parse_envelope(payload)
    with RELAY_LOCK:
        # Atomic reference to the old-style membership snapshot, preserving
        # insertion order and socket identities without per-frame list copies.
        roster = RELAY_ROSTER
        sender_client = RELAY_CLIENTS.get(conn)
        if envelope is not None and sender_client is not None and (observe or routing):
            sender, session, kind = envelope
            sender_client.sender_hint = sender
            sender_client.seen_envelope = True
            old_hint = sender_client.session_hint
            new_hint = session or None
            if new_hint != old_hint:
                if old_hint is not None:
                    RELAY_HINT_COUNTS[old_hint] -= 1
                    RELAY_HINTED_CLIENTS -= 1
                    if RELAY_HINT_COUNTS[old_hint] <= 0:
                        del RELAY_HINT_COUNTS[old_hint]
                if new_hint is not None:
                    RELAY_HINT_COUNTS[new_hint] += 1
                    RELAY_HINTED_CLIENTS += 1
                sender_client.session_hint = new_hint
            if kind in RELAY_DISCOVERY_TYPES:
                category = "discovery_frames"
            elif session and kind in RELAY_SESSION_TYPES:
                category = "session_frames"
                # Sender has just been indexed under this session, so
                # counting all other known sessions excludes the sender.
                skip_count = RELAY_HINTED_CLIENTS - RELAY_HINT_COUNTS.get(session, 0)
                if routing:
                    # The host identifies itself with sender == session; guests
                    # advertise session == host. A modern client that has sent a
                    # valid envelope but still reports session zero keeps receiving
                    # early snapshots until it advertises a nonzero party. Completely
                    # silent/legacy sockets are not flooded with every world's state.
                    roster = tuple(
                        (peer_conn, peer) for peer_conn, peer in roster
                        if (peer_conn is conn or
                            (peer.seen_envelope and peer.session_hint is None) or
                            peer.session_hint == session or
                            peer.sender_hint == session))
    # The encoded bytes and per-recipient FIFO ordering remain unchanged.
    frame = struct.pack("<I", len(payload)) + payload if roster else b""
    queued = 0
    for peer_conn, client in roster:
        if peer_conn is not conn:
            queued += client.send_framed(frame)
    if observe:
        _relay_record(rx_frames=1, rx_bytes=4 + len(payload),
                      queued_frames=queued, queued_bytes=queued * (4 + len(payload)),
                      shadow_skip_frames=skip_count,
                      shadow_skip_bytes=skip_count * (4 + len(payload)),
                      **{category: 1})
    return queued


class RelayClient:
    """One player's outgoing frames (2026-10-13). Each player has his own writer thread, so a
    player whose game stopped reading (crash, dead link) can no longer stall the reader of everyone
    else: before, the sender's thread blocked in sendall to him, stopped reading the sender, and
    the sender's game dropped the link ~10 s later - every 10 s while hosting."""

    def __init__(self, conn, name):
        self.conn = conn
        self.name = name
        self.queue = collections.deque()
        self.queued = 0
        self.cond = threading.Condition()
        self.closed = False
        self.reason = ""
        self.sender_hint = None
        self.session_hint = None
        self.seen_envelope = False
        threading.Thread(target=self._writer, daemon=True).start()

    def send(self, payload):
        # Retain original API for peer-count (0x7f) announcements.
        return self.send_framed(struct.pack("<I", len(payload)) + payload)

    def send_framed(self, data):
        """Queue already length-prefixed immutable bytes; return accepted/not accepted."""
        with self.cond:
            if self.closed:
                return False
            if self.queued + len(data) > RELAY_QUEUE_LIMIT:
                self._fail_locked("too slow ({} bytes waiting)".format(self.queued))
                return False
            was_empty = not self.queue
            self.queue.append(data)
            self.queued += len(data)
            # A sleeping writer can exist only while the queue is empty.
            if was_empty:
                self.cond.notify()
            return True

    def _fail_locked(self, reason):
        if not self.reason:
            self.reason = reason
            if _relay_stats_enabled():
                if reason.startswith("too slow"):
                    _relay_record(slow_disconnects=1)
                elif reason.startswith("send failed"):
                    _relay_record(send_errors=1)
        self.closed = True
        self.queue.clear()
        self.queued = 0
        self.cond.notify()
        try:
            self.conn.shutdown(socket.SHUT_RDWR)  # ends his reader too
        except OSError:
            pass

    def fail(self, reason):
        with self.cond:
            self._fail_locked(reason)

    def _writer(self):
        while True:
            with self.cond:
                while not self.queue and not self.closed:
                    self.cond.wait()
                if self.closed:
                    return
                # Drain only frames already waiting. This adds no artificial
                # latency and leaves the same 512-KiB enqueue/backpressure rule.
                data = self.queue.popleft()
                self.queued -= len(data)
                # The common case is a single frame with an otherwise empty
                # queue: avoid allocating a list or copying its bytes.
                if self.queue and len(data) < RELAY_WRITE_BATCH_MAX_BYTES:
                    batch = [data]
                    size = len(data)
                    while (self.queue and len(batch) < RELAY_WRITE_BATCH_MAX_FRAMES and
                           size + len(self.queue[0]) <= RELAY_WRITE_BATCH_MAX_BYTES):
                        frame = self.queue.popleft()
                        batch.append(frame)
                        size += len(frame)
                        self.queued -= len(frame)
                    if len(batch) > 1:
                        data = b"".join(batch)
            # Preserve the exact concatenated length-prefixed TCP stream.
            try:
                self.conn.sendall(data)
            except OSError as error:
                self.fail("send failed ({})".format(error))
                return


def _relay_announce():
    with RELAY_LOCK:
        clients = list(RELAY_CLIENTS.values())
    for client in clients:
        client.send(bytes([0x7F, min(len(clients) - 1, 255)]))


def _relay_client(conn, address):
    name = "{}:{}".format(*address)
    client = RelayClient(conn, name)
    count = _relay_register_client(conn, client)
    print("      relay: {} connected ({} player(s))".format(name, count), flush=True)
    _relay_announce()
    frames = 0
    reason = ""
    started = time.time()
    try:
        while True:
            header = _recv_exact(conn, 4)
            if header is None:
                reason = "closed by the player"
                break
            size = struct.unpack("<I", header)[0]
            if size > 64 * 1024:
                reason = "oversized frame ({} bytes)".format(size)
                break
            payload = _recv_exact(conn, size) if size else b""
            if payload is None:
                reason = "closed by the player"
                break
            frames += 1
            _relay_forward(conn, payload)
    except OSError as error:
        reason = "read failed ({})".format(error)
    finally:
        left = _relay_unregister_client(conn)
        if client.reason:
            reason = client.reason  # the writer gave up first
        client.fail(reason or "gone")
        try:
            conn.close()
        except OSError:
            pass
        print("      relay: {} left after {} frames, {:.0f} s ({}) ({} player(s))".format(
            name, frames, time.time() - started, reason, left), flush=True)
        _relay_announce()


def _recv_exact(conn, size):
    data = b""
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def start_relay(host, port):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((host, port))
    listener.listen(16)

    def accept_loop():
        while True:
            conn, address = listener.accept()
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            if hasattr(socket, "TCP_USER_TIMEOUT"):  # Linux: unacked data for 20 s = dead link
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_USER_TIMEOUT, 20000)
            threading.Thread(target=_relay_client, args=(conn, address), daemon=True).start()

    threading.Thread(target=accept_loop, daemon=True).start()
    print("BBSC relay on {}:{} (for the BBSC mod); immutable frame reuse enabled".format(
        host, port))
    interval = getattr(ARGS, "relay_tcp_stats_interval", 0)
    if interval > 0:
        threading.Thread(target=_relay_log_stats, args=(interval,), daemon=True,
                         name="bbsc-tcp-stats").start()


class QuietHTTPServer(ThreadingHTTPServer):
    # The game drops keep-alive connections with a reset; that is normal, not worth a traceback.
    def handle_error(self, request, client_address):
        if isinstance(sys.exc_info()[1], (ConnectionResetError, ConnectionAbortedError, BrokenPipeError)):
            return
        super().handle_error(request, client_address)


def _bbhost_ss_info(b64, local):
    """build_ss_info points bbhost summons at 127.0.0.1:20443; move them to where this server is."""
    xml = base64.b64decode(b64).decode("utf-8")
    here = "http://127.0.0.1:{}".format(ARGS.port) if local else \
        "http://{}:{}".format(ARGS.advertise or ARGS.host, ARGS.port)
    return base64.b64encode(xml.replace("http://127.0.0.1:20443", here).encode("utf-8"))


def _public_ipv4(name):
    try:
        return socket.gethostbyname(name) if name and name != "0.0.0.0" else ""
    except OSError:
        return ""


def start_bbhost():
    """bbhost players' services: accounts, rooms, events (HTTP) and STUN + relay (UDP)."""
    lo, hi = (int(x) for x in ARGS.bbhost_relay_ports.split("-"))
    public = ARGS.bbhost_public_ip or _public_ipv4(ARGS.advertise or ARGS.host)
    bbhost_service.configure(
        log=print, heartbeat_timeout=ARGS.bbhost_heartbeat_timeout,
        require_account=not ARGS.bbhost_open, public_ip=public, relay_ports=(lo, hi),
        accounts_path=os.path.join(os.path.dirname(os.path.abspath(ARGS.data)), "bbhost-accounts.json"))
    if not ARGS.bbhost_stun_port:
        return
    if not public:
        print("bbhost: no public address (set --advertise or --bbhost-public-ip): relay off")
    bbhost_service.RELAY.start(ARGS.bbhost_stun_bind or ARGS.host, ARGS.bbhost_stun_port,
                               rcvbuf_bytes=ARGS.bbhost_udp_rcvbuf)
    if ARGS.bbhost_udp_stats_interval:
        interval = ARGS.bbhost_udp_stats_interval
        if not 5 <= interval <= 3600:
            raise ValueError("UDP stats interval must be 5..3600 seconds")

        def log_udp_stats():
            before = bbhost_service.RELAY.counters()
            while True:
                time.sleep(interval)
                after = bbhost_service.RELAY.counters()
                delta = {key: after[key] - before[key] for key in after}
                before = after
                print("bbhost UDP stats {}s: rx={} datagrams / {} bytes; tx={} datagrams / {} bytes; "
                      "shad forwarded={}; bbhost forwarded={}; send_errors={}; unrecognized={}".format(
                          interval, delta["datagrams_in"], delta["bytes_in"],
                          delta["datagrams_out"], delta["bytes_out"],
                          delta["shad_frames"], delta["frames"],
                          delta["send_errors"], delta["unrecognized_in"]), flush=True)

        threading.Thread(target=log_udp_stats, daemon=True, name="bbhost-udp-stats").start()

    def follow_address():
        # a DuckDNS name can change address; the relay addresses must follow it
        while True:
            time.sleep(300)
            now = _public_ipv4(ARGS.advertise or ARGS.host)
            if now and now != bbhost_service.OPTS["public_ip"]:
                print("bbhost: public address is now {}".format(now), flush=True)
                bbhost_service.OPTS["public_ip"] = now
    if not ARGS.bbhost_public_ip:
        threading.Thread(target=follow_address, daemon=True).start()


def main():
    global ARGS, SS_INFO_B64, SS_INFO_LOCAL_B64, SS_INFO_BBHOST_B64, SS_INFO_BBHOST_LOCAL_B64
    global AREA_REPLY_BUDGET
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shadnet", required=True,
                        help="address of the real shadnet server, e.g. 26.1.2.3")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20443)
    parser.add_argument("--relay-port", type=int, default=20444,
                        help="BBSC mod relay (TCP); 0 turns it off")
    parser.add_argument("--relay-tcp-stats-interval", type=int, default=0,
                        help="TCP fan-out and untrusted-session SHADOW telemetry seconds; "
                             "0 disables statistics. Routing stays global.")
    parser.add_argument("--relay-session-routing", action="store_true",
                        help="route high-rate BBSC state frames only within their advertised "
                             "host session; discovery/follow/legacy frames remain global")
    parser.add_argument("--advertise",
                        help="address to put in ss.info if it differs from --host")
    parser.add_argument("--summon-interval", type=int, default=0,
                        help="seconds between summon polls (vanilla 35; try 5 for quick summons)")
    parser.add_argument("--level-range", type=int, default=0,
                        help="co-op level window (vanilla 20; try 999 to match at any level)")
    parser.add_argument("--no-playlog", action="store_true",
                        help="turn off the play-log uploads to Amazon S3 (recommended)")
    parser.add_argument("--tomb-max-response-bytes", type=int, default=None,
                        help="message / bloodstain reply budget in bytes (default 65536 - the "
                             "Dantelion2 Panic 265 fix); 0 = no cap")
    parser.add_argument("--verbose", action="store_true", help="log headers and request bodies")
    parser.add_argument("--no-bbhost", dest="bbhost", action="store_false",
                        help="turn off the bbhost players' services (bbhost_service.py)")
    parser.add_argument("--bbhost-port", type=int, default=18671,
                        help="bbhost's session/account port (bbhost's default 18671; 0 = only --port)")
    parser.add_argument("--bbhost-stun-port", type=int, default=3478,
                        help="bbhost STUN + relay, UDP (0 = off)")
    parser.add_argument("--bbhost-udp-rcvbuf", type=int, default=0,
                        help="requested UDP SO_RCVBUF bytes; 0 leaves OS default (safe default)")
    parser.add_argument("--bbhost-udp-stats-interval", type=int, default=0,
                        help="periodic UDP traffic counters, seconds; 0 disables logging")
    parser.add_argument("--bbhost-stun-bind", default="",
                        help="address the STUN socket binds to (default: --host)")
    parser.add_argument("--bbhost-public-ip", default="",
                        help="the IPv4 address players reach this server at (default: --advertise)")
    parser.add_argument("--bbhost-relay-ports", default="24000-24999",
                        help="relay port numbers handed out (virtual, nothing is bound)")
    parser.add_argument("--bbhost-heartbeat-timeout", type=float, default=30.0)
    parser.add_argument("--bbhost-open", action="store_true",
                        help="no bbhost accounts needed (players named by their own OnlineId; testing)")
    parser.add_argument("--log-dir", default=os.path.join(HERE, "logs"),
                        help="where gameserver-*.log goes (START-GAME-SERVER.cmd uses server\\logs)")
    # next to BBSC-GameServer.exe when frozen (HERE is then a temp folder emptied on exit)
    data_home = os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else HERE
    parser.add_argument("--data", default=os.path.join(data_home, "online-data.json"),
                        help="where shared messages and bloodstains are kept between runs")
    ARGS = parser.parse_args()
    if ARGS.tomb_max_response_bytes and not 4096 <= ARGS.tomb_max_response_bytes <= 2*1024*1024:
        parser.error("--tomb-max-response-bytes must be 0 or 4096..2097152")
    if ARGS.bbhost_udp_stats_interval and not 5 <= ARGS.bbhost_udp_stats_interval <= 3600:
        parser.error("--bbhost-udp-stats-interval must be 0 or 5..3600")
    if ARGS.bbhost_udp_rcvbuf and not 8192 <= ARGS.bbhost_udp_rcvbuf <= 64*1024*1024:
        parser.error("--bbhost-udp-rcvbuf must be 0 or 8192..67108864")
    if ARGS.tomb_max_response_bytes is not None:
        AREA_REPLY_BUDGET = ARGS.tomb_max_response_bytes

    log_path = open_log(ARGS.log_dir)
    print("BBSC game server - {} - log: {}".format(
        datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), log_path))
    disable_quick_edit()

    global STORE
    STORE = OnlineStore(ARGS.data)
    print("messages and bloodstains: {} ({} messages, {} bloodstains)".format(
        ARGS.data, len(STORE.data["messages"]), len(STORE.data["tombs"])))

    if not os.path.exists(TEMPLATE):
        print("missing {}".format(TEMPLATE))
        return 1

    print("building ss.info (other player):")
    SS_INFO_B64 = build_ss_info()
    print("  {} bytes of base64".format(len(SS_INFO_B64)))
    print("building ss.info (this PC):")
    SS_INFO_LOCAL_B64 = build_ss_info(local=True)
    print("  {} bytes of base64\n".format(len(SS_INFO_LOCAL_B64)))
    if ARGS.bbhost:
        SS_INFO_BBHOST_B64 = _bbhost_ss_info(build_ss_info(quiet=True, bbhost=True), local=False)
        SS_INFO_BBHOST_LOCAL_B64 = _bbhost_ss_info(build_ss_info(local=True, quiet=True, bbhost=True),
                                                   local=True)
        try:
            start_bbhost()
        except OSError as e:
            print("bbhost: STUN/relay port {} unavailable ({}) - bbhost players cannot play".format(
                ARGS.bbhost_stun_port, e))

    if ARGS.relay_port:
        start_relay(ARGS.host, ARGS.relay_port)
    server = QuietHTTPServer((ARGS.host, ARGS.port), Handler)
    print("BBSC game server on http://{}:{}".format(ARGS.host, ARGS.port))
    if ARGS.bbhost and ARGS.bbhost_port and ARGS.bbhost_port != ARGS.port:
        try:
            np_server = QuietHTTPServer((ARGS.host, ARGS.bbhost_port), Handler)
            threading.Thread(target=np_server.serve_forever, daemon=True).start()
            print("bbhost sessions and accounts on http://{}:{}".format(ARGS.host, ARGS.bbhost_port))
        except OSError as e:  # bbhost players only; everyone else must not lose the server over it
            print("bbhost: port {} unavailable ({}) - bbhost players cannot connect".format(
                ARGS.bbhost_port, e))
    print("Leave this window open while playing. Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    return 0

if __name__ == "__main__":
    sys.exit(main())
