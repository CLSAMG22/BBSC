"""The Hunter's Dream's messages in co-op (optional, off by default).

The co-op server only knows the messages its own players wrote, so the world is nearly empty. With
this on, the messages The Hunter's Dream (thehuntersdream.com) has for the same areas are added to
the game's message list. With "write" on as well, the messages the host writes are posted there too
(and removed there when the host deletes them). Messages written by guests stay on the co-op server
only: posting them would put them under the host's account. Both need the host's sign-in.

It uses the host's own The Hunter's Dream account: BBSC-GameServer.exe --thd-sign-in signs in with
Discord (the device code flow bbhost and the bloodborne_pc launcher use) and saves the account and
token in thd-account.json; the token the bloodborne_pc launcher keeps works too. The server then
signs in the way the game does in that online mode (/basic_utils/login with the X-BB-Token and
X-BBHost-Ruleset headers). Answers are kept per region for CACHE_SECONDS and
fetched in the background, so the game never waits on The Hunter's Dream: the first list for a
region has only our own messages, the next one has theirs too.

Settings, first found wins:
  enabled  --thd-messages, BBSC_THD_MESSAGES=1, or "enabled": true in thd-messages.json
  write    BBSC_THD_WRITE=1 or "write": true in thd-messages.json (default: read only)
  token    BBSC_THD_TOKEN, thd-account.json (--thd-sign-in writes it), "token" in
           thd-messages.json, or the bloodborne_pc launcher's settings.json
  account  BBSC_THD_ACCOUNT, thd-account.json, "account" in thd-messages.json, or the launcher's
  ruleset  BBSC_THD_RULESET, "ruleset" in thd-messages.json, else "vanilla"
  url      BBSC_THD_URL, "url" in thd-messages.json, else https://thehuntersdream.com:18671
thd-messages.json and thd-account.json sit next to online-data.json. The sign-in has its own file so
turning the setting on and off never loses it.
"""
import json
import os
import queue
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

DEFAULT_URL = "https://thehuntersdream.com:18671"
DEFAULT_AUTH = "https://thehuntersdream.com"   # its account service (/auth/device/...)
CACHE_SECONDS = 120
RETRY_SECONDS = 60      # after a failed fetch for a region
TIMEOUT_SECONDS = 10
CONFIG_NAME = "thd-messages.json"
ACCOUNT_NAME = "thd-account.json"          # {"account", "token"} from --thd-sign-in
POSTED_NAME = "thd-messages-posted.json"   # our message id -> its id on The Hunter's Dream
POST_TRIES = 3
POST_RETRY_SECONDS = 60
LAUNCHER_SETTINGS = os.path.expandvars(r"%APPDATA%\bbport-launcher\settings.json")


def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def _their_ids(reply):
    """The message ids in a create reply: plain numbers, or {"BloodMessId": n} entries."""
    raw = reply.get("BloodMessIdList")
    if not isinstance(raw, list):
        raw = [reply.get("BloodMessId")]
    ids = []
    for item in raw:
        if isinstance(item, dict):
            item = item.get("BloodMessId")
        if isinstance(item, str) and item.isdigit():
            item = int(item)
        if isinstance(item, int) and not isinstance(item, bool) and item:
            ids.append(item)
    return ids


class HuntersDreamMessages:
    def __init__(self, enabled, token, account, ruleset, url, write=False, data_dir=None):
        self.enabled = enabled and bool(token)
        self.write = self.enabled and write
        self.posted_path = os.path.join(data_dir, POSTED_NAME) if data_dir else None
        self.posted = {}      # str(our id) -> their id, for deletes and to leave our own out
        if self.posted_path:
            self.posted = {str(k): v for k, v in _read_json(self.posted_path).items()}
        self.own_ids = set(self.posted.values())
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
        config_path = os.path.join(data_dir, CONFIG_NAME)
        config = _read_json(config_path)
        try:
            with open(config_path, "rb"):
                unreadable = False
        except FileNotFoundError:
            unreadable = False
        except OSError:
            unreadable = True
        if unreadable:
            # 2026-10-19: written by a script run as administrator, the file belonged to the
            # Administrators group and the server (run as the user) could not read it.
            print("THD messages: {} exists but could not be read (written as administrator?) - "
                  "the setting in it is ignored".format(config_path))
        enabled = flag or os.environ.get("BBSC_THD_MESSAGES") == "1" or config.get("enabled") is True
        launcher = _read_json(LAUNCHER_SETTINGS)
        signed_in = _read_json(os.path.join(data_dir, ACCOUNT_NAME))
        token = (os.environ.get("BBSC_THD_TOKEN") or signed_in.get("token") or config.get("token")
                 or launcher.get("thd_token") or "")
        account = (os.environ.get("BBSC_THD_ACCOUNT") or signed_in.get("account")
                   or config.get("account") or launcher.get("thd_online_id") or "")
        ruleset = os.environ.get("BBSC_THD_RULESET") or config.get("ruleset") or "vanilla"
        url = os.environ.get("BBSC_THD_URL") or config.get("url") or DEFAULT_URL
        write = os.environ.get("BBSC_THD_WRITE") == "1" or config.get("write") is True
        found = cls(enabled, token, account, ruleset, url, write, data_dir)
        if enabled and not token:
            print("THD messages: on, but no The Hunter's Dream sign-in found (run "
                  "BBSC-GameServer.exe --thd-sign-in, or sign in on the bloodborne_pc launcher's "
                  "Online page) - off")
        elif enabled:
            print("THD messages: on - The Hunter's Dream's messages are added to ours{} (account "
                  "{}, ruleset {}, {})".format(", the host's are posted there" if found.write
                                               else " (read only)", account or "?", ruleset,
                                               found.url))
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
            entries = [e for e in entries if e["BloodMessId"] not in self.own_ids]
        return entries[:limit]

    def post(self, body, our_ids):
        """The host wrote messages (our ids, in the order of body's BloodMessList): post them."""
        if self.write and our_ids:
            self.jobs.put(("create", body, list(our_ids), 1))

    def remove(self, our_ids):
        """Messages deleted here: delete the ones we had posted."""
        if self.write and any(str(i) in self.posted for i in our_ids):
            self.jobs.put(("delete", list(our_ids), 1))

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

    def _call(self, path, body):
        """A call with our session; signs in again once if the session has ended."""
        if self.session is None:
            self._login()
        for attempt in (0, 1):
            reply = self._post(path, dict(body, SessionId=self.session[0], UserId=self.session[1]))
            if reply.get("ResKind") == 0:
                return reply
            if attempt == 0:
                self._login()
        raise RuntimeError("{} refused (ResKind {})".format(path, reply.get("ResKind")))

    def _save_posted(self):
        if not self.posted_path:
            return
        temp = self.posted_path + ".tmp"
        with open(temp, "w", encoding="utf-8") as handle:
            json.dump(self.posted, handle)
        os.replace(temp, self.posted_path)

    def _write_job(self, job):
        if job[0] == "create":
            _, body, our_ids, _tries = job
            reply = self._call("/blood_messenger/create", body)
            # From here on the message is posted: nothing below may fail the job, or the retry
            # would post it again (test 2026-10-20: their reply's ids were not plain numbers, the
            # bookkeeping raised, and every retry posted another copy).
            try:
                theirs = _their_ids(reply)
                with self.lock:
                    for ours, their in zip(our_ids, theirs):
                        self.posted[str(ours)] = their
                        self.own_ids.add(their)
                self._save_posted()
            except Exception as error:  # noqa: BLE001 - see above
                theirs = []
                print("      THD messages: posted, but not recorded ({}: {})".format(
                    type(error).__name__, error), flush=True)
            if theirs:
                print("      THD messages: posted {} there as {}".format(our_ids, theirs),
                      flush=True)
            else:
                print("      THD messages: posted {} there, but their id is not in the reply, so "
                      "deleting it here won't delete it there: {}".format(
                          our_ids, json.dumps(reply)[:300]), flush=True)
        else:
            _, our_ids, _tries = job
            with self.lock:
                theirs = [self.posted[str(i)] for i in our_ids if str(i) in self.posted]
            if theirs:
                self._call("/blood_messenger/delete", {"MessageId": "BloodMessRemoveRequest",
                                                       "BloodMessIdList": theirs})
            with self.lock:
                for i in our_ids:
                    self.posted.pop(str(i), None)
            self._save_posted()
            print("      THD messages: removed {} there".format(theirs), flush=True)

    def _retry_later(self, job):
        if job[-1] >= POST_TRIES:
            print("      THD messages: gave up on {} {} after {} tries".format(
                job[0], job[2] if job[0] == "create" else job[1], job[-1]), flush=True)
            return
        again = job[:-1] + (job[-1] + 1,)
        timer = threading.Timer(POST_RETRY_SECONDS, self.jobs.put, args=(again,))
        timer.daemon = True
        timer.start()

    def _worker(self):
        # Any failure only costs this one job: a region keeps what it had (or nothing) and is
        # asked again RETRY_SECONDS later, so the messages come back once The Hunter's Dream does;
        # a post or delete is tried again POST_RETRY_SECONDS later, POST_TRIES times in all.
        # Nothing here may end the thread.
        while True:
            region = self.jobs.get()
            if isinstance(region[0], str):  # ("create" | "delete", ...)
                try:
                    self._write_job(region)
                except Exception as error:  # noqa: BLE001 - passive: never let it stop the worker
                    self.session = None
                    print("      THD messages: could not {} on The Hunter's Dream ({}: {}) - "
                          "trying again later".format(region[0], type(error).__name__, error),
                          flush=True)
                    self._retry_later(region)
                continue
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
        # Exactly the fields The Hunter's Dream sends, as it sends them: that is what the game
        # shows in their online mode. (Our own entries' AreaId / CharaData: true were added here
        # at first, and the game showed none of these messages.)
        return {key: entry[key] for key in ("BloodMessId", "BloodData", "BloodDataVersion",
                                            "EvaluatePlus", "EvaluateMinus", "MessShellInfo",
                                            "MessShellInfoVersion") if key in entry}


# ---- signing in (BBSC-GameServer.exe --thd-sign-in) ------------------------------------------------

def _auth_call(url, body):
    request = urllib.request.Request(url, data=json.dumps(body).encode("utf-8"), method="POST",
                                     headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
            text = response.read()
    except urllib.error.HTTPError as error:
        text = error.read()
    reply = json.loads(text or b"{}")
    if not isinstance(reply, dict) or reply.get("ResKind", 0) != 0:
        raise RuntimeError("The Hunter's Dream refused: {}".format(
            reply.get("Message") or reply.get("ResKind") if isinstance(reply, dict) else reply))
    return reply


def sign_in(data_dir, open_page=True):
    """Discord sign-in with a code to approve on The Hunter's Dream's website; the account and token
    go into thd-account.json. Returns 0 on success."""
    auth = (os.environ.get("BBSC_THD_AUTH") or DEFAULT_AUTH).rstrip("/")
    try:
        start = _auth_call(auth + "/auth/device/start",
                           {"DeviceName": "BBSC co-op server on {}".format(
                               os.environ.get("COMPUTERNAME", "PC"))})
        device, code = start.get("DeviceCode", ""), start.get("UserCode", "")
        if not device or not code:
            raise RuntimeError("no code in the reply")
        interval = max(1, int(start.get("Interval") or 5))
        deadline = time.monotonic() + int(start.get("ExpiresIn") or 600)
        page = start.get("VerificationUri") or auth + "/link"
        page += ("&" if "?" in page else "?") + "code=" + urllib.parse.quote(code)
        print("\n  Sign in to The Hunter's Dream with Discord and approve this code:\n\n"
              "      {}\n\n  on {}\n  (not in its \"code from the game\" box). Waiting...".format(
                  code, page), flush=True)
        if open_page:
            webbrowser.open(page)
        while time.monotonic() < deadline:
            time.sleep(interval)
            poll = _auth_call(auth + "/auth/device/poll", {"DeviceCode": device})
            status = poll.get("Status")
            if status == "approved":
                if not poll.get("OnlineId") or not poll.get("Token"):
                    raise RuntimeError("no token in the reply")
                path = os.path.join(data_dir, ACCOUNT_NAME)
                temp = path + ".tmp"
                with open(temp, "w", encoding="utf-8") as handle:
                    json.dump({"account": poll["OnlineId"], "token": poll["Token"]}, handle)
                os.replace(temp, path)
                print("\n  Signed in as {}. Saved in {}.".format(poll["OnlineId"], path), flush=True)
                if not _read_json(os.path.join(data_dir, CONFIG_NAME)).get("enabled"):
                    print("  The messages are still off: turn them on to use them.", flush=True)
                return 0
            if status == "slow_down":
                interval += 5
            elif status == "denied":
                raise RuntimeError("the sign-in was denied")
            elif status == "expired":
                break
        raise RuntimeError("the code expired; try again")
    except Exception as error:  # noqa: BLE001 - a console tool: say what went wrong
        print("\n  Sign-in failed: {}".format(error), flush=True)
        return 1
