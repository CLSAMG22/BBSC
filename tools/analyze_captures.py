"""Summarize BBSC baseline-test captures.

Reads shadp2p Bloodborne RE traces (captures/bloodborne-re/*.jsonl) from one or more players and,
optionally, shadNet server logs, then prints a merged timeline of session-relevant events and a
per-session-end summary naming the kick path.

Usage:
    python analyze_captures.py <capture dir or .jsonl> [...] [--label NAME=PATH ...]
                               [--server-log FILE] [--since "2026-09-15 20:00"] [--all]

Targets CUSA03173 01.09 (sha256 d65f0b4f...). Read-only.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# Functions that call CSMultiPlayMan::Stop (0x01ED07A0), from static analysis of the user eboot.
# (start, size, label, class) where class is structural | genuine | join | unknown.
STOP_CALLERS = [
    (0x013160F0, 144, "OnLanCutError (b)", "genuine"),
    (0x01332F60, 144, "session helper 0x01332F60", "unknown"),
    (0x013808B0, 320, "SprjSessionManager::OnMatchingCheck", "structural?"),
    (0x013809F0, 176, "SprjSessionManager::OnMatchingError", "genuine"),
    (0x013849A0, 2768, "ForceSummonSuccess", "join"),
    (0x013858A0, 144, "session helper 0x013858A0", "unknown"),
    (0x013859F0, 224, "session helper 0x013859F0", "unknown"),
    (0x01388510, 336, "OnLeave_Limit (a)", "unknown"),
    (0x01388680, 0x013893E0 - 0x01388680, "SummonReloadStart handler", "join"),
    (0x01389440, 480, "SummonReloadStart dispatch (event 4059)", "join"),
    (0x01389620, 496, "ForceSummonReloadStart", "join"),
    (0x01389D20, 352, "OnReviveMagic_1", "structural"),
    (0x01389FB0, 528, "OnLeave_Limit (b)", "unknown"),
    (0x0138B2D0, 528, "BlockClearAnim / Failed_BossAreaMission_LeaveMap", "structural"),
    (0x013CBAD0, 768, "character event 0x013CBAD0 (Idle_wild)", "unknown"),
    (0x013CDDA0, 144, "stage transition helper (unconditional)", "structural"),
    (0x013CDE30, 256, "StageTransition (skipped if state_1592)", "structural"),
    (0x015BEC90, 128, "Stop+cleanup helper 0x015BEC90", "unknown"),
    (0x01789190, 112, "session helper 0x01789190", "unknown"),
    (0x017894E0, 1136, "OnIrregularLeaveSession (a)", "genuine"),
    (0x01789F9E, 1, "network events: host ends session after partner left (live 2026-09-15)", "reaction"),
    (0x01789B60, 3456, "network events (OnRoomTimeOut / OnIrregularLeaveSession / OnBeThxKickOut)", "mixed"),
    (0x0178B5E0, 7936, "join/create/kick events (OnFailedJoinSession / OnBeKickOut / OnJoinClearedRoom)", "mixed"),
    (0x0178F650, 112, "session helper 0x0178F650", "unknown"),
    (0x0178F9A0, 224, "OnIrregularLeaveSession (b)", "genuine"),
    (0x01872360, 7168, "SosStatus.Update", "unknown"),
    (0x01939470, 1280, "stage flow 0x01939470", "structural"),
    (0x0193AC10, 28688, "stage update / stage-UID policy", "structural"),
    (0x019470F0, 400, "stage task destructor (fork retargets 0x019471B1)", "structural"),
    (0x01E57210, 1616, "OnLanCutError (a)", "genuine"),
]

MULTI_PLAY_STATES = {
    0: "idle", 1: "host-creating", 2: "host-create-failed", 3: "host-established",
    4: "guest-joining", 5: "guest-join-failed", 6: "guest-established", 7: "leaving", 8: "state8",
}

KEY_SITES = {
    "CSMultiPlayMan.StopRequest": "STOP",
    "WorldChrMan.RemoteCleanup": "CLEANUP",
    "BBSC.Persist.SkippedCleanup": "PERSIST-KEPT",
    "Matching2.StopTask.Enqueue": "STOP-TASK",
    "Matching2.LeaveRoom.Begin": "LEAVE-BEGIN",
    "CSMultiPlayMan.StageUidChanged.Policy": "STAGE-UID",
    "OnMatchingCheck": "MATCH-CHECK",
    "OnMatchingError": "MATCH-ERROR",
    "Warp.StageTransition": "STAGE-TRANSITION",
    "Warp.RespawnPointParam": "WARP-RESPAWN",
    "Event.WarpNextStage": "WARP-EVENT",
    "CSMultiPlayerIns.Init": "INSERT-INIT",
    "CSMultiPlayerIns.Create": "INSERT-CREATE",
    "CSMultiPlayerIns.Delete": "INSERT-DELETE",
    "SummonedMapReload.Native": "SUMMON-RELOAD",
    "ChrAction.UseItem.NativeApply": "USE-ITEM",
}
# Sites that fire continuously; only shown with --all.
NOISY = {"MATCH-CHECK", "INSERT-INIT", "INSERT-CREATE", "INSERT-DELETE", "USE-ITEM"}

GOODS = {200: "Beckoning Bell", 205: "Small Resonant Bell", 225: "Sinister Resonant Bell",
         111: "Silencing Blank"}


@dataclass(order=True)
class Event:
    time_ms: int
    player: str = field(compare=False)
    kind: str = field(compare=False)
    detail: str = field(compare=False)
    record: dict = field(compare=False, default_factory=dict)


def caller_label(offset: int) -> tuple[str, str]:
    for start, size, label, cls in STOP_CALLERS:
        if start <= offset < start + size:
            return label, cls
    return f"unmapped 0x{offset:08X}", "unknown"


def as_int(value) -> int | None:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 16) if value.lower().startswith("0x") else int(value)
        except ValueError:
            return None
    return None


def find_key(obj, key):
    """Depth-first search for a key anywhere in a nested record."""
    if isinstance(obj, dict):
        if key in obj:
            return obj[key]
        for value in obj.values():
            found = find_key(value, key)
            if found is not None:
                return found
    elif isinstance(obj, list):
        for value in obj:
            found = find_key(value, key)
            if found is not None:
                return found
    return None


def fmt_time(ms: int) -> str:
    return dt.datetime.fromtimestamp(ms / 1000).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def describe(kind: str, rec: dict) -> str:
    parts = []
    caller = as_int(find_key(rec, "caller_offset"))
    if kind in ("STOP", "CLEANUP", "PERSIST-KEPT", "STOP-TASK", "LEAVE-BEGIN", "STAGE-UID") and caller:
        label, cls = caller_label(caller)
        parts.append(f"caller={label} [{cls}] @0x{caller:08X}")
    state = find_key(rec, "multi_play_state")
    if isinstance(state, int):
        parts.append(f"session={MULTI_PLAY_STATES.get(state, state)}")
    for key in ("state_1592", "reload_phase", "stop_reason", "room_id", "current_map", "map_id",
                "warp_info_id"):
        value = find_key(rec, key)
        if value is not None and value != -1:
            parts.append(f"{key}={value}")
    if kind == "USE-ITEM":
        goods = as_int(find_key(rec, "rsi"))
        if goods is not None:
            parts.append(f"goods={goods} {GOODS.get(goods, '')}".strip())
    return " ".join(parts)


def load_trace(path: Path, player: str, events: list[Event]) -> None:
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            time_ms = rec.get("time_ms")
            if not isinstance(time_ms, int):
                continue
            if rec.get("type") == "matching2_leave_room":
                events.append(Event(time_ms, player, "HLE-LEAVE", describe("LEAVE-BEGIN", rec), rec))
                continue
            kind = KEY_SITES.get(rec.get("site", ""))
            if kind:
                events.append(Event(time_ms, player, kind, describe(kind, rec), rec))


SERVER_LINE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s+\w+\s+(.*)$")
SERVER_KEEP = re.compile(r"(?i)bloodborne summon|room \d+|logged in|login|disconnect|kick")


def load_server_log(path: Path, events: list[Event]) -> None:
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = SERVER_LINE.match(line.rstrip())
            if not match or not SERVER_KEEP.search(match.group(2)):
                continue
            stamp = dt.datetime.strptime(match.group(1), "%Y-%m-%d %H:%M:%S.%f")
            events.append(Event(int(stamp.timestamp() * 1000), "server", "SERVER", match.group(2)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", help="capture directories or .jsonl files")
    parser.add_argument("--label", action="append", default=[], help="NAME=PATH (name a player's captures)")
    parser.add_argument("--server-log", action="append", default=[], help="shadNet log file")
    parser.add_argument("--since", help='ignore events before "YYYY-MM-DD HH:MM"')
    parser.add_argument("--all", action="store_true", help="include high-frequency sites")
    args = parser.parse_args()

    sources: list[tuple[str, Path]] = [(f"P{i + 1}", Path(p)) for i, p in enumerate(args.paths)]
    for item in args.label:
        name, _, path = item.partition("=")
        sources.append((name, Path(path)))
    if not sources and not args.server_log:
        parser.print_help()
        return 1

    events: list[Event] = []
    for name, path in sources:
        files = sorted(path.glob("*.jsonl")) if path.is_dir() else [path]
        if not files:
            print(f"warning: no .jsonl files in {path}", file=sys.stderr)
        for file in files:
            load_trace(file, f"{name}:{file.stem.split('-')[2] if file.stem.count('-') >= 3 else file.stem}", events)
    for log in args.server_log:
        load_server_log(Path(log), events)

    if args.since:
        cutoff = int(dt.datetime.strptime(args.since, "%Y-%m-%d %H:%M").timestamp() * 1000)
        events = [e for e in events if e.time_ms >= cutoff]
    events.sort()

    print("=" * 100)
    print("TIMELINE")
    print("=" * 100)
    last_warp: dict[str, Event] = {}
    ends: list[tuple[Event, Event | None]] = []
    for event in events:
        if event.kind in ("STAGE-TRANSITION", "WARP-RESPAWN", "WARP-EVENT"):
            last_warp[event.player] = event
        if event.kind == "STOP":
            ends.append((event, last_warp.get(event.player)))
        if event.kind in NOISY and not args.all:
            continue
        print(f"{fmt_time(event.time_ms)}  {event.player:<14} {event.kind:<17} {event.detail}")

    kept = [e for e in events if e.kind == "PERSIST-KEPT"]
    print()
    print("=" * 100)
    print(f"PHASE 1 PERSISTENCE: {len(kept)} session end(s) kept alive")
    print("=" * 100)
    for event in kept:
        caller = as_int(find_key(event.record, "caller_offset")) or 0
        label, cls = caller_label(caller)
        state = find_key(event.record, "multi_play_state")
        print(f"{fmt_time(event.time_ms)}  {event.player:<14} {label} [{cls}]  "
              f"session={MULTI_PLAY_STATES.get(state, state)}")

    print()
    print("=" * 100)
    print("SESSION STOP REQUESTS (what asked the game to end co-op)")
    print("=" * 100)
    if not ends:
        print("none recorded")
    for stop, warp in ends:
        caller = as_int(find_key(stop.record, "caller_offset")) or 0
        label, cls = caller_label(caller)
        state = find_key(stop.record, "multi_play_state")
        acted = isinstance(state, int) and state not in (0, 2, 5)
        print(f"{fmt_time(stop.time_ms)}  {stop.player}")
        print(f"    caller   : {label}  [{cls}]  0x{caller:08X}")
        print(f"    session  : {MULTI_PLAY_STATES.get(state, state)}  -> {'ENDS SESSION' if acted else 'no-op (idle/failed state)'}")
        if warp is not None:
            gap = (stop.time_ms - warp.time_ms) / 1000
            print(f"    last warp: {warp.kind} {gap:.1f}s earlier  state_1592={find_key(warp.record, 'state_1592')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
