# BBSC game server

What makes Bloodborne believe it is online, without depending on anybody else's machine.

> Need help with any of this, or with shadPS4 and Bloodborne on PC generally?
> Join the **Bloodborne PC** Discord — it is the main community for running this game on PC.

## Why this exists

Before Bloodborne enables a single online feature it fetches:

```
https://ss4.scej-network.jp:20443/bb-eu/ss.info
```

That file is **base64-encoded XML**, and it is the **server list**. A `<gameurl2>` block names one
URL per game API — login, notices, blood messages, ghosts, summoning, play logs — plus the timing
and matching parameters the online systems run on. Everything the game does afterwards goes
wherever that file says.

Private servers work by redirecting that one URL. Previously it pointed at `thehuntersdream.com`,
so their `ss.info` decided where *our* co-op went. On 2026-09-21 their backend stopped responding
(TCP connected, no HTTP reply, on both 18671 and 20443) and co-op stopped working for us with it.

Serving our own removes that dependency for good.

## What it does

| Endpoints | Where they go |
| --- | --- |
| the four `api_SummonData*` | the real shadnet server — this is where co-op actually happens |
| everything else | this process, which answers empty-but-valid JSON so the game stops asking |

Blood messages, ghosts, notices and channels are **not implemented**. They get well-formed empty
replies, which is enough to keep the game happy; this does not pretend to provide them.

Each player runs their own copy on `127.0.0.1`. Only the summon endpoints are shared, which is why
the shadnet address is the one thing you have to set.

## Running it

```
python bbsc_gameserver.py --shadnet 100.101.102.103
```

Then in `%APPDATA%\shadPS4\host_overrides.json`:

```json
{ "https://ss4.scej-network.jp:20443": "http://127.0.0.1:20443" }
```

Standard library only — no Flask — so a stock Python runs it. Leave the window open while playing;
it logs every request it answers, which is the fastest way to see whether the game is really
talking to us.

| Option | Effect |
| --- | --- |
| `--shadnet ADDR` | **required** — the machine running `shadnet.exe` |
| `--summon-interval N` | seconds between summon-sign checks (retail 35 / 30) |
| `--level-range N` | co-op level window (retail ±20) |
| `--host` / `--port` | where to listen (default `127.0.0.1:20443`) |
| `--advertise ADDR` | address to write into `ss.info` when it differs from `--host` |
| `--verbose` | log request headers and bodies |

Defaults change nothing except the addresses. Retuning is opt-in.

## Retail tuning values

Taken from the real `ss.info`, recorded here because they are not visible anywhere in the game and
several of them explain behaviour we spent a long time blaming on the emulator.

| Tag | Retail | What it controls |
| --- | --- | --- |
| `SummonDataGetListInterval2` | 35 | seconds between checks for a summon sign |
| `SummonDataCreateInterval2` | 30 | how often a host re-posts its sign |
| `SummonDataGetListGuardTimer2` | 60 | guard timer on the sign list |
| `SummonDataGetListGetMaxCount2` | 20 | most signs returned at once |
| `SummonDataCoopMatchingLevelUpperAbs2` | 20 | co-op level window, upper |
| `SummonDataCoopMatchingLevelLowerAbs2` | −20 | co-op level window, lower |
| `SummonDataBloodMadHunterPercent2` | 5 | blood-mad hunter chance |
| `ReloadServerStatusInfoInterval2` | 300 | how often the game re-reads `ss.info` |

**`SummonDataGetListInterval2` is what "quick summoning" means.** A sign can take over half a
minute to appear on retail settings, and no amount of work inside the game could have changed it —
the value lives in this file.

## Credit

The `ss.info` format, the endpoint list and the JSON response shapes come from
[NoDreamForHunter](https://github.com/fhody125-web) by **fhody125**, MIT licensed — see
`LICENSE.NoDreamForHunter`. He handed over the project and a real `ss.info` when thehuntersdream
went down, which is the only reason any of this was possible. `ss_info_template.xml` is his file
with the shadnet address left as a placeholder.

This script is a standalone reimplementation rather than a fork: no Flask dependency, and it can
retune the summon parameters on the way out.
