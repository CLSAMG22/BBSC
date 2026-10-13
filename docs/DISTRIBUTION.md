# Distribution plan — deferred until open beta

Parked 2026-09-22. **Not to be started before co-op is stable for two players.** Right now a guest
who dies twice can still end up a corpse; making the thing easy to install for two hundred people
before it works for two is the wrong order.

The decision when we get there: **self-host or use the public one**, the same shape as the Online
Expedition server for Clair Obscur: Expedition 33 (`OnlineExpedition-Server-0.12.0`), which is the
model worth copying.

## Where we are against that model

| | Online Expedition | BBSC today |
| --- | --- | --- |
| Install | one binary, no dependencies | `shadnet.exe` + 6 Qt DLLs + vc_redist + **Python** |
| Start | double-click | two windows, one needs Python on PATH |
| Ports | 1 (7777 TCP), opened by UPnP | 4 — 31313 TCP, 31314 UDP, 31315 TCP, 20443 TCP, all manual |
| Reachable? | server checks and prints your address | you find out when a friend fails to connect |
| Joining | type address + password | email-verified account signup against the host's SMTP |
| Config | one commented `relay.toml` | `shadnet.cfg` + `bbsc-accounts.cfg` + `host_overrides.json` |
| No public IP | playit.gg tunnel | Tailscale invite to the host's tailnet |

## The work, in the order it pays off

1. **Kill the account system.** The biggest win by a distance. Every player currently needs an
   email-verified signup against the *host's* SMTP configuration - fine for three friends, fatal
   for a public mod. Replace with: any login succeeds when it matches the server password, and the
   account row is created on the spot. Contained change inside shadnet.
2. **Remove the Python dependency.** PyInstaller `bbsc_gameserver.py` into a ~10 MB exe. Deletes
   the most likely support question before anyone asks it.
3. **One folder, one double-click.** shadnet + DLLs + the game server exe + a single `START.cmd`.
4. **UPnP and a reachability check.** shadnet has no server-side UPnP, but `net_upnp.cpp` already
   exists on the client side, so the code to port is in the tree. The reachability check needs a
   small endpoint - the public instance can serve it.
5. **Fewer ports.** Four is a lot to ask of someone's router. The game server and shadnet's web API
   are both plain HTTP, so folding 20443 into 31315 gets it to three.
6. **Public instance** on a VPS as the default for people who do not want to host.
7. **Publish the source.** Push the shadp2p fork (and shadnet, once item 1 changes it) to GitHub
   before the first public build. shadPS4 is GPL-2.0: anyone who gets the binary is entitled to
   the source.
8. **Donation pages - only after open beta 1 is out, and only after item 7.** Text is ready in
   `DONATION-PAGES.md`. Nothing paywalled, no early access, ever.

## Worth stealing

Their telemetry design: anonymous, opt-out, explained plainly in the README, and it gives the
*user* something back (a reachability answer and an update notice). That is how to ship telemetry
without anyone objecting to it.

Their README generally - it explains the failure cases (UPnP off, CGNAT, antivirus flagging an
unsigned exe) before they happen, in the order a person hits them.

## Hosting

Costed 2026-09-22. The server needs about 50 MB of RAM, so hardware is not the constraint and the
cheapest tier of anything works. **RackNerd** takes PayPal and crypto (1 GB KVM at $21.99/year as
listed 2026-09-23, `my.racknerd.com/cart.php?a=add&pid=952`; the ~$11/year price was a New Year
sale), which matters
because the blocker on this project has never been money - it is that a prepaid card got rejected.
~99.9% uptime by community consensus; oversold, which is irrelevant to a workload this small.

Scaling is a code question, not a hardware one: `SummonBroker::Search` scans every active sign
under a single global mutex, so it is O(N^2) single-threaded and extra cores cannot help. The wall
is somewhere near 1,500 concurrent players. The free fix first - the summon poll interval is set to
5 s against retail's 35 s, so putting it back to 20 s is a 4x capacity increase - then index the
broker instead of scanning. Neither is needed until an order of magnitude past where we are.

`server/oracle/setup-server.sh` already handles most of a fresh Ubuntu host. It predates the game
server, so it needs a third systemd unit for `bbsc_gameserver.py` and TCP 20443 opened.
