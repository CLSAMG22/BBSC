"""The Hunter's Dream's messages in co-op (optional, off by default).

The co-op server only knows the messages its own players wrote, so the world is nearly empty. With
this on, the messages The Hunter's Dream (thehuntersdream.com) has for the same areas are added to
the game's message list. Read only: nothing is written, rated or deleted there.

It uses the player's own The Hunter's Dream sign-in, the token the bloodborne_pc launcher keeps after
"Sign in with Discord", and signs in the way the game does in that online mode (/basic_utils/login
with the X-BB-Token and X-BBHost-Ruleset headers). Answers are kept per region for CACHE_SECONDS and
fetched in the background, so the game never waits on The Hunter's Dream: the first list for a
region has only our own messages, the next one has theirs too.

Settings, first found wins:
  enabled  --thd-messages, BBSC_THD_MESSAGES=1, or "enabled": true in thd-messages.json
  token    BBSC_THD_TOKEN, "token" in thd-messages.json, or the launcher's settings.json
  ruleset  BBSC_THD_RULESET, "ruleset" in thd-messages.json, else "vanilla"
  url      BBSC_THD_URL, "url" in thd-messages.json, else https://thehuntersdream.com:18671
thd-messages.json sits next to online-data.json.
"""
import json
import os
import queue
import threading
import time
import urllib.error
import urllib.request

DEFAULT_URL = "https://thehuntersdream.com:18671"
CACHE_SECONDS = 120
RETRY_SECONDS = 60      # after a failed fetch for a region
TIMEOUT_SECONDS = 10
CONFIG_NAME = "thd-messages.json"
LAUNCHER_SETTINGS = os.path.expandvars(r"%APPDATA%\bbport-launcher\settings.json")


def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


class HuntersDreamMessages:
    def __init__(self, enabled, token, account, ruleset, url):
        self.enabled = enabled and bool(token)
        self.token = token
        self.account = account
        self.ruleset = ruleset
        self.url = url.rstrip("/")
        self.session = None   # (SessionId, UserId) from /basic_utils/login
        self.cache = {}       # (area, region, channel) -> (fetched at, entries)
        self.lock = threading.Lock()
        self.pending = set()
        self.jobs = queue.Queue()
        self.last_error = 0.0
        if self.enabled:
            threading.Thread(target=self._worker, daemon=True).start()

    @classmethod
    def from_settings(cls, flag, data_dir):
        config = _read_json(os.path.join(data_dir, CONFIG_NAME))
        enabled = flag or os.environ.get("BBSC_THD_MESSAGES") == "1" or config.get("enabled") is True
        launcher = _read_json(LAUNCHER_SETTINGS)
        token = (os.environ.get("BBSC_THD_TOKEN") or config.get("token")
                 or launcher.get("thd_token") or "")
        account = config.get("account") or launcher.get("thd_online_id") or ""
        ruleset = os.environ.get("BBSC_THD_RULESET") or config.get("ruleset") or "vanilla"
        url = os.environ.get("BBSC_THD_URL") or config.get("url") or DEFAULT_URL
        found = cls(enabled, token, account, ruleset, url)
        if enabled and not token:
            print("THD messages: on, but no The Hunter's Dream sign-in found (sign in on the "
                  "launcher's Online page) - off")
        elif enabled:
            print("THD messages: on - The Hunter's Dream's messages are added to ours (account {}, "
                  "ruleset {}, {})".format(account or "?", ruleset, found.url))
        return found

    # ---- what the game asks ----------------------------------------------------------------------

    def entries_for(self, region, limit):
        """Cached entries for one region (possibly none yet); a refresh is queued when needed."""
        if not self.enabled or limit <= 0:
            return []
        now = time.time()
        with self.lock:
            fetched, entries = self.cache.get(region, (0.0, []))
            if now - fetched > CACHE_SECONDS and region not in self.pending:
                self.pending.add(region)
                self.jobs.put(region)
        return entries[:limit]

    # ---- talking to The Hunter's Dream -----------------------------------------------------------

    def _post(self, path, body):
        request = urllib.request.Request(
            self.url + path, data=json.dumps(body).encode("utf-8"), method="POST",
            headers={"Content-Type": "application/json", "Accept": "application/json",
                     "X-BB-Token": self.token, "X-BBHost-Ruleset": self.ruleset})
        with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
            data = json.loads(response.read() or b"{}")
        return data if isinstance(data, dict) else {}

    def _login(self):
        reply = self._post("/basic_utils/login", {"MessageId": "LoginRequest",
                                                  "PlatformAccountId": self.account})
        if reply.get("ResKind") != 0 or not reply.get("SessionId"):
            raise RuntimeError("sign-in refused (ResKind {})".format(reply.get("ResKind")))
        self.session = (reply["SessionId"], reply.get("UserId", 0))

    def _fetch(self, region):
        if self.session is None:
            self._login()
        for attempt in (0, 1):
            session_id, user_id = self.session
            reply = self._post("/blood_messenger/message_area", {
                "AreaInfoList": [{"AreaId": region[0], "AreaRegionId": region[1],
                                  "ChannelId": region[2], "GetCount": 30}],
                "BloodDataVersion": 5, "CharaDataVersion": 1, "GetMaxCount": 30,
                "MessShellInfoVersion": 2, "MessageId": "BloodMessGetListRequest",
                "SessionId": session_id, "UserId": user_id})
            if reply.get("ResKind") == 0:
                return reply.get("BloodMessList") or []
            if attempt == 0:
                self._login()  # the session may have ended (their server restarts now and then)
        raise RuntimeError("message list refused (ResKind {})".format(reply.get("ResKind")))

    def _worker(self):
        # Any failure only costs this one fetch: the region keeps what it had (or nothing) and is
        # asked again RETRY_SECONDS later, so the messages come back once The Hunter's Dream does.
        # Nothing here may end the thread.
        while True:
            region = self.jobs.get()
            entries = None
            try:
                entries = [self._entry(e, region) for e in self._fetch(region)
                           if isinstance(e, dict) and e.get("BloodData")]
                if self.last_error:
                    self.last_error = 0.0
                    print("      THD messages: The Hunter's Dream answers again", flush=True)
                print("      THD messages: {} for {}/{}".format(len(entries), region[0], region[1]),
                      flush=True)
            except Exception as error:  # noqa: BLE001 - passive: never let it stop the worker
                self.session = None
                if time.time() - self.last_error > 60:
                    self.last_error = time.time()
                    print("      THD messages: The Hunter's Dream did not answer ({}: {}) - only our "
                          "own messages for now, trying again later".format(
                              type(error).__name__, error), flush=True)
            finally:
                with self.lock:
                    fetched, old = self.cache.get(region, (0.0, []))
                    # a failed fetch keeps the last list but is retried sooner than a good one
                    when = time.time() if entries is not None else \
                        time.time() - CACHE_SECONDS + RETRY_SECONDS
                    self.cache[region] = (when, entries if entries is not None else old)
                    self.pending.discard(region)

    @staticmethod
    def _entry(entry, region):
        return {"BloodMessId": entry.get("BloodMessId", 0), "PrevBloodMessId": 0,
                "AreaId": region[0], "AreaRegionId": region[1], "ChannelId": region[2],
                "BloodData": entry["BloodData"],
                "BloodDataVersion": entry.get("BloodDataVersion", 0),
                "CharaData": entry.get("CharaData", True),
                "CharaDataVersion": entry.get("CharaDataVersion", 1),
                "EvaluatePlus": entry.get("EvaluatePlus", 0),
                "EvaluateMinus": entry.get("EvaluateMinus", 0),
                "MessShellInfo": entry.get("MessShellInfo", ""),
                "MessShellInfoVersion": entry.get("MessShellInfoVersion", 0)}
