# Changelog

## 2026-10-04 (c) - mod 11: Silencing Blank ends the co-op cleanly; Memory Altar for the guest

- New tool: tools\gamedata\lua50.py - reads the game's compiled Lua (Lua 5.0 with one extra empty list
  between upvalue names and constants) and disassembles named global functions.
- Silencing Blank (CONFIRMED from global_event.lua): OnLeaveMagic and OnLeaveMenu_Yes return at once while
  event flag 4047 is set; otherwise LeaveSession(), set 4047, OnLeave_Limit in 5 s (the trip home).
  BBSC deaths/boss kills leave 4047 set again later (cleared once per respawn), so the item did nothing
  (guest log 2026-10-02: 3 uses, no effect); when it ran, the kept-session patches stopped the leave
  halfway (the 2026-09-24 frozen guest). BbscLeaveItemUsed (from the Lua event hook 0x01339870, before the
  script runs): living player in a session -> clear 4047, SuspendBbscPersistence (20 s, as quit to title).
  Guest goes home; host ends the co-op. Untested.
- Memory Altar (CONFIRMED from t210699): opens only if f47(3, 4104, 0, 1, 0) == 1 (LOCAL player holds the
  Rune Workshop Tool), else "The workshop tool required for this task is missing." (the 2026-10-01
  screenshot). Workbench gates 4103 / 4102 the same way. ESD bytes 6F 43 82 <item> 40 41 40 89; guest in
  the host's Dream + host flag 6631/6630/6633 (event 12100450, group 6 comes with the host's Dream state)
  -> comparison byte 0x40 -> 0x43 (count <= 1), restored on leaving. Found with FindInMemory off the game
  thread, bytes re-checked before every write. Logs "BBSC Dream: Memory Altar (...) - ...". Untested.
- bbsc.dll 31FE6356 (mod 11) in all four C8FB6283 zips; TEST-LIST 34-35.

## 2026-10-04 (b) - mod 10: boss fight wipe - both players reload

- Report: both died in a boss fight; the first respawned, the second stayed dead, world not reset.
  STRONGLY INDICATED: a dead player's copy on the other machine keeps HP > 0 (only [chr+0x58]+0x114 bit 0
  set), LivingPartnerExists only checked HP, and TickBbscRemoteRevive even revives such a copy (it cannot
  tell a dead partner from a stale copy of one that respawned in place). So the second to die saw the
  first alive and spectated forever.
- Relay packet "SHAD" 0x23 (u32 random player id, u8 dead, u32 sequence), sent by every player twice a
  second while in a session (dead = spectating or HP <= 0); bbsc_relay.cpp Dispatch -> OnBbscLifePacket.
  LivingPartnerExists returns false when every fresh (3 s) report says dead; remote revive skipped while a
  partner reports dead. Sent with SendBbscWorldFlagsToPeers (raw frame); the built-in emulator build
  sends it too but has no receiver (ignored). Logs "BBSC life: ...". Untested.
- Floating host (2026-10-01) was a patch the tester switched on by mistake, not a BBSC bug; F8 stays.
- bbsc.dll 92F8EA56 (mod 10) in all four C8FB6283 zips; TEST-LIST 32-33.

## 2026-10-04 - mod 9: guest map items no longer wiped by the host's item sync

- Guest log 2026-10-02 (mod 8, test logs): pickups DO work (items 0x12/0x22/0x32,
  drops, list +0x38: reach -> accepted -> take -> lot given). Map items (list +0x28, ids & 0xF == 3) were
  created at every load (132, 119 pickable) and the whole list was empty 2-3 s after each
  "reinsert v8: restarted ... FirstSyncWait" (3 of 3 times) - so they could be seen (glow) but not reached.
- CONFIRMED statically: item manager network update 0x017D2F30 (called from 0x0193AC10) reads packet 0x2C;
  one message kind drops the receiver's items, selector [rbp-0x19C]+1 via jump table 0x017D4C2C: all lists
  (calls 0x017D3498/34A0/34A8) or one (0x017D4089/409A/40AB). 0x017D59F0/5B40/5C40 free bit-0x80 items and
  then zero the list head/tail (so every item of that list is lost).
- Fix: Retargets GuestItems.KeepOnSync* (6 calls -> ReturnZero 0x01300AA0). Not PersistNoOp: that address
  is hooked for the rejoin logic. Untested in game; proof = the survey count staying after the load.
- bbsc.dll BD004B08 (mod 9) in all four C8FB6283 zips; TEST-LIST 31.

## 2026-10-01 (e) - mod 8: every visible item pickable for the guest, F8 unstuck

- User rule: every item the guest can see must be pickable. Prompt search 0x017D9FB0 (decompiled) takes
  any item with +0x48 bit 0x40 and has no session check, so "seen but not pickable" = bit off.
  TickBbscGuestItemsPickable (guest in host world, 1 s): items with the bit off and pickup flag (+0x28)
  not set -> the game's enable 0x017D9E70(manager, &id, 1). Log "BBSC guest items: N item(s) made
  pickable". Risk: items an event keeps off on purpose become pickable too. Untested.
- Floating host (rolled into the guest at an elevator, stuck inside the guest, backed out, floated):
  cause not found. F8 in the game window (DLL only): warp to own position +0.5 m + collision reset
  0x018BCF30(chr, 1). build-mod.cmd links user32.lib. Untested.
- bbsc.dll 037C40EC (mod 8) in all four C8FB6283 zips; START-HERE mentions F8; TEST-LIST 29-30.

## 2026-10-01 (d) - mod 7: guest stuck dead after a boss-fight wipe, guest workbench tools

- Test 2026-10-01 (test logs local files, build 12236534 / mod 6, host = BBSC-12236534 folder, guest
  = "host 2"; only the host's mod log was sent). Host died in a boss fight (spectate), guest died, host
  reloaded (18:34:13), guest followed in still dead and only respawned when the host died again.
  STRONGLY INDICATED: the guest's spectate kept waiting for a boss kill after the load reset the boss.
  TickBbscSpectate: if the local character changed since the spectate started (a load) -> respawn
  in place, "a load ended the boss fight".
- Workbench t210692: Blood Gem Fortification / Arcane Haze are gated by 72100200 / 72100201, set only
  when the LOCAL player holds key item 4103 / 4102 (f47). Guest saw "The workshop tool required for this
  task is missing." (FMG 30 10011070). BbscGiveHostWorkshopTools: when the guest's workbench talk is
  allowed, set those flags if the host's m21_00 snapshot (group 72100, index 20) has them. Untested.
- Scaling worked as logged: 1 guest -> x1.40 / x2.00 / x1.15; drops to 0 guests during each warp-together
  and comes back with the re-summon.
- Not fixed, need the guest's mod log: host floating (no gravity), items seen but not picked up,
  connection trouble until the server restart (need server\logs).
- C8FB6283 (2026-10-01 (c), never summarised): mod layout detection (FindGameFiles / PrefixBackend:
  dvdroot_ps4 at the root, game folders straight in the mod, or wrapped), Mods Folder... button
  (<user>\mods-folder.txt, read by launcher and emulator), settings window scrolls and fits the screen,
  captures/ not game files, emulator Discord RPC skipped when the BBSC status is on, scaling compiled in.
- bbsc.dll EBECD699 (mod 7) in BBSC-C8FB6283 / -Nucleus; all four C8FB6283 zips rebuilt (mod 6 zips in old\).

## 2026-10-01 (b) - mod 6: enemy scaling (like ERSC), untested in game

- Vanilla co-op scaling CONFIRMED statically: only Lua_MultiDoping (0x0138BC70, boss scripts) -> SpEffect
  7500 (HP x1.5, 1 guest) / 7501 (x2.0, 2 guests) from [[global+0x16F8]+0x08]; normal enemies none.
- Patch group Scaling (SHADPS4_BBSC_SCALING, default on): 0x0138BCC2 / 0x0138BCF9 JNZ->JMP, vanilla buff off.
- bloodborne_re.cpp: param tables found in memory by header (new src\core\bbsc_memscan.cpp, SEH-guarded
  VirtualQuery walk) from start-up; row 7500 copied over 7502 (enemies) / 7503 (bosses, NpcParam
  isSoulGetByBoss), put into a free spEffectID slot of every teamType-23/boss row; rows rewritten per guest
  count (maxHpRate +16, attack rates +72..+84), superArmorDurability +280 scaled. Defaults: enemy HP 40,
  boss HP 100, damage 15, stagger 15/20 (% per guest), Mods-tab settings. Trace: Lua_MultiDoping hook.
- TESTED (boot, this PC): tables found in 2.4 s at the title, 5359 enemy + 392 boss rows, 58 without a
  free slot; 40 patch sites. NOT tested: in game. Open: does a changed row rescale enemies already
  spawned, or only after the next load? Do enemies spawn at full (scaled) HP? Host-decides sync not done.
- bbsc.dll 9A75907F (mod 6, the boot-tested build), in BBSC-12236534 / -Nucleus zips. CMake: bbsc_memscan for the next emulator build.

## 2026-10-01 - mod 5: 0.0.0.0 summon address fixed (Nucleus test, untested fix)

- fra98_'s Nucleus test (test logs): BBSC DLL loaded in both windows (mods-next-to-exe
  CONFIRMED), shadnet login OK, but every summon_messenger call went to http://0.0.0.0:31315 (from ss.info of
  the handler's NoDreamForHunter server) -> "Could not establish connection", no signs. bbsc_http.cpp
  FixAnyAddress: http(s)://0.0.0.0 -> 127.0.0.1 (one warning line). bbsc.dll FE08B6C2, mod 5.
- mod_scan.h (both copies): captures/ is not game files either (still "DLL + Files" in 12236534).

## 2026-09-30 (g) - 12236534: mods next to the emulator (Nucleus Co-op)

- framilano's BloodborneNucleusCoopHandler starts shadPS4.exe directly with a fresh %APPDATA%\shadPS4 per
  window, so <user>\mods was empty and BBSC never loaded. mod_manager.cpp Root(): when <user>\mods has
  no mod, use <exe dir>\mods, then <exe dir>\..\mods (log line "using the mods next to the emulator").
  TESTED (this PC, empty portable user folder): found mods, BBSC "[DLL]", 38 patches, 159 hooks.
- `share\BBSC-12236534-Nucleus\` (+zip): shadPS4.exe + mods\ + server\BBSC-GameServer.exe +
  README-NUCLEUS.txt (use BBSC-GameServer instead of NoDreamForHunter.exe for the 20444 relay).
- `share\BBSC-12236534\` = 7B0B4EB9 with this emulator; 7B0B4EB9 zips to old\.

## 2026-09-30 (f) - package 7B0B4EB9 (untested)

- Vanilla emulator 7B0B4EB9: skips its own Discord RPC when SHADPS4_BBSC_DISCORD=1 (the BBSC mod's
  Discord status is on); mod_scan.h logs/docs/config/cache fix (BBSC is "[DLL]" again).
- Launcher rebuilt with the same mod_scan.h fix. Package `share\BBSC-7B0B4EB9\` = 5FB60854 + both
  exes, mod 4. Zips in send-to-friends; the 5FB60854 zips are in old\. TEST-LIST items 17-18.

## 2026-09-30 (e) - mod 4: Discord status (untested)

- `mod\bbsc_discord.cpp` (new): talks to the Discord app's pipe itself (no library), app "BBSC"
  1554935855674691645, asset `bbsc` = art\logo\bbsc-logo-1024.png. Shows the area, Solo / Hosting /
  Co-op guest with (n of 3|5), or "Boss fight: <name>" with a timer. Mods tab switch "discord"
  (SHADPS4_BBSC_DISCORD). Log lines `Discord status:` in the mod log.
- bloodborne_re.cpp TickBbscPresence (game tick, 1/s) -> GetBbscPresence. Names = the game's
  English text: place names item.msgbnd 19 (id = area*100 + block*10), NpcName item.msgbnd 18;
  SetBossGauge (0x01336C80) stores the name id at gauge slot +0x1658 (CONFIRMED statically).
  Member count from [[global+0x16F8]+0x18] - that it counts the host too is a HYPOTHESIS.
- bbsc.dll 0DDF77B4, mod.json "mod 4". The emulator's own Discord RPC still runs next to it until the
  next emulator build (turn it off in the launcher settings meanwhile).

## 2026-09-30 (d) - mod 3: DLL memory checks cached (1 fps fix, CONFIRMED - user tested, normal speed)

- Two testers: package 5FB60854 + mod 2 ran at about 1 fps (no crash; game logic, saves and
  online all ran). Mod log normal size, so not logging. Cause (STRONGLY INDICATED, not measured):
  the DLL answered bloodborne_re.cpp's per-read memory checks (HasMemoryAccess ->
  IsValidMapping + QueryProtection) with two VirtualQuery kernel calls each, thousands per frame,
  against the emulator's constant VirtualProtect traffic. The emulator build uses its own map.
- `mod\bbsc_mod.cpp`: QueryRegion - per-thread cache of the last 16 regions for 250 ms.
  bbsc.dll B48D62D7, mod.json "mod 3", TEST-LIST item 14. Old DLL: archive\exes\bbsc-mod2-BBCC8815.dll.
- mod_scan.h (both copies): logs/, docs/, config/, cache/ are not game files any more (the
  mod's own logs/ made BBSC "DLL + Files" and laid the mod folder over /app0). Needs an
  emulator + launcher rebuild - not built yet.

## 2026-09-30 (c) - Host package works on every network (untested with friends)

- `host-package\tools\bbsc-network.ps1` (new, shared): network list (Radmin, Hamachi, Tailscale,
  ZeroTier, home; privacy VPNs hidden) and the one host address in `server\host-address.txt`.
- START-HOST: no longer Radmin-only - uses the saved address, the only VPN, or asks;
  START-HOST-OTHER-NETWORK.cmd asks again. Firewall rules allow 26/8, 25/8, 100.64/10 and
  LocalSubnet (old Radmin-only rules are updated through the admin prompt).
- GENERATE-CARD: Enter keeps START-HOST's address; changing it saves it (restart START-HOST).
  The manual firewall hint is gone.
- JOIN-HOST (both packages, one file): takes a card (drag onto JOIN-HOST.cmd) or any address
  incl. Hamachi and names, keeps a non-31313 port, copies the card into `<user>\servers\`.
  Tested in a scratch APPDATA (card, Hamachi, named address, bad address).
- Zips: BBSC-Host-Server.zip, BBSC-5FB60854.zip, BBSC-5FB60854-with-Host-Server.zip (both).

## 2026-09-30 (b) - Boss deaths: spectate until the boss dies, reset when everyone is dead (mod 2, untested)

- Test 2026-09-29 (split-screen maker + friend, old built-in build, logs in
  test logs): dying in a boss fight respawned the player at once, and
  when both died nothing reset (enemies, boss HP). Every death was logged "no boss fight".
- Cause (CONFIRMED statically): the game's "died in the boss area" script Failed_BossAreaMission
  (0x0138B2D0, Lua) wipes all three boss gauge slots ([0x05562878]+0x1658..0x1690) and sets
  flags 4047/4000 before the death step, so BossFightActive() was always false there. With every
  death respawned at once nobody was ever "all dead", so the vanilla reload never came.
- Fix (bloodborne_re.cpp): the boss bar is remembered (every TickBbscSpectate + a hook on
  Failed_BossAreaMission, log "BBSC boss fight: Failed_BossAreaMission"); a death counts as a
  boss death if a bar is up or was up within 10 s with no boss kill since. Spectate ends on the
  boss kill (BlockClear2_1, "boss killed") instead of "bar gone" (the death removes the bar).
  Repeat deaths in the same load get the same rule. Nobody alive -> vanilla reload (unchanged).
- bbsc.dll rebuilt (package BBSC-5FB60854 updated, mod.json "mod 2"); built-in exe rebuilt
  (59ACB655, not released).

## 2026-09-30 - Vanilla emulator + BBSC as a mod, one package (5FB60854, untested in game)

- CMake option `BBSC_VANILLA` (`build\build-shadp2p-vanilla.cmd` -> `build\shadp2p-vanilla\`):
  `Core::Bbsc::BuiltIn` is false, every BBSC switch reads off (the real opt-outs
  NO_PLACEMENT_HEADER / NO_FAST_SIGN / NO_GUEST_HOOKS read on, so http.cpp's own fast sign and
  placement code stay out of the mod's way), module.cpp skips the installers and logs
  "BBSC build: vanilla", http.cpp skips its stale-record cleanup. The DLL (BBSC_MOD) keeps
  BuiltIn true. First build of that folder takes long (NMake, one file at a time).
- Mod loader: in the vanilla build the bbsc DLL mod is plain "bbsc" (no built-in entry, no
  refusal); `mod_scan.h` Scan/ModsForGame take `bbsc_built_in`. The launcher shows the DLL mod
  when one is installed (BbscBuiltIn()), otherwise the built-in entry. Choice id "bbsc" either way.
- `mod\mod.json`: every switch the DLL honours (ids match the built-in ones), relay address; no
  env block (not needed without a built-in BBSC).
- Launcher: `Bbsc::SetUpBundle` at start - `emulator\shadPS4.exe` next to the launcher is added
  to the Version Manager as "BBSC" and selected once per path; `mods\` next to it is copied into
  <user>\mods (update_existing).
- Package `share\BBSC-5FB60854\` + zip (in send-to-friends): launcher + Qt DLLs, emulator\
  (vanilla 5FB60854), mods\CUSA03173\bbsc\ (bbsc.dll + mod.json), JOIN-HOST, README, TEST-LIST,
  CREDITS. C50A7A07 (built-in) stays as the fallback. Both players must use the same one.

## 2026-09-29 (k) - Release C50A7A07 + BBSC-Launcher package

- `share\BBSC-C50A7A07-exe-only.zip` (exe, JOIN-HOST, WHATS-NEW, TEST-LIST, CREDITS),
  `share\BBSC-Launcher.zip` (launcher + Qt DLLs, README with GPL source note, CREDITS), both in
  the local test folder with the current BBSC-Host-Server.zip; 7D099618 zip moved to old\.
  client-dev\shadPS4.exe = C50A7A07 (7D099618 kept in archive\exes). Nothing of it run in game yet.

## 2026-09-29 (j) - Server cards: GENERATE-CARD for hosts, Import Card in the launcher

- `host-package\GENERATE-CARD.cmd` (+ `tools\generate-card.ps1`, also in the public
  BBSC-Host-Server package, zip rebuilt): asks a server name, lists this PC's networks (Radmin
  26.x, Hamachi 25.x, Tailscale 100.64/10, ZeroTier, home network) or takes a typed address
  (playit tunnel -> local 31313, domain), reads RegistrationSecretKey from server\shadnet.cfg and
  writes `cards\BBSC-card-<name>.json` {name, shadnet, registration_key, network, created}. For a
  non-Radmin network it prints the firewall commands (START-HOST only allows 26.0.0.0/8).
  Tested on a scratch copy with a fake key.
- Launcher Accounts tab: **Import Card...** copies a card into <user>\servers and selects it
  (address + key filled in).

## 2026-09-29 (i) - Patches tab in the launcher's game settings (untested)

- The Mods tab only read patch XMLs inside mod folders, not the downloaded shadPS4 / GoldHEN
  patches (%APPDATA%/shadPS4/patches/<repo>/, files.json maps XML -> serials). New **Patches**
  tab next to Mods: lists this game's patches for its version (APP_VER from the game folder's
  param.sfo, update folder wins; AppVer="mask" = any version), a switch per patch with the note
  and author as tooltip, Download / Update Patches (the launcher's own CheatsPatches downloader,
  both repos), Open Patches Folder. Save rewrites isEnabled in the XML exactly like the Cheats /
  Patches window, so the launcher sends them to any emulator build as before.

## 2026-09-29 (h) - Mod formats: DLL, PRX, patch XML, game files; load order, zip install (emulator C50A7A07, untested in game)

- One scanner shared by the emulator and the launcher: `src-shadp2p\src\core\mod_scan.h` =
  `src-qtlauncher\src\qt_gui\bbsc_mod_scan.h` (keep identical). mod.json is optional; a dropped
  folder is detected: *.dll, *.prx/*.sprx (or prx\), *.xml patches (or patches\; every
  <Metadata Name> becomes its own switch, default = its isEnabled), game files (files\, a .zar,
  or folders like dvdroot_ps4\ at the top). `mods\<SERIAL>\<mod>` = that game only.
- Emulator: file mods are extra read-only layers in front of /app0 and /hostapp
  (`MntPoints::AddOverlays`, first in load order wins); PRX mods are loaded and started next to
  `custom_modules` (`Linker::Execute`); patch mods go through `ApplyPatchesFromXML` with only the
  switched-on names (`MemoryPatcher::OnGameLoaded`). The BBSC DLL build is refused in this
  emulator (BBSC is built in). The in-game Mods tab shows each mod's type and has Move Up/Down.
- Launcher Mods tab: Install Mod (from .zip/.7z/.rar via Windows tar, from a folder, or a single
  .dll/.prx/.sprx/.xml/.zar; a wrapping top folder is lifted), numbered load order with
  Move Up/Down, file conflicts between enabled file mods ("replaces N files of X").
- Checked: both build; the scanner run on sample mods of every kind found them all correctly.
  Not yet run in game.

## 2026-09-29 (g) - Mods and Accounts tabs in the Qt launcher's settings (untested)

- The user wants the tabs in the launcher's Settings window (General / Graphics / User ...), not
  the emulator's in-game one. Forked shadps4-qtlauncher at the user's version (v357, 864dc30)
  into `src-qtlauncher\`; Qt 6.10.0 (msvc2022_64 + qtmultimedia) installed with aqtinstall into
  `tools\qt\`. Build: `& build\build-qtlauncher.cmd` (NMake; log
  `build\qtlauncher-build.log` ends exit=0). CMakeLists git calls now run in the source folder
  (the rev count was empty and broke the .rc).
- `src\qt_gui\bbsc_tabs.cpp`: **Mods** tab in a game's settings (list with on/off, the selected
  mod's settings, Open Mods Folder / Rescan; writes `mods\_settings\<serial>.json`, same file the
  emulator reads) and **Accounts** tab in the global settings (server card / address = the
  shadnet_server setting, "Create Account on Server" with the registration key, optionally puts
  the new account on a local user, button for the existing User Manager).
- Runnable copy: the local test folder (uses the same %APPDATA%\shadPS4
  settings, game list and versions as the stock launcher).

## 2026-09-29 (f) - Mods and Users tabs in the emulator's settings, built-in mod loader (build 8786C794, untested)

- Settings window (F3 in game, or `shadPS4.exe -b`): two new tabs.
  - **Mods** (a game profile, or the running game): every mod for that game with an on/off
    switch and its settings. BBSC shows as a built-in mod; its switches are the
    SHADPS4_BBSC_* variables, and turning it off makes the game vanilla online. Choices are saved
    per game in `<user>\mods\_settings\<serial>.json`.
  - **Users** (Global profile, outside a game): the local users, default user, shadNet ID /
    password / email, the shadNet server address, server cards (`<user>\servers\*.json` =
    name, shadnet host:port, registration_key) and a "Create Account on Server" button
    (`src\shadnet\account_create.cpp`, the same Create request as shadnet-sample register).
    No delete button: UserManager::RemoveUser deletes the user's saves.
- `src\core\mod_manager.cpp`: mods are `<user>\mods\<folder>\mod.json` (+ optional DLL). At
  start the enabled mods' env blocks and settings go into the environment (a variable already
  set by a launcher wins), then after the eboot loads their DLLs are loaded. No injector needed.
  A folder mod with a built-in mod's id is shown but not loaded.
- Tab / L1 / R1 now walk only the visible tabs (Tab is left alone while typing).
- `mod\mod.json`: manifest for bbsc.dll, for an emulator that has the loader but not BBSC.
- Checked: builds. The UI and the loader have NOT been run with the game yet.

## 2026-09-29 (e) - Mod step 3: the game's HTTP calls wrapped in the DLL

- `mod\bbsc_http.cpp`: swaps the eboot's GOT slots for sceHttpCreateConnectionWithURL /
  CreateRequestWithURL / SendRequest / DeleteRequest (slots from new
  `tools\gamedata\eboot_imports.py`, which lists an eboot's imports and GOT slots by NID).
  Does what the emulator's http.cpp did: host_overrides.json URL rewrite, seamless header on
  /summon_messenger/*, the host placement header on search/request, connect-first AreaId on
  create, forced delete of a stale broker record on the first seamless create.
- The game imports no response-header function, so the host's placement travels through the
  relay (frame "SHAD" 0x30): sent whenever the host's game searches for signs, used by a player
  not yet in a session (SetSeamlessHostPlacementHeader), forgotten after 20 s without one.
- Left out: the 3 s fast-sign re-post (its reply would take the broker's claim from the game).
- Tested: wrappers installed (6 slots, 2 overrides) in a real start; the HTTP path itself NOT
  run yet (servers and Radmin were off, the game stayed offline).

## 2026-09-29 (d) - Mod step 2: all hooks in the DLL, BBSC packets through a relay

- `bbsc.dll` now compiles the emulator build's own `src\core\bloodborne_re.cpp` (define BBSC_MOD)
  with stand-ins in `mod\shim\` for its emulator includes (logging -> bbsc-mod.log, memory
  queries -> VirtualQuery, capture dir -> next to the DLL, param.sfo, pid).
- `mod\bbsc_hooks.cpp`: the emulator's hook trampoline (same GuestRegisterSnapshot), placed in
  the last 1 MB of the 8 MB RWX area the emulator maps after the eboot (checked mapped + unused).
- `mod\bbsc_relay.cpp` + game server `--relay-port 20444` (default on): BBSC's packets (follow
  0x20, world flags 0x22) go over one TCP connection to the game server, which forwards each
  frame to every other connected player; address from host_overrides.json or BBSC_RELAY.
  Tested with fake clients (forward, no echo, player counts) and the real game (connected).
- Start-up order in the DLL: Wozzardman's seamless patches, BBSC byte patches, all hooks,
  relay. Master switches in the DLL: BBSC_MOD_SEAMLESS / BBSC_MOD_HOOKS (the emulator's
  SHADPS4_BLOODBORNE_SEAMLESS_COOP / _RE_TRACE only turn the exe's own copy off).
- TESTED (boot only, this PC): exe with all three of its BBSC switches off, DLL via injector:
  seamless patches on, 38 byte sites, 159/159 trace hooks + every BBSC hook, relay connected,
  game running. NOT tested in a co-op session.
- Missing in the mod: the emulator-side network parts (seamless placement header / connect-first
  cross-map join, fast sign re-post, stale record removal, room-member replay, static peer map),
  the F1 camera input and the intro/loading ImGui layers.
- Host packages: firewall rule for 20444 added to start-host.ps1, server exe rebuilt.
- the local test folder BBSC (MOD TEST).cmd` + `mod-test\` for the user.

## 2026-09-29 (c) - BBSC as a mod, step 1: injector + DLL apply the byte patches

Format decided with the shadnet maker: an injector + a Windows DLL (+ an sprx bridge later), so
BBSC runs on a stock shadPS4 build. Source in `src-shadp2p\mod\`, build `mod\build-mod.cmd`
(output `build\mod\`, log `build\mod-build.log`).
- `bbsc-launch.exe [--emu shadPS4.exe] [--dll bbsc.dll] <shadPS4 args>`: starts the emulator
  suspended, LoadLibraryW of the DLL by a remote thread, resumes, waits, returns its exit code.
- `bbsc.dll`: waits for the eboot at 0x800000000, verifies every enabled site (original or
  patched bytes; any other byte = wrong game/version, nothing written), writes the install-scope
  patches, then every 0.5 s re-writes sites that went back to original and toggles the Lamp
  role group with session state 3. Log `bbsc-mod.log` next to the DLL.
- The patch list and switches moved to `src\core\bbsc_patch_tables.h`, used by both the emulator
  (bbsc_persistence.cpp) and the DLL. Emulator rebuilt: builds, behaviour unchanged (not
  released).
- TESTED 2026-09-29 on this PC: client-dev exe with `SHADPS4_BBSC_PERSIST=0` (emulator log:
  "persistence declined"), launched via the injector: image verified 5.1 s after start, 39 of 39
  install sites written (Lamp's 3 wait for hosting), no reverts in 25 s, game running. Not played
  in session with the DLL yet.
- Not in the DLL yet: session suspensions (quit to title / partner gone) and all hooks
  (bloodborne_re.cpp) - still emulator-side.

## 2026-09-29 (b) - Walking apart no longer reloads the guest, item survey (build 7D099618)

### Test of 57405528 (user, two instances)
- Floor guard CONFIRMED: `BBSC floor guard: character ... floor cleared` at the spot of the old
  crash (guest in 0x18010000), no crash.
- Guest pulled back to the host on the ladder down to Central Yharnam: `host moved to map
  0x18000000 (we are in 0x18010000), following` - the host had not moved. Old rule: same area
  and more than 120 m apart = follow.
- No "Pick up" prompt and not one `BBSC pickup:` line: the reach search 0x017D9FB0 never
  returned an item.

### Changed
- Follow (TickBbscFollowHost): remembers the host's map when both last shared one. Same area:
  follow only a host who arrived by a load (lamp, death reload). Other area: only a host who
  changed map since then. Log `... - no reload` once per map pair.
- `BBSC guest items:` survey every 5 s (logged on change) while a guest: item counts per list,
  pickable (+0x48 bit 0x40), nearest item id/flags/distance, whether the pickup action ran.

Untested.

### Messages / bloodstains (same test, server log gameserver-20260929-104029.log)
- Guest (8072) wrote message 6676292 in 0x18000000/240000 at 11:08; the host (2864) got it in
  the 11:09:32 message_area reply and did not show it. The stored PrevBloodMessId was
  9287298313769999166 (> int64 max) and was passed on as is. HYPOTHESIS: that breaks the host's
  reply parse. Server now sends PrevBloodMessId 0 to others and logs every message_area /
  tomb_messenger area query (user, asked regions, stored regions). New frozen server exe.
- No tomb_messenger/create at all: only the guest died (2 kept deaths). STRONGLY INDICATED:
  vanilla only records the world owner's death as a bloodstain. Host death not tested yet.
- Grey NPCs CONFIRMED fixed (user).

## 2026-09-29 - Grey NPCs, floor crash guard, pickup trace (build 57405528)

### Found in the 2026-09-29 two-instance test of F94ECC0E (one shad_log.txt for both; captures
### p23460 = host, p25732 = guest)
- **Guest crash** (CONFIRMED site): `Unhandled Exception 0xc0000005 at 0x8018cb16a` while the
  guest streamed in m24_00 (host in Cathedral Ward m24_01, guest to the Tomb of Oedon). Character
  floor update 0x018CB0E0 reads `[[chr+0x288]+0x18]+0x88`; the floor was collision of a map block
  that was gone. STRONGLY INDICATED: the character is the remote partner; vanilla keeps both in one
  block with the co-op fog walls BBSC removes.
- **Grey NPCs** (CONFIRMED statically): NPC constructor 0x018E4870 swaps isChangeWanderGhost rows
  to the ghost model + type 4 for anyone in another world. The earlier "tester's mod" explanation
  for grey NPCs was wrong.
- **Items**: `BBSC.Items.PlaceTreasure` ran 65x on the guest - items are placed now. Statically no
  session gate on the pickup path of a plain treasure (reach search 0x017D9FB0, action 0x012FD880,
  take 0x017D8570, give 0x017D89F0). Cause unknown.

### Changed
- `SolidNpcs.NoWanderGhost` 0x018E4FBF 74 1B -> EB 1B (switch `SHADPS4_BBSC_SOLID_NPCS`, on).
- Floor guard hook at 0x018CB14F (switch `SHADPS4_BBSC_FLOOR_GUARD`, on): in a session, a floor
  whose chain does not read back is cleared (the function's own no-floor path). Log
  `BBSC floor guard:`. Best effort - freed memory that still reads sane passes.
- Pickup trace: `BBSC pickup:` lines for item in reach (guest), button accepted, take item, lot
  given.

Untested.

## 2026-09-24 (f) - Faster cross-map summon: fast sign re-post (build 43B41805)

No Qt SDK here, so the shadnet rebuild was skipped; the speed-up lives in the emulator instead.
`http.cpp`: after the guest's own `/summon_messenger/create` on the seamless route, the worker
re-posts the same sign every 3 s (at most 5 times, 15 s), writing the host's map in as soon as the
broker names it (`RewriteConnectFirstAreaId`, extracted from `ApplyBloodborneSeamlessRoute`), and
stops at the delivered claim (reply contains "SessionId"). The game gets the last reply as the
answer to its post. Off switch: `SHADPS4_BBSC_NO_FAST_SIGN=1`. Log: `BBSC fast sign: re-post N`.
Expected: advertised -> preparing -> re-advertised in host map -> claimed -> delivered inside one
game post instead of four, i.e. roughly 15-30 s instead of ~2 minutes. Untested.

## 2026-09-24 (e) - Cross-map summon: works; the failure was a password mismatch

Server logs (`server/logs/shadnet-*.log`, UTF-16):
- 00:14 run (the "cross-map broken" report): host searched with `SummonWord` "BBSC", the guest's
  sign carried "5676". `MatchesSearch` rejects a different SummonWord even in anywhere mode, so the
  sign was never matched and never reached the cross-map preparation. Not a code bug.
- 2026-09-23 20:02 and 2026-09-24 12:54 (both "BBSC"): full cross-map sequence - advertised,
  "preparing cross-map ... placement bytes 53", re-advertised in the host's AreaId, claimed,
  delivered, room joined. 12:54:35 sign -> 12:56:34 joined = about 2 minutes (same map: 5 s).
- The time goes into the guest's sign re-post, every ~30 s although ss.info sets
  SummonDataCreateInterval2 = 5 (the game appears to keep 30 s), times 3-4 round trips.

Possible speed-up (not done, needs a shadnet rebuild): return a cross-map sign to the host's search
straight away (location spoofed, as anywhere mode already does) instead of hiding it as Preparing,
and deliver the claim with the host placement even while the guest's AreaId still differs - the
guest's connect-first handoff then loads it at join. Two round trips fewer, ~30-60 s total.

Tester notes (WHATS-NEW.txt in the friend packages) now say: same matchmaking password on both.

## 2026-09-24 (d) - Guest echoes doubled at the co-op multiply (build 04819EAF)

Both players NG+ (user), and the friend's own test also shows the guest at half, so the NG-cycle
theory is out. The JZ -> JMP skip at 0x01335582 is removed. Instead the co-op multiply at
0x01335589 now reads 2.0 (0x04731F04) instead of 0.5 (0x04925B1C) - displacement 0x035F058B ->
0x033FC973 at 0x0133558D, `Echoes.GuestDouble`, new 4-byte `WideEdits` table. Only a summoned
guest reaches that multiply; the unknown second halving then brings them level with the host.
Expected in the log for one kill: host +1341, guest +1341. If the guest shows +2682, the second
halving does not exist on that path and the constant goes back to 1.0.

## 2026-09-24 (c) - Guest respawn at the nearest lamp of the map (build 7B144918)

### Test of 39A08659 (user, Discord summary)
- Fog walls fully gone, textures included. Guest bloodstain at the death spot. Warping works.
- New: guest respawned beside the host. Log: `last lamp point 2102961 is not in the loaded map`
  -> fallback "partner". The guest's own last lamp was the Hunter's Dream lamp from the follow.
- Echoes still host +1341 / guest +671 (NG+ question open).

### Changed
- When the last lamp is not in the loaded map, respawn at the nearest lamp of the loaded map
  (FrpgNetMan lamp list, point = lamp id + 1000, nearest to the body). Partner stays the fallback.

## 2026-09-24 (b) - Fog SFX ids, guest stain position, lamp role (build 39A08659)

### Found in the 2026-09-24 second local log (test logs)
- Repeat deaths now handled (3 s, echoes dropped) - confirmed working.
- **Fog stays visible:** the sweep hid 13 walls in block 24 but found 0 fog effects. The map files
  name the co-op fog SFX parts `SFX_霧壁_マルチ_*` and give them the listed ids themselves
  (2403995-2403999, 2413997/2413999, 2703998/2703999, 3203999, 3503990, ...), not id + 1.
- **Guest stain by the lamp:** `0x013D2910` places a guest's stain (global+0x1592 == 0) at the
  forced summon placement +0x1500/+0x1510, map +0x14F0 - the lamp spot our warps wrote.
- **Echoes:** same kill, host +1341 and guest +671 = 1341 x 0.5 rounded the game's way. The patch
  at 0x01335582 is applied (22 sites) and is the only reference to the 0.5 constant; the enemy's
  own SpEffect echo rates (NG-cycle scaling) are the remaining multiplier - open question whether
  the host's world is NG+.
- **"Warp does nothing":** the StageTransition came from the GUEST process (user_id 8072 = the
  guest's sign), the host removed the member, and no load followed. The host never started a warp.

### Changed
- Fog SFX refused and swept by the id itself (id + 1 kept).
- A guest's BBSC-dropped stain uses the own-world branch: the character's position and map.
- Lamp talk-script patches need session state 3 AND own world AND player type 0.

## 2026-09-24 - Repeat deaths, Old Yharnam walls, fog/echo logging (build AA6B118B)

### Found in the 2026-09-24 local log
- **Deaths 2 and 3 were never handled by the game.** Its death script (`OnEvent_4000_Hp`, an HP <= 0
  condition from the map init `0x0137D7A0` via `0x01329D30`) is armed once per map load. A kept
  death does not reload, so the first death ran `SoloPlayDeath` normally and later ones ran
  nothing: the player lay dead until the 15 s watchdog, and kept their echoes because no step had
  recorded them.
- **Old Yharnam's walls are not in the map scan.** `2301999` and `2301950` are raised by the same
  EMEVD handler as the Cathedral Ward area walls, as soon as Old Yharnam loads in a session.
- The fog sweep never logged a success in the whole run, and the echo patch is applied but the
  guest still reports half: neither can be settled from this log.

### Changed
- A death after a kept death in the same load is handled by BBSC after 3 s: echoes recorded at the
  moment of death, bloodstain dropped, respawn at the last lamp. The first death is unchanged.
- `2301999`, `2301950` added to the area walls (suppressed only while a session is live).
- Logging: fog sweep result once per map block; every echo change in a session with player type.

## 2026-09-24 - Local test of E82670ED (user, two windows over Radmin)

### Worked
- **Lamp in co-op and warping together**: Central Yharnam -> Hunter's Dream -> Old Yharnam, guest
  followed both times.

### Still broken / to check
- Blood echoes for the guest still halved.
- Fog walls still visible; one co-op wall in Old Yharnam near the lamp could not be passed.
- A boss the host had already killed was dead for the guest too (expected in the host's world);
  still to check that the guest's OWN world keeps that boss alive after going home.
- An earlier attempt with the two players on different maps (Old Yharnam / Cathedral Ward) never
  matched: the guest's sign was accepted 5 times, host searches always empty (known cross-map bug).
- Log copy: test logs.

## 2026-09-23 (late) - Lamp "Travel" prompt in co-op, build E82670ED

The lamp prompt is not decided by the lamp condition object (`0x012F5C40`, class "Condition:Bonfire",
vtable `0x0531CA70`); that function always returns 0 and its four gate bytes only drive the glow and
the FrpgNetMan lamp list (`+0x48` "usable", read by talk function 0x25 via `0x0146E6E0`). The prompt
comes from the lamp's **talk script**. The talk condition dispatcher `0x0203AD60` answers script
function 0x34 ("is multiplayer in progress") with the same expression as lamp gate `+0x49`
(session member vector non-empty, or `[global+0x16F8]+0x14 > 1`), ending in `OR CL,AL` at
`0x0203C564`.

### Changed
- **Lamp.TalkMultiplayerCheck** (`0x0203C564`: `08 C1` -> `30 C9`), in `Group::Lamp`, so it is on only
  while hosting (`SetHostLampPatches`). On the host every talk script now sees a solo game: lamp
  prompt, and anything else the scripts gate on co-op. The guest and single player stay vanilla.
- STRONGLY INDICATED, not yet played. Expected host log: `BBSC lamp: prompt allowed ... (3 sites)`.
- New read-only Ghidra helpers: `tools/dumpq.cmd` (qwords/vtables), `tools/strat.cmd` (strings).

## 2026-09-23 - Full code review: save snapshot, home map, warping, partner-gone, save writes

Every BBSC line was read and every issue written down in `docs/CODE-REVIEW-2026-09-23.md`. The two
most serious findings were checked against the game code (Ghidra decompiles of `0x013824C0`,
`0x013CDE30`, `0x01332BC0` and the `StageTransition` listing), not just against our comments.

### Fixed
- **A1 - guest's own-world snapshot kept through a kept death.** `PartyGhostDeath_2` deletes
  `global+0x1698/+0x16B8` first thing, unconditionally. They are held away from the step and put
  back on the next tick. Suspected half of "guest spawns at Iosefka's Clinic".
- **A2 - the guest's home map is never overwritten.** `+0x14F0` is the home map (the selector reads
  it whenever `+0x1520` is set, and every send-home path sets it). The join and follow warps wrote
  the host's map there and left `+0x1520 = 1`. They no longer touch either. Suspected other half.
- **A3 - warping together aims where it asks.** Session reloads load the summoned placement map in
  phase 3, so the follow went back to the original join map. `RequestBbscSessionWarp` now writes the
  destination into the summoned placement block and `+0xC`, sets phase 3 itself, and the follow has
  a 90 s arrival timeout with 3 attempts per host map.
- **A4 - save writes (emulator, upstream bug).** `_wfopen_s` is exclusive, and the save-backup
  thread's copy made about 95% of Bloodborne's save writes fail with "permission denied" (151/158
  on 2026-09-23, 126/129 on 2026-09-19). An exclusive open of an existing file that fails with
  EACCES now waits up to 2 s. Worth reporting upstream.
- **B1 - partner gone no longer kicks the host.** After 180 s (was 90 s, now long enough for a slow
  first load) the host ends the session exactly as `StageTransition` does - `Stop([0x05540290])`,
  `RemoteCleanup(global+0x16F8)` - and stays in their world. A guest still takes the vanilla path
  home, with only the session patches lifted.
- **B2 - suspensions only lift session patches.** Echoes, lamp, fog walls, death handling and
  host-death handling stay on through a quit or partner loss.
- **B3/B4 - lamp edits only while hosting.** A guest never gets the lamp prompt; single player
  keeps both vanilla lamp gates.
- **B5 - warp-together after a host restart.** A lower sequence is accepted once the last packet
  is stale.
- **D1** `SHADPS4_BBSC_TRACE_MAPS` off by default. **D2** raw NUL bytes in a source file replaced
  with `'\0'` (no behaviour change).

### Correction (added later the same day)
An earlier version of this entry said a tester's log showed a non-BBSC run. Retracted: the log he
sent was from 2026-09-05 - his portable install logs elsewhere and `collect-logs` did not look
there. `collect-logs` is fixed; see `docs/CODE-REVIEW-2026-09-23.md` E1.

Not played yet.

## 2026-09-23 - The "AFK kick" was the wandering-ghost reply

The tester reported that the game server "kicks the player when it thinks they are AFK". The
host's shadPS4 log shows the real sequence: `POST /wandering_ghost/get` -> our reply
`{"ResKind": 0}` (14 bytes) -> **the very next line is `BBSC quit to title requested`**, i.e.
"Failed to connect to Bloodborne server. Returning to title menu." Every HTTP call before it
returned 200.

The game reads `WanderingGhostList` from that reply; a reply without it counts as a failed call.
Ghosts are fetched every `WanderingGhostGetInterval2` (120 s), so standing still is exactly when
it fires - hence "AFK". fhody125's NoDreamForHunter (the tester's server, and the code ours came
from) answers every unknown path the same bare way, so it has the same kick.

### Fixed
- All 37 game APIs mapped from the eboot (`Name:%hS/path` UTF-16 strings) and their reply fields
  from the key block after `ResKind`/`WarningMessage`. Every non-summon path now gets
  `ResKind 0`, a `MessageId`, and **every list the game can read, empty** (`WanderingGhostList`,
  `BloodMessList`, `TombMessList`, `ChairMessList` + `ChairMessCount`, `ChannelList`,
  `ChannelInfoList`, ...). Creates return a fresh `BloodMessId` / `TombMessId`. Unknown paths get
  the same all-lists-empty reply instead of a bare one.
- The first 3 request bodies per path are logged (not login, not summons), to build the real
  messages / bloodstains / glyph services from later.

Verified: all 33 routes plus an unknown path answer with the field the game reads (test copy,
port 20443 untouched). **Not yet played** - the proof is standing still for 5+ minutes without a
kick.

## 2026-09-23 - Game server: no more host kicks when the VPN drops, and a log file

First Radmin session. The host got "Failed to connect to Bloodborne server. Returning to title
menu." The game re-reads ss.info every `ReloadServerStatusInfoInterval2` (300 s) and checks in
(login, get_datetime) at the addresses it names; one failed check-in sends it to the title. The
ss.info named the VPN address for everyone, including the host's own game, so switching Radmin off
kicked the host from their own server.

### Changed (`tools/gameserver/bbsc_gameserver.py`)
- **Two ss.info variants.** A game on the same PC (caller `127.0.0.1`/`::1`, or the advertised
  address itself) gets the non-summon endpoints at `127.0.0.1`; the other player gets the VPN
  address as before. The four summon endpoints keep the shadnet address in both: co-op needs the
  VPN anyway, and that path is the tested one.
- **Log file.** Everything the console shows also goes to `gameserver-<date>.log`;
  `START-GAME-SERVER.cmd` passes `--log-dir server\logs`, which `collect-logs` already zips.
- Every line now names the caller's address, so a log shows whose game stopped checking in.
- An exception while answering is logged with its traceback and answered with a generic reply
  instead of dropping the request.

Verified on a second copy (port 20499): local caller gets `api_Login` at `127.0.0.1`, remote
variant at `<vpn-ip>:20443`, summon endpoints `<vpn-ip>:31315` in both, log file written.
Not yet played. Backups: `bbsc_gameserver.py.bak-20260923`, `START-GAME-SERVER.cmd.bak-20260923`.

### Also found this session (not fixed)
- Messages, bloodstains, ghosts and Chalice glyph channels are **stubs** (as in fhody125's
  NoDreamForHunter, which this came from) - empty replies, nothing stored.
- Cross-map summon failed (host m23 searching, guest m24_01 advertised; shadnet sent the placement,
  the guest never travelled). Same-map summons work. Needs the guest's shadPS4 log.

## 2026-09-22 — The fog that stayed behind

Reported from the working session: "both can cross fogwalls, the fog is there but its passable."
Half the fix had landed and the half that renders had not.

### A fog wall is two objects
Lua_MultiWall (0x0138B720) gives it away in five instructions:

```
CALL 0x013CABD0(id, 1)   draw
CALL 0x013CA580(id, 1)   collision
INC  EBX                 id + 1
JMP  0x01315FA0          the fog effect
```

The wall proper is an **invisible collision box**; the fog you see is a **separate SFX object whose
entity id is the wall's id + 1**. BBSC hooked the draw and collision pair and refused both for the
25 co-op ids, which is why the walls went soft - but nothing touched the third call, so the fog
kept rendering. Walking through visible fog was the exact, expected result of fixing two thirds of
it.

0x01315FA0(map_list, entity_id) finds the object by binary search on entity id and sets bit 8 of
the byte at record+0x27; 0x01315E50 clears it. It only ever enables - there is no "on" argument -
and the id arrives in **rsi**, not rdi like the other two.

### Fixed
A third hook on 0x01315FA0 (tag 2), refusing the effect when `entity_id - 1` is a suppressed wall.
The boss gates are guarded twice over: BbscWallSuppressed already refuses to suppress one, and the
hook additionally never refuses an id that is itself a boss gate.

Its prologue is PUSH RBP / R15 / R14 / R12 / RBX (`55 41 57 41 56 41 54 53`), eight bytes, not the
six-byte frame setup the other two open with.

**Caveat, stated rather than hidden:** the skip mechanism returns to the caller without running the
function, so the return value is whatever was in RAX at entry rather than a deliberate 0. For the
Lua path that is the address of a singleton pointer, i.e. non-zero, which reads as "the object was
found" - the outcome we want. The same mechanism already drives the draw and collision refusals
and has behaved in a session, which is the only reason this is acceptable rather than sloppy.

Built as 65475B5EE4C1D08E. Not played.

## 2026-09-22 — Warping together

**Built as 0E1C461F19B08841, never played.** The machine ran out of commit charge partway through
this change (C: down to 0.25 GB); clearing the caches freed enough to compile. One real bug fell
out of that gap: bbsc_join_teleport_done was declared after the function that sets it, which the
compiler would have caught immediately.

### The problem
When the host takes a lamp to another area, vanilla ends the session - a client cannot follow a
host across a stage load, so everyone is sent home. The persistence patches keep the session alive
through that load, which leaves the guest standing in a world the host has left.

Nothing the game sends tells a guest where the host went, because vanilla never needs to know: it
only needs to know that the stage changed, so it can end things. The host placement BBSC already
transports (the `X-` header on the summon exchange) is a handshake-time channel and is not
refreshed during a session either. So there is nothing to piggyback on, and the message has to be
ours.

### The channel
`SignalingBbscFollow`, a 30-byte packet (type `0x20`) on the np_signaling UDP socket the two peers
already keep alive with echo pings:

```
magic[4] type kind packed_map x y z heading sequence
```

Chosen over an HTTP endpoint on our own game server because the peers already hold each other's
addresses, it needs no new port, no server change, and it keeps working if shadnet is restarted
mid-session. `sequence` drops announcements that arrive out of order, which UDP allows. Any build
that is not BBSC drops it as an unknown packet, so it is safe on a mixed pair.

### The behaviour
* **Host** (`TickBbscFollowBroadcast`): while hosting a session with someone in it, in its own
  world and not mid-load, broadcasts its map and position twice a second.
* **Guest** (`TickBbscFollowHost`): when the announced map is not the map the guest is standing in,
  runs the same two steps the cross-map join already uses and that are confirmed working - a real
  reload through `0x013CDE30` with the forced-warp destination (`RequestBbscJoinWarp`), then the
  in-map warp onto the host's spot once the load has settled.

The position comes over the wire rather than being read from the host's character, because after
the host's load that character no longer exists in the guest's world - which is the whole problem
being solved.

`bbsc_join_teleport_done` keeps the initial join and the follow from moving the same character at
the same time: following only starts once the join teleport has placed the guest.

Switch `SHADPS4_BBSC_WARP_TOGETHER`, on by default.

### Not yet known
Whether the guest's reload lands cleanly while the host is still finishing its own, and what the
remote-player records do across the pair of loads. Both are things only a session will say.

## 2026-09-22 — Death that costs something, for both players

Reported from the first working session: the guest "didn't get you died prompt, but respawned
after a while, but this doesn't take her echoes, so it's like she didn't die at all."

### Most of that is vanilla, not ours
`PartyGhostDeath_2` (0x013824C0), the summoned guest's final death step, drops a bloodstain only
for character **types 2 and 0xC - the two invader types**. A co-op phantom is type 1, so it never
loses anything. And `PartyGhostDeath` (0x01381FD0) never raises the `0xFFA` text event that
`SoloPlayDeath` (0x01381B60) raises, so there is no "YOU DIED" either. In vanilla that is
consistent: a dead phantom is thrown out of the session and back into their own world, having
risked nothing. In a kept session nobody is thrown anywhere, so the death simply does not happen.

The rest is ours: a kept death skips the reload, and the reload is what normally commits a death.

### The penalty is one call
`DropBloodstain` 0x013D2910(stain_root, chr, 1) does all of it (CONFIRMED statically):

* copies the map id and the death transform (`[[[chr+0x58]+0x3B0]+0x68]+0x1E0` position, `+0x1D0`
  rotation) into the record at `[0x0553B130]+0x30`,
* pays out any pending reward sitting in the accumulator at `+0xF8`,
* moves the player's blood echoes from **`[[0x0553B130]+8]+0x94`** into the record at `+0x38` and
  zeroes them,
* sets the "a stain exists" byte at `[0x0553B130]+0x28`,
* and calls the stain spawner `0x01868A50(holder, amount >= 0)`, which builds the object **in the
  world that is already loaded**. So the stain appears where the player died with no reload - which
  is exactly what a kept death needs.

`stain_root` is `[[0x0553B108] + 0x80]`, the same argument `SoloPlayDeath_2` passes.

### What BBSC does now
* The echo count is snapshotted when the death step starts and read again just before the respawn.
  The drop is only made when the count is still non-zero, so the host's own drop is never repeated
  and a guest - who never had one - gets one. It also covers a host whose guard
  (`global+0x1649`) happened to be set.
* The guest's `PartyGhostDeath` raises the same `0xFFA` text event `SoloPlayDeath` does, with the
  same two arguments (`chr->vtable[0x558]` and `[0x598]`), so "YOU DIED" appears for both players.
* The watchdog path drops as well, so a death whose step goes missing still costs something.
* `global+0x1770` is logged with every drop: 0x013D2910 does nothing at all when that byte is zero,
  so a run where the echoes survive names its own reason instead of needing another guess.

Switch `SHADPS4_BBSC_DEATH_PENALTY`, on by default. **Not yet confirmed in a session** - the host
log from 2026-09-22 shows the respawn working (`SoloPlayDeath` -> `SoloPlayDeath_2 without reload`
-> `respawn in place ... warped to last lamp`), but nothing about echoes, because none of this
existed yet.

## 2026-09-22 — The guest's half share of blood echoes

Reported from the first working two-player session: **the guest took 24 blood echoes where the
host took 48.** Exactly half, which made a multiplier the obvious suspect rather than anything
about who landed the killing blow.

### Where it lives
Every blood echo reward the local player receives is computed in one place, `0x01335400`, which
leaves the result in a float at `[[0x0553B130] + 8] + 0xF8`. The payout sites read that float
back, zero it, and hand it to `PlayerIns::vtable[0x3D0]` - boss clear (`0x013849A0`, for both
player types) and the ordinary enemy kills (`0x013352C0`, `0x01335350`, `0x013356C0`,
`0x0131F8D0`, `0x0137D7A0`). One function governs the lot.

Halfway through it:

```
0x01335578  MOV EAX, dword ptr [R15 + 0x78]   ; the local player's character type
0x0133557C  OR  EAX, 0x8
0x0133557F  CMP EAX, 0x8
0x01335582  JZ  0x01335596                    ; taken for type 0 and type 8
0x01335584  VMOVSS XMM0, [RBP + -0x44]
0x01335589  VMULSS XMM0, XMM0, [0x05925B1C]
0x01335591  VMOVSS [RBP + -0x44], XMM0
0x01335596  ...                               ; rune / player-count bonus, then the store
```

`0x05925B1C` holds `0x3F000000` - **0.5**. `(type | 8) == 8` is only true for type 0 (own world)
and type 8, so the host is exempt and a summoned guest (type 1) is paid half. Invaders (types 2
and 0xC) are in the same boat.

### Fixed
`Echoes.CoopHalfRate`, a two-byte edit at `0x01335582`: `JZ +0x12` (`74 12`) becomes `JMP +0x12`
(`EB 12`). Same length, same destination, the multiply is simply never reached, so both players
are paid at the host's rate. Solo play never reached it either way - the member-count test a few
instructions earlier already jumps to the same label - and the rune and player-count bonus that
follows is untouched, so each player still gets their own runes' worth on top.

Switch `SHADPS4_BBSC_FULL_ECHOES`, on by default. Byte-verified at install like every other patch:
a wrong address makes the installer decline and log a signature mismatch rather than write
anything.

### The constants, for the record
`0x05925B1C` = 0.5 (the co-op rate), `0x05925B20` = 1.0 (the rune chain's starting value),
`0x05925B24` ~ 5e-6 (a rounding epsilon), and `0x05731EF0` is the player-count bonus table
`[0.0, 1.0, 1.1, 1.25, 1.5, 2.0, 2.5]`, indexed by `[0x0553B130] + 0x68` clamped to 6.

Raw `eboot.bin` file offsets do **not** equal loaded-image offsets, in `.rodata` as well as
`.text`, so the constants had to be read through Ghidra's memory rather than out of the file. A
byte check against the file on disk is not a valid way to verify a patch address.

## 2026-09-22 — "Failed to connect to Bloodborne server" was our own login handler

The game was being thrown back to the title every few minutes, in **solo play as well as co-op**,
which is what ruled out anything session related.

### Cause
`bbsc_gameserver.py`'s `/basic_utils/login` returned `random.randint(1000, 10000)` as the UserId
and a fresh uuid as the SessionId, on every call. The game logs in more than once per run - 16
times in the 2026-09-21 22:xx session - so its identity changed underneath it repeatedly. The log
shows requests carrying `user_id=5840` and then `user_id=5447` minutes apart, with seven distinct
ids in one session. The game gave up and returned to the title.

Carried over unchanged from fhody125's throwaway implementation, where it was never exercised by a
re-login.

### Fixed
`identity_for()` derives the UserId from a SHA-256 of the account name (1000..9999) and keeps one
SessionId per account for the life of the process, so a re-login - and a restart of the game
server mid-session - returns the same identity. Verified: five consecutive logins for one account
return the same UserId and SessionId, and two accounts get different ones.

**Server-side only.** Both players' logins resolve to the host's game server through ss.info, so
restarting it fixes both; no emulator rebuild and no new friend package.

### Also corrected
`TickBbscMapTrace` reads the wrong structure. It reports all 17 map blocks every time because that
list is the game's static map table, not the set of streamed maps - the index at +0x20 only marks
which one the player counts as being in. It cannot answer whether two maps are resident, so the
cross-map texture theory is still untested.

### How the dead ends went
`LeaveRoom` from caller `0xCC790F` looked like an unpatched session-end path; it is our own
teardown running after the quit, and the quit came first. The caller of `RegistReturnTitle`
(`0x013931A0`) turned out to be a generic event dispatcher that names no reason. The user_id
changing in the request URLs was the thing that actually gave it away.

## 2026-09-21 — Co-op fog walls removed, boss gates protected

### How the ids were found (no play-testing)
Bloodborne's MSB map layouts name every part in Japanese, and FromSoft's own naming classifies
them: a fog wall is 霧壁, boss gates carry ボス, the walls that box co-op players into an area
carry マルチ, and 埋まり marks stuck-detection volumes that are not walls at all. Decompiling
`CalcGetMultiWallEntityId` (0x01338840) gave the id formula —
`block*100000 + area*10000 + n % 10000` — which bounds each map's ids to a 10000-wide window and
makes finding the id beside a name reliable without modelling the whole MSB record.

24 maps, 155 fog parts: **45 boss gates, 51 co-op walls (25 distinct ids), 33 other, 26 regions.**
The boss and co-op sets do not overlap. Tool: `tools/find-fog-walls.py`, output
`tools/fog-walls.txt`. This replaced a whole planned test round whose only purpose was harvesting
these ids from logs.

### Changed (build `2F639DE8E0BA9F66`)
- `BbscCoopWallIds` (25 ids) is now the **default** suppression list when `SHADPS4_BBSC_WALL_IDS`
  is unset and `SHADPS4_BBSC_NO_FOG_WALLS` is on, which it is by default. `SHADPS4_BBSC_WALL_IDS=0`
  empties the list without disturbing anything else the switch controls.
- `BbscBossGateIds` (27 ids) are **never** suppressed. Guarded twice, deliberately redundantly:
  once when building the list (a hand-written id in the environment variable is rejected and
  logged), and again at the refusal point in `BbscWallToggleHook`. A boss room a player can walk
  out of mid-fight is a worse bug than a fog wall that stayed.
- Both arrays were checked against the map data after editing: 25 and 27 entries, no overlap,
  exact match with what the tool reads today.

### Not yet verified in game
The ids and the formula are read from shipped data and confirmed against the binary, but no
session has exercised the suppression path yet. The log lines to look for are
`BBSC fog walls: suppressing 25 co-op walls (boss gates kept)` at startup and
`BBSC fog wall: draw refused for entity <id>` in play.

## 2026-09-21 — Our own game server (no more thehuntersdream dependency)

fhody125, who wrote NoDreamForHunter, handed over his old fake-server project (MIT) including a
real `ss.info`. That answered the open question: **`ss.info` is base64-encoded XML**, and it is the
server list. A `<gameurl2>` block names one URL per game API, so whoever serves `ss.info` decides
where every online feature goes. thehuntersdream's copy pointed at their own boxes, which is why
their outage took the whole thing down.

### Added
- **`tools/gameserver/bbsc_gameserver.py`.** Serves `/bb-eu/ss.info` and answers the other game
  APIs with empty-but-valid JSON (`SP-RES-KIND: 0`, the `MessageId` each caller expects). The four
  `api_SummonData*` endpoints are pointed at the real shadnet server; everything else (blood
  messages, ghosts, notices, channels, play logs) resolves to this process and goes nowhere.
  Standard library only - no Flask - so it runs on a stock Python.
  - `--shadnet <addr>` is the only required argument.
  - `--summon-interval N` rewrites `SummonDataGetListInterval2` / `SummonDataCreateInterval2`
    (retail 35 / 30 seconds). This is what "quick summoning" actually is - the retail file is why
    a sign can take half a minute to show up.
  - `--level-range N` rewrites the four `SummonDataCoopMatchingLevel*` tags (retail +/-20), so two
    players at any level can match.
  - Defaults change nothing but the addresses; retuning is opt-in.
- **`START-GAME-SERVER.cmd`** for both the host and the friend package, and the template XML plus
  fhody125's MIT licence vendored beside the script.

### Changed
- `host_overrides.json` now maps `https://ss4.scej-network.jp:20443` at the game server. The host
  serves it for **both** players (`--host 0.0.0.0 --advertise <tailscale ip>`), the same way they
  already serve shadnet, so the second player installs and runs nothing: their override just points
  at `http://<host tailscale ip>:20443`.
  - fhody125's design has each player run their own copy on `127.0.0.1`. That works, but it puts a
    Python install and a second window on the guest for services that are no-ops anyway, and the
    guest already depends on the host's machine being up for shadnet - so this adds no new
    dependency. `--advertise` exists for exactly this.
  - Needs an inbound firewall rule for TCP 20443, Private profile only (the Tailscale adapter is
    Private; this keeps it off public networks).
  - The friend package no longer ships `gameserver/` or `START-GAME-SERVER.cmd`, and SETUP.md drops
    the step that asked the guest to run one.

### Confirmed working (2026-09-21, host window)
The game reads it and goes online. No "Failed to acquire Bloodborne server information" dialog,
and the game server logged the fetch plus the follow-up API calls. thehuntersdream.com is out of
the path for good - it was still refusing connections at the time this was confirmed.

Also verified outside the game: the served body decodes to 11021 bytes of XML, the summon endpoints
carry the configured shadnet address, no placeholder survives, and the retuned intervals read back
as set.

Retail values recorded for reference: `ReloadServerStatusInfoInterval2` 300,
`SummonDataGetListInterval2` 35, `SummonDataCreateInterval2` 30, `SummonDataGetListGuardTimer2` 60,
`SummonDataGetListGetMaxCount2` 20, `SummonDataCoopMatchingLevelUpperAbs2` 20 /
`LowerAbs2` -20, `SummonDataBloodMadHunterPercent2` 5.

## 2026-09-21 — Direct peer connection (static peer map)

Suggested by the maintainer of NoDreamForTheHunter: skip the broker and connect the two players
directly. Both testers sit on a fixed virtual LAN (Tailscale), so asking the matchmaking server to
resolve a peer's address is pure overhead and one more service that can be down.

### Added (build `EF94CD499AACB9E1`)
- **Static peer map.** `ResolvePeerEndpoint` (np_matching2_signaling.cpp) used to fall through to
  `RequestSignalingInfos`, a round trip to the shadnet server that maps an npid to an IP and port
  (5 s timeout, fails closed). A configured entry is now checked first and wins over both the
  cached member address and the server lookup, so nothing has to agree with the broker about
  addressing. Addresses and ports are stored in network byte order, matching what
  `RequestSignalingInfos` produced.
  - Sources, first to define an npid wins: `SHADPS4_BBSC_PEERS` ("npid=host:port,..."), then
    `bbsc_peers.json` in the user folder (path overridable with `SHADPS4_BBSC_PEERS_JSON`).
    Comments are allowed in the JSON. A malformed entry is logged and skipped, never fatal.
  - With no file and no variable the map is empty and resolution is exactly as before, so this is
    inert unless it is configured.
- **Pinned P2P port in the friend package.** `start-bbsc.ps1` never set `SHADPS4_P2P_PORT`, so the
  OS handed out a fresh ephemeral port on every launch and no fixed entry could describe that
  player. It now pins 41001 (`-P2PPort` to change it). The two-instance launcher already pinned
  41001/41002.

### Shipped disabled
Compiles and installs, **never exercised in a live session**, and the config was pulled back out
before the first test round. The broker lookup it replaces was never the thing failing - our own
shadnet answers it instantly - so this was the right diagnosis applied to the wrong problem; the
outage was fixed by serving our own ss.info instead. It also scales badly: every player needs an
entry for every other player, on every machine, re-edited whenever an address changes.

The code stays in (inert with no config) for the case where NAT or routing makes the broker answer
wrong. To turn it on, drop a `bbsc_peers.json` in the user folder; the log line to look for is
`BBSC direct peer '<npid>' -> <addr>:<port> (no broker lookup)`.

### Still blocked
`thehuntersdream.com:20443` (the Sony `ss.info` server-info file the game fetches before enabling
anything online) accepts TCP but never responds, on both 18671 and 20443, while the site on 80/443
is fine. It returned `200 OK, 15172 bytes` earlier the same day. Nothing in this entry affects it —
the game gates on it before any peer connection is attempted.

## 2026-09-20 — Tester round: solo respawn, HP penalty, quit to title

Reported by the friend testing BBSC (with his own DLL proof of concept for the HP penalty):
HP penalty survives death, solo play cannot respawn, the cooperator stays when someone goes to the
menu (both have to close the game), guest does not respawn after a second death.

### Fixed (build `9079F27CEC4712B0`)
- **Solo respawn.** The death step's reload call is retargeted whether or not a session exists, but
  `BbscSpectateDeathHook` only asked for the reload back when one did, so a solo death left the
  player dead with camera control only. The no-session branch now requests it too.
- **HP penalty (SpEffectParam 9006, maxHpRate 0.7).** New `SHADPS4_BBSC_NO_HP_PENALTY` (default
  on): every second the local player's effect list is walked (chr+0x1C8 manager, +0x08 first node,
  node+0x40 id, +0x48 param, +0x58 next) and a 9006 node with a rate below 1 has its rate
  (param+0x10) set to 1.0, with maximum HP at [[chr+0x3B0]+0x20]+0xFC scaled back up by the rate
  that was in force. Both are signature-checked, so wrong offsets write nothing.
- **Quit to title.** `RegistReturnTitle` (0x0138B990, the Lua step behind the menu's quit choice,
  paired with `Check_ReturnTitle` 0x0138BAD0 and `OnReturnTitle` 0x0138BAF0) is hooked, and
  `Core::Bbsc::SetPersistentSessionPatches(false)` suspends every persistence patch for 20 s so the
  vanilla teardown runs for both players. They go back on afterwards.
- **Partner gone safety net.** A live session with no remote character for 90 s suspends the
  patches the same way, so a crashed or disconnected partner can no longer hang the session.

### Confirmed working
- **Boss kill keeps the co-op** (user test with a friend, 2026-09-20): the `BlockClear2` chain
  hooks (`_1` arms the death-lock clear, `_3Leave` skipped while the session is alive, `_3` skipped
  for a summoned guest) hold - both players stay in the session after the boss dies. This had been
  implemented but never verified in play.

### Added
- `Core::Bbsc::SetPersistentSessionPatches(bool)`: applies or reverts every enabled site at
  runtime, only after the original verification passed.

## 2026-09-20 — Lamp usable in co-op; server bound to every adapter

### Evidence
- Lamp prompt gate 0x012F5C40 writes four availability bytes and only shows the prompt when all are
  zero (0x012F83D3..0x012F83F0): +0x48 (message 0x15F95), +0x49 (message 0x15F90, set at 0x012F8323
  when the session has members or [global+0x16F8]+0x14 > 1), +0x4A (lamp object state
  [obj+0x270] == 0), +0x4B (result of the WorldChrMan scan 0x0191B6E0 at 0x012F83BC).
- shadp2p's seamless hook already clears +0x49 for the host, so +0x4B was the remaining blocker.

### Changed (build `D318E15D267C7686`)
- `SHADPS4_BBSC_LAMP` is on by default: the `Lamp.CharacterGate` retarget (0x012F83BC -> return
  zero) clears +0x4B so the host can use a lamp inside a co-op session.
- `server/shadnet.cfg`: `Host=0.0.0.0` (was the Tailscale address), so the server answers on
  Radmin/Hamachi/LAN adapters too. Old file kept as `shadnet.cfg.bak-tailscale`.
- `server/oracle/setup-server.sh`: unused for now (Oracle is blocked in the user's country), kept
  for a future always-on host: builds shadnet on Ubuntu, installs both services under systemd,
  opens 31313/31314/31315 and keeps the account service on localhost.

### Open: warping together
- Log evidence (2026-09-19): during a live session the HOST keeps polling `/summon_messenger/get`
  (42 requests) while the guest only ever sends `delete` after being summoned. So the guest has no
  channel that tells it the host changed map; following a host warp needs either the host to
  re-advertise in the new map (and the guest to re-join through the existing connect-first path) or
  a new source for the host's map.

## 2026-09-20 — Fog walls: finding what actually enables them

### Evidence
- `Lua_MultiWall` (0x0138B720) shows a wall with `0x013CABD0(id, 1)` (draw) + `0x013CA580(id, 1)`
  (collision); it is already redirected to `Lua_InvalidMultiWall`, yet the walls between areas
  stayed (run 2026-09-17), so the walls are enabled by another caller of that pair.
- Callers of `0x013CA580`: `0x013807E0` (Lua, entity 10000/10001+), `Lua_MultiWall` /
  `Lua_InvalidMultiWall`, the EMEVD handlers `0x017C46E0`, `0x017C3530`, `0x017B93A0`, and the thunk
  `0x0132DEC0`. The event-script helpers `CalcGetMultiWallEntityId` (0x01338840, entity id =
  block*100000 + area*10000 + id%10000), `SetMultiWallMapUid` (0x013388D0) and `GetMultiWallNum`
  (0x013389D0, returns -1) are registered at 0x01344135..0x013442D8.

### Added (build `19295BDF9094F72F`)
- `SHADPS4_BBSC_TRACE_WALLS` (default on): hooks the object draw/collision pair and logs every
  entity that is ENABLED while a session is live, once per entity, with the calling code offset.
  This identifies the fog walls by id and names the system that enables them.
- `SHADPS4_BBSC_WALL_IDS=<id,id,...>`: refuses draw and collision for those entity ids, so a
  candidate list can be tested without a rebuild. Boss gates are never listed.
- `tools/ghidra-scripts/ListingAt.java`: new `range:<start>:<end>` and `qword:<addr>:<count>` modes.

## 2026-09-20 — Death without reload: the step now runs, only its reload is dropped

### Evidence (test run 2026-09-19, host + guest log bundles in `test-results/`)
- Cross-map join confirmed working: guest in Old Yharnam (`0x17000000`), host in Central Yharnam
  (`0x18010000`), one load, then `join teleport: guest moved from (-192.94, -28.65, 68.57) to the
  host at (-192.69, -28.65, 68.24)`. Boss kill kept the session.
- First death: `SoloPlayDeath_2 held` -> revived, warped to the last lamp. Second death: only
  `reinsert v8: kept death` — the hook never fired again, the player kept camera control only, and
  `OnRoomTimeOut` ended the session 60 s later. Both runs ended that way.
- Cause (Ghidra, `SoloPlayDeath_2` 0x01381DE0 / `PartyGhostDeath_2` 0x013824C0): the step releases
  the death UI objects at `global+0x1690/+0x1698/+0x16B0/+0x16B8`, sets the event state, drops the
  blood echoes (`0x013D2910`), picks the reload destination (`0x01332BC0`) and only then requests
  the reload (`0x013CDE30`). Skipping the whole step left those objects allocated, so the death
  script could not run a second time.
- `dropped 0 blood echoes`: the manual drop read the count after the game had already zeroed it.

### Changed (build `C124D1BD6DED1461`)
- New patch group `Spectate` (`SHADPS4_BBSC_SPECTATE`, default on): the `call 0x013CDE30` inside
  `SoloPlayDeath_2` (0x01381EDE) and `PartyGhostDeath_2` (0x01382663) is retargeted to the
  return-only stub. The death steps now run in full; only the world rebuild is dropped.
- `BbscSpectateDeathHook` no longer skips the step (no skip flag on the hook) and no longer
  computes the echo condition; `BbscRespawnInPlace` no longer drops echoes itself — the step does.
- Release path (`nobody alive` / `session ended` / safety limit / no partner at the death step)
  calls `0x013CDE30` directly through `RequestBbscDeathReload`, reproducing the vanilla reload.
- `BbscRespawnInPlace` also clears `global+0x1520` (forced warp home, set by `PartyGhostDeath_2`)
  and `global+0x1524`.
- `OnLeave_Limit` (0x0138A4A0) moved out of `NoFogWalls` into its own group `LeaveLimit`
  (`SHADPS4_BBSC_SKIP_LEAVE_LIMIT`, default OFF): skipping it removed the only path that sends a
  guest home, which is why players could not leave the world.

### Expected
- Dying twice (and more) keeps working; menus and ladders usable after each respawn.
- Blood echoes stay where the player died.
- Leaving the co-op area sends a guest home again (vanilla behaviour).

## 2026-09-17 — Cross-map summon: connect first, one load into the host's map

### Evidence
- Cathedral Ward host + Central Yharnam guest (BBSC on, and again with all BBSC off): the server
  prepared the cross-map summon (`preparing cross-map ... placement bytes 53`), the guest teleported
  to Cathedral Ward and the fork re-rang the bell (`responder resumed ... after 1 native attempt`),
  but the guest never sent another `/summon_messenger/create`; host searches kept returning 0.
- Broker logic (shadnet-p2p `bloodborne_summon_broker.cpp`, read, not rebuilt): a search with a host
  placement marks a sign in another map `Preparing` and hides it; `Advertise` returns only the
  placement while the advertisement's `AreaId` differs from the host map, and delivers the claim
  (with placement) once it matches. Only `AreaId` is compared.

### Changed (build `22FC9AF2ED5483C6`)
- `SHADPS4_BBSC_CONNECT_FIRST` (default on): a placement header for another map remembers the host
  map (cleared on session state 4/6 or after 10 min); the teleport-first warp
  (`ApplyPreMatchCrossMapGuestWarp`) is not run; `/summon_messenger/create` bodies get
  `"AreaId"` rewritten to the host map (Content-Length recomputed). The join-time handoff
  (`ApplyCrossMapSummonGuestPlacement`) then loads the guest into the host map; the join teleport
  moves it beside the host.

### First run and fix (build `4316880F42766DF0`)
- Connect-first joined in ~70 s (`AreaId 402718720 -> 402653184`, claimed, signaling, JoinRoom) and the
  guest loaded Cathedral Ward (`StageTransition map_id 0x18000000`, summoned placement ≈ (15.6,-9.5,101.7)),
  but spawned at (0,-22,0) under the level (grey screen): the handoff hook logged 8x
  `received_placement_not_ready` (state+0x1648 never set on this path) and a header-less advertisement
  reply had cleared the transported placement (`transported_host:false`).
- The prepare reply's placement is now kept (`bbsc_connect_first_placement`, 10 min) and used by
  `ApplyCrossMapSummonGuestPlacement` instead of waiting for state+0x1648, so the forced summon
  placement (map, position, orientation, select, StageTransition) is applied for the join load.

### Second run and fix (build `14513CB936A4C09F`)
- Handoff now `applied` (CY → CW, transition started), host summon data rewritten to CW, but the guest
  still spawned at (0,-21.5,0) and fell; the in-place respawn to the partner fell again (grey).
- Every correctly placed same-map join capture has the received placement block filled:
  `state+0x1620` x,y,z,1.0; `+0x1630` 0,heading,0,0; `+0x1640` area; `+0x1644` map; `+0x1648` = 1.
  Both cross-map runs had it all zero (the claim in the advertisement reply that fills it never
  arrives before the signaling join). Connect-first now fills that block from the kept host placement.

### Third run and fix (build `2289CB4560D5F213`)
- Build 14513CB9: no "received placement filled" line, and at `SummonedMapReload.Native` the block
  was reset by the game (area/map -1, flag 0), so a fill at handoff time is too early.
- The fill moved to the entry of the native summoned reload `0x0131E5C0`
  (`BbscFillConnectFirstReceivedPlacement`), with a log line for every skip reason.

### Fourth run and fix (build `0756635CA1C315E4`)
- Build 2289CB45: `received placement filled` and the guest spawned beside the host in Cathedral Ward,
  but only on the guest: missing floors/buildings, blurry textures, no lamp, red flashes.
- Timeline: handoff `applied` → forced `StageTransition` (CW) → 1.5 s later the native
  `SummonedMapReload.Native` started its own load. The first connect-first run (no forced warp) shows the
  native summoned reload alone already loads the host map. Connect-first now returns
  `connect_first_native_reload` from the handoff: one native load only.

### Fifth run and fix (build `626A3941D4D3545A`)
- Build 0756635C: guest in Cathedral Ward with Central Yharnam lighting (red sunset, red statues and
  trees), low-detail buildings. The game had filled the received block itself with the guest's own
  Central Yharnam spot (map 0x18010000), the fill logged "already filled by the game" and dropped the
  kept placement; 5 ms later the handoff (no connect-first placement any more) applied the forced
  warp to Cathedral Ward on top of the native reload.
- `BbscWriteConnectFirstPlacement` writes the host placement into the summoned block (0x14C4/0x14D0/
  0x14E0) and the received block (0x1620..0x1648) at the handoff and again at the summoned reload,
  overwriting a block that names another map. The placement is kept until `BBSC.Type.ReloadFinish`
  sees the guest in the host map, so the forced warp never runs during a connect-first join.

### Sixth run (build 626A3941) and next attempt
- One native load only (`placement written at handoff`, `summoned reload finished in host map`), still
  Central Yharnam lighting and low-detail pieces in Cathedral Ward. The guest's save without co-op
  renders Cathedral Ward normally, so it is the cross-map join.
- Sign placement block `state+0x14A0` pos / `+0x14B0` rot / `+0x14C0` map still named Central Yharnam.
  Now also written with the host placement (HYPOTHESIS: the load takes the area setup from it).
  Built; install pending (game was running).

## 2026-09-16 — First two-PC session: echo drop on death, boss kill keeps the co-op

### Evidence (build ABDEC6181F484B9C, host + friend over Tailscale; the friend's VPN blocked P2P)
- Join, teleport, deaths and respawns worked. Bugs: (1) the dead player's blood echoes were not
  dropped; (2) after killing the boss the guest got "The final battle has ended. It is time to bid
  farewell.", left the room (`sceNpMatching2LeaveRoom`, `RemoveMember` x2, state 0) but stayed in the
  host's world without a character; the host saw the guest vanish.
- Echo drop (**CONFIRMED** statically): `SoloPlayDeath_2` calls `0x013D2910([SprjEventMan+0x80], chr, 1)`
  unless `global+0x1649` / event `+0x14`; it records the bloodstain from the current transform, zeroes
  echoes and spawns the stain. Skipping the step skipped the drop. `PartyGhostDeath_2` only drops
  for invaders (types 2/0xC).
- Boss clear chain (**CONFIRMED** statically): `BlockClear2` → `_1` → (guest) `_2` → `_3Leave`
  (Stop + remote cleanup) and `_3` (StageTransition home). `BlockClear2` marks all slots leaving.

### Changed (build `D1E57D8A77E17CA2`)
- Respawn in place first calls `0x013D2910` (host and guest) when the held step would have dropped.
- Hooks: `BlockClear2_1` arms the death-lock clearing; `BlockClear2_3Leave` skipped while the session
  is alive; `BlockClear2_3` skipped for a guest (chr type 1). Rewards in `BlockClear2`/`_2` still run.
- Friend package RAR `BBSC-friend-test-build-D1E57D8A.rar`.

## 2026-09-16 — Working features on by default (no PowerShell needed)

Tester feedback: too many environment variables; the build should run from a launcher as is.

### Changed (build `ABDEC6181F484B9C`)
- `Core::Bbsc::SwitchEnabled` (`bbsc_persistence.h`) replaces the three env-flag helpers
  (`bloodborne_re.cpp`, `http.cpp`, `bbsc_persistence.cpp`). Unset variable -> default; "0" -> off;
  anything else -> on. Default ON: `SHADPS4_BLOODBORNE_SEAMLESS_COOP`, `_RE_TRACE`,
  `SHADPS4_CAPTURE_BLOODBORNE_SUMMON`, `SHADPS4_BBSC_PERSIST`, `_PERSIST_MATCHCHECK`, `_REJOIN`,
  `_PERSIST_TASKS`, `_PERSIST_HOSTDEAD`, `_SPECTATE`. Still OFF: boss, lamp, reinsert, no-header,
  no-guest-hooks, phantom flags, rejoin v6. All features still install only for CUSA03173 01.09.
- Friend packages: `PLAY.cmd` removed; SETUP step 9 is "select the BBSC dev build and start".
  `start-bbsc.ps1` / `PLAY-BOSS-LAMP.cmd` remain for the experimental boss/lamp mode and `-Baseline`.

## 2026-09-16 — Respawn at the real lamp, guest joins beside the host, skip HostDead

### Evidence (build 581D1D0A2AA521F5)
- Ladder still blocked for the guest after the host's death although the slot leaving flag
  (`mask=0x1`) was cleared. The guest's state-diff baseline was taken while the host was already
  dead (it showed `+0x70` and `+0xA9` going 1 -> 0), so it could not show the cause.
- Players respawned at the save/continue position: the old capture stored the position after a
  load of type 5 (continue from title), not a lamp.
- Respawn point (**CONFIRMED** statically): `global+0x1528` = last lamp event point id
  (`0x01332BC0` reads it at `0x01332C2F`; lamp travel `0x019423A0` writes it). The placement step
  resolves it with `0x0154AFC0(map list, &pos, &rot, id)`; in normal play `0x01581BE0` calls it
  with map list `[0x0553B148]`. Only points in loaded map blocks are found.
- In-map event warp `0x013CB3D0` (raw listing): `vtable[0x5F8](chr, &pos, &rot, 1)`, then
  `chr+0x1E1 |= 2`.

### Changed (build `25AD116AA1F1B37B`)
- `HostDead` group: `HostDead` (`0x01381870`, prologue `55 48 89 E5 41 57`) now returns at entry
  too, so the guest never gets flag 4047, death-pending, the host-dead message or EventMenuBrake.
- Respawn in place target: last lamp via `0x0154AFC0([0x0553B148], ..., global+0x1528)` -> living
  partner -> own body. The load-position capture (and its placement hook use) is removed.
- New `TickBbscJoinTeleport` (with `-Spectate`): once per session, when the guest (state 6,
  `global+0x1592 == 0`) is loaded and the host's synced position has been stable for 2 s, warp the
  guest 1 unit beside the host.
- State diff: baseline only when every character in WorldChrMan is alive with no death flag.

## 2026-09-16 — Ladder after host death: slot "leaving" flags, state diff

### Evidence (build 7D59ABE65BC168E1)
- The guest can use the ladder near the Central Yharnam lamp before any death, not after the host's
  death. Lock clearing ran on both machines (`menu brake=false death pending=true event flag
  4047=true`): the menu brake was never set, so it is not the blocker.
- Death handler `0x018C49F0` on a REMOTE dying player (raw listing, **CONFIRMED**): record id
  `[vtable[0x1C8]()+0x10]`; id 1 (the host) -> `0x014EB860` marks **every** slot leaving
  (`[0x0553B130]+0xA9+slot*2 = 1`), other ids mark only their slot (`0x018C4E30`). This call
  (`0x018C4DE8`) is not patched. Reader `0x014EBA90(id)` = "is this player leaving"; also read by
  `0x01316640` (OnLeave, returns early), `0x01E50A70` (member instance step), `0x0178CFEB`.
  Cleared only by slot reset `0x014E7C20`/`0x014E7C50` or member-left `0x0178ABF3`.
  Blocking the ladder: **HYPOTHESIS** (no direct caller of `0x014EBA90` found).

### Added (not yet installed: game was running)
- `TickBbscDeathLockClear` also clears `+0xA9+slot*2` for every slot whose record id is not -1.
- `TickBbscStateDiff`: baseline every 3 s of slot manager, MenuMan, `global+0x1400..0x1800`,
  WorldChrMan, local chr (0x600), `chr+0x58` control (0x300) and its `+0x38` flags (0x100); 6 s
  after a respawn in place / partner revive logs every changed byte (`BBSC state diff ...`).

## 2026-09-16 — Guest could not use ladders/levers after a host death: death-flow locks

### Evidence (build 200F8713D18006C0, host died outside a boss fight)
- Host log: `SoloPlayDeath_2 held`, `respawn in place: revived (hp 1900) ... last lamp`; guest log:
  `partner revive: remote player copy revived (hp 1900, session state 6)`; host kept both member
  tasks (`mode=3`, `mode=1 cur=6`). Co-op worked, but the guest could not climb ladders.
- Decompiled `HostDead` (`0x01381870`, **CONFIRMED**), the step that runs on the partner of a dead
  host: sets event flag 4047 (`0x0132E5A0`, ESI `0xFCF`, EDX 1), calls `0x01386960` (host-dead
  message, `[0x0553B130]+0x70 = 1`), schedules `HostDead_1` (already skipped) and **`EventMenuBrake`**
  (`0x01385EA0`: close menus `0x0176A900(3)`, `MenuMan[0x05562878]+0x2DC = 1`).
- `+0x2DC` is bit `0x100` of the menu-busy mask `0x0176B7E0` (**CONFIRMED**); only the load-time
  menu reset (`0x01769405`) or the setter `0x0176BBE0` clear it. `[0x0553B130]+0x70` is cleared only
  by the player create `0x01939470`. Flag 4047 is checked by the player update `0x018FEF50`.
  Without a reload all three stay set (**STRONGLY INDICATED** as the ladder/lever blocker).

### Added (build `7D59ABE65BC168E1`)
- `TickBbscDeathLockClear` (with `-Spectate`): for 15 s after an own respawn in place or a partner
  revive, while the session is active, nothing is loading and the local player is alive, clears
  the menu brake, the death-pending byte and event flag 4047. Logs
  `BBSC death locks cleared: ...` when it changes something.
- `tools/ghidra-scripts/ListingAt.java`: raw listing (`fn:`), data references with a filter
  (`refs:`), and constant scan (`scalar:`).
- `build-and-install.ps1` now points to `PLAY-HOST.cmd` / `PLAY-GUEST.cmd`.

## 2026-09-16 — Pre-test verification pass (partner revive, warp transform)

Checked against raw disassembly before testing:
- `BbscRespawnInPlace` vs `0x013CBAD0` player branch: every offset, mask and call order matches
  (`chr+0x208`, guard `+0x280/+0x80`, `vtable[0x5F8]`, `+0x1E4 |= 2`, `Idle_wild` at `0x04992C40`,
  `vtable[0xA8]`, `0x018BCD40`, `+0x48 &= 0xFC`, `WorldChrMan+0x68`, draw `+0xC = 1`,
  `+0x114 &= 0xFFFE`). Position w constant `[0x04925D14]` = 1.0, as used.
- The event revive also calls `0x013CB4F0(10000, &pos, &rot, -1)`: decompiled, it only overwrites
  pos/rot with the map's default respawn region (type 4) — our captured last respawn is used instead,
  no side effect lost. The `global+0x1450/+0x1460` transform write is now mirrored.
- `0x018BCF30(chr, flag)` (physics reset) reads position/rotation from
  `[[[[chr+0x58]+8]+0x3B0]+0x68]+0x1E0/+0x1D0` — **CONFIRMS** the transform chain used for capture.
- Death handler remote branch (`0x018C4DBF..0x018C4E30`): a dying REMOTE player only sets the slot
  bookkeeping flag `[0x0553B130]+0xA9+slot*2` (or `0x014EB860` when the record id is 1); it does not
  remove member tasks.
- Skip trampoline in `InstallGuestCodeHook` re-read: flags/rax/stack handling correct; the previous
  run proved the skip at runtime (the held death did not reload).

### Added
- Partner-side revive (`TickBbscRemoteRevive`, with `-Spectate`): a remote player whose synced HP is
  > 0 for 1 s while its death flag is still set gets the event revive's non-local steps
  (`0x018BCF30(chr,1)`, `Idle_wild`, opacity, death flags, drawing). Fixes the dead-looking copy the
  partner keeps after an in-place respawn (the guest only saw the host's name).


## 2026-09-16 — Host death kick found; revive rebuilt from the game's own event revive

### Evidence (build 6A164E1924800BD4, host died, no boss)
- The respawn path ran (`SoloPlayDeath_2 held ... respawn in place: revived (hp 1900), warped to last lamp`),
  but the host was invisible and could not move, the guest only saw the host's name, enemies healed
  from the guest's damage, and the host could not see the guest.
- `RemoveMember` fired twice right at the host's death, from `0x018C4D82`. Decompiled
  **PlayerIns death handler `0x018C49F0`** (**CONFIRMED**): sets HP 0 and death flags
  (`[chr+0x58]+0x114 |= 1`, `[[chr+0x58]+0x38]+0x48 |= 1`, `+0x1E1` bits), sets `global+0x1522`
  (and `+0x1523` for falls), and **when the local player is the host (state 3) calls RemoveMember for
  every member (`0x018C4D7D`), the twin removal (`0x018C4DAC`), and marks every remote slot leaving
  (`0x014EB860`, `0x018C4DBA`)**. That is the native host-death kick.
- `0x01900710` restores HP but not the death flags, which explains the invisible, frozen host.
- The game's own event revive **`0x013CBAD0(10000, ...)`** (**CONFIRMED**): `chr+0x208 = (1.0, 1.0)`,
  warp `vtable[0x5F8]`, `chr+0x1E4 |= 2`, `Idle_wild` animation (`0x01A19F60`, UTF-16 string
  `0x04992C40`), `vtable[0xA8]`, HP to max (`0x018BCD40`), clear `+0x48` bits and `+0x114` bit0,
  `WorldChrMan+0x68 = 0`, draw flag `[[[chr+0x3B0]+0x58]+0x28]+0xC = 1`, then
  `CSMultiPlayMan::Stop`.
- Guest death steps before `_2` (`PartyGhostDeath`, `_wait`, `_1`) only post event messages and set
  `[0x0553B0D8] = 2`; they do not end the session.

### Changed (build `EB516427232C8933`)
- `HostDead` persistence group also retargets the three host-branch calls in the death handler
  (`0x018C4D7D`, `0x018C4DAC`, `0x018C4DBA`) to the return-only function.
- Respawn in place now copies `0x013CBAD0` step by step **without** its Stop call, then clears
  `global+0x1522/+0x1523` and resets `[0x0553B0D8]` to 0. It never forces type 0.
- If the respawn in place fails, the held death step is released (normal reload) instead of leaving
  the player dead.
- Code reviewed by reading the persistence file and the whole death-rules section of
  `bloodborne_re.cpp`; stale comments and the unused revive constant removed.


## 2026-09-16 — Seamless Co-op death rules (ER / DS3 / DSR model)

### Research
- All three Yui mods: outside boss fights a dead player respawns at their last checkpoint in the same world,
  the world is not reset, the partner keeps playing; the world resets for everyone only when someone rests.
  During a boss fight the dead player spectates until the boss dies or everyone is dead.
- Bloodborne pieces (**CONFIRMED** statically unless noted):
  - Boss fight active: `SetBossGauge` (`0x01336C80`) stores up to 3 bosses at
    `[0x05562878]+0x1658+i*0x14` (+0x165C boss character handle); `ClearBossGauge` (`0x01336D00`)
    resets them to -1. A boss health bar on screen == a slot != -1.
  - The game's own warps (`Lua_Warp_1` `0x0138C570`) always go through StageTransition, i.e. a load.
  - In-map teleport without load (event warps `0x013CB3D0`, `0x013CB870`):
    `chr->vtable[0x5F8](chr, &pos, &rot, 1)` then `chr+0x1E1 |= 2`.
  - Revive `0x01900710(chr)` (called by `OnSpEffectRevive`): HP to max, state reset; also writes
    type 0 to `chr+0x78` and the slot.
  - Respawn point resolution: `0x0154AFC0(map_list, &pos, &rot, point_id)` and
    `0x0154AA70` (default respawn region type 4), called from the placement step `0x01939C80`.
  - Character position (**STRONGLY INDICATED**, from the event warps):
    `[[[[chr+0x58]+0x08]+0x3B0]+0x68]+0x1E0` (vec4), rotation at `+0x1D0`.

### Changed (build `6A164E1924800BD4`, launchers `PLAY-HOST.cmd` / `PLAY-GUEST.cmd`)
- The held death step now decides:
  - boss bar up + partner alive -> spectate; when the bar has been gone for 2 s -> respawn in place;
  - no boss bar + partner alive -> respawn in place right away;
  - nobody alive (1.5 s) or session over -> run the held step, normal reload.
- Respawn in place: revive, restore a phantom's type (character and slot), teleport to the last
  respawn position (captured on the first in-world tick after a load of type 4 death respawn,
  9 lamp travel or 5 continue), or next to the living partner if none was captured yet.
  Positions are rejected when not finite, all zero, or beyond 20000.
- Known gaps: the camera does not follow the partner while spectating; the world does not reset
  when a lamp is used by the partner; the "YOU DIED" fade might leave the screen dark.


## 2026-09-16 — Spectate on death (ERSC-style), first version

### Research
- Elden Ring Seamless Co-op: a death respawns the player at their checkpoint without resetting the world;
  in boss fights the dead player spectates until the boss dies or everyone is dead, and no world reload
  happens. ER's session states (Host 3, Client 6, Leaving 7) match Bloodborne's. Sources: Nexus mod page,
  ersc-docs, and the open-source rewrite `micthiesen/unseamless-coop` (SESSION-LIFECYCLE-FINDINGS.md,
  SPECTATE.md).
- Bloodborne's death flow (Lua, registered in `0x0138D400`, **CONFIRMED** statically):
  `SoloPlayDeath` (`0x01381B60`) -> `_TextWait` -> `_1` -> `_2` (`0x01381DE0`) for the host;
  `PartyGhostDeath` (`0x01381FD0`) -> `_wait` -> `_1` -> `_2` (`0x013824C0`) for a guest;
  `HostDead` -> `HostDead_1` for the partner of a dead host. The first step schedules the final step
  on a timer via `0x0132B1C0(delay, event, 4000, "<name>_2", ...)`. The final step frees the own-world
  save snapshot (`global+0x1698/+0x16B8`), selects the respawn point (`0x01332BC0`) and runs
  StageTransition. Full list of 226 registered Lua C functions saved while researching.
- Note: `PartyGhostDeath_2` frees the guest's own-world save snapshot. The current guest-death flow
  runs it while the guest stays in the host world — to be checked for save impact.

### Changed
- `InstallGuestCodeHook` gained an optional skip flag: if the hook sets it, the trampoline returns to
  the caller instead of running the function (function entry only).
- Spectate (`SHADPS4_BBSC_SPECTATE`, launcher `-Spectate`): hooks `SoloPlayDeath_2` and
  `PartyGhostDeath_2`. While a partner in WorldChrMan still has HP, the step is skipped and its event
  object saved: the dead player stays dead, nothing reloads, the partner keeps playing. When no
  living partner is left for 1.5 s (or the session ends, or 15 min pass), the saved step is run and
  the normal reload happens.
- `PLAY-HOST.cmd` / `PLAY-GUEST.cmd`: `-Persist -MatchCheck -Rejoin -Tasks -HostDead -Spectate`.
- Not done yet: the camera stays on the dead player's body instead of following the partner.
- Dev build `69C8E3690DA3D9F4`.


## 2026-09-16 — Host death: the guest must reload; trace who drops the guest

### Evidence (host death with keep-tasks=true, ignore-host-death=true)
- With `HostDead_1` skipped the guest stayed put, but the host still could not see him, enemies he
  damaged snapped back to full HP, and ladders/levers did nothing for him.
- The host's respawn rebuilds its whole world; the guest was left standing in the old copy. Native
  Bloodborne never reloads a host world with a guest inside, so there is no path to move a guest
  into the new copy without a load (**STRONGLY INDICATED**). Conclusion: on a host death the guest
  has to reload; the goal is one short load that puts him back beside the host.
- Host log: right at the death, before the load, the guest's task went to WaitExit with the exit
  flag (`cur=8 flags=0x1e`) and the host's own task disappeared; after the respawn `ins=[]`. The
  tasks were told to exit individually, so the `Tasks` patch (bulk destroy) did not help.
- The exit flag is set by `CSMultiPlayMan::RemoveMember` `0x01E54DB0` (**CONFIRMED** statically).
  Callers: `0x01383D90` (Lua `RequestMenuEnd_PK_Sucess`), `0x0178A900` (x2), `0x0178B5E0`
  (MEMBER_LEFT), `0x018C49F0`, `0x01E54FD0`.

### Changed
- Trace site `BBSC.RemoveMember` (`0x01E54DB0`, with caller chain) to identify which path drops the
  guest on a host death.
- `PLAY-HOST.cmd` / `PLAY-GUEST.cmd` no longer pass `-HostDead`: the guest reloads on a host death
  again. The `HostDead` group stays available for experiments.


## 2026-09-16 — Host death: guest stays in place (HostDead_1), self-heal

### Evidence (host death, run with `keep-tasks=false`)
- The run used a launcher without `-Tasks`; every window logged `keep-tasks=false`, so the host still
  lost its member tasks (`ins=[]`) and could not see the guest.
- The host-side reinsert never armed: it hooks the skipped session-end cleanup, but a host is in its
  own world (`global+0x1592 = 1`), so the host's death transition never tries to end the session.
- The guest reloaded and respawned by the lamp. Trace: the guest's StageTransition was called from
  the Lua C function **`HostDead_1`** (`0x01381A60`, registered at `0x0138DADB`), one second before
  the host's own respawn. Decompiled (**CONFIRMED**): `[0x0553B0D8] = 2`, `global+0x1520 = 1`
  (forced warp), `0x01332BC0(lua, -1)` (home respawn point), `0x013CDE30` (StageTransition), then
  `[0x0553B130]+0x8C = [[0x0553B188]+4]` (restore the original type on the next create).

### Changed
- New persistence group `HostDead` (`SHADPS4_BBSC_PERSIST_HOSTDEAD`, launcher `-HostDead`):
  `HostDead_1` returns immediately (entry byte `0xC3`, prologue verified). The guest stays where he
  is with no loading screen when the host dies. New patch type "entry return" in
  `bbsc_persistence.cpp` for Lua C functions whose whole job is a session-ending action.
- Self-heal on host and guest (`TickBbscSelfHeal`, with `-Rejoin`): a member task at CreateWait
  whose character has been missing for 4 s while no load is running gets flag bit0 +
  `SetState(4)`. This rebuilds the guest on the host's screen after a host death.
- `SEAMLESS-DEATHS` launchers now enable `-Persist -MatchCheck -Rejoin -Tasks -HostDead`.
- Risk to watch: the guest no longer reloads on a host death, so enemies the host's reload resets
  may stay dead or out of place on the guest's screen.
- Dev build `617981FE787F015D`.


## 2026-09-16 — Host death: keep the member tasks, reinsert on the host side

### Evidence (host death on build BD3252C2B25FC023)
- The session survived the host's death and the guest stayed in the host world. The guest could see
  the host; the host could **not** see the guest — the mirror image of the old guest-death bug.
- Host log after the respawn: `state=3 phase=4 ... ins=[]`. The stage task destructor
  (`0x019470F0`) destroys every member task through `0x01947207 -> 0x01E54830` whenever the summon
  reload phase is not 3, which is exactly a host death (**CONFIRMED**). With no task left, nothing
  recreates the guest's character.

### Changed
- New persistence group `Tasks` (`SHADPS4_BBSC_PERSIST_TASKS`, launcher switch `-Tasks`): the
  destructor's `0x01947207` call is retargeted to the return-only function, so the member tasks
  survive a kept load.
- The v8 reinsert pass now also arms when the session state is 3 (host). It rebuilds the guest's
  character after the host respawns; no phantom type or summoned reload is involved on that side.
- New launchers `start-HOST-only-SEAMLESS-DEATHS.cmd` / `start-GUEST-only-SEAMLESS-DEATHS.cmd`
  (working co-op set + `-Tasks`). The plain `SEAMLESS-REJOIN` pair stays as the tested guest-death
  configuration.
- Dev build `F1F905979BB5CBD3`.


## 2026-09-16 — WORKING: guest death keeps co-op

First fully working guest death. With build `BD3252C2B25FC023` and the `-MatchCheck` launchers
(`start-HOST-only-SEAMLESS-REJOIN.cmd` / `start-GUEST-only-SEAMLESS-REJOIN.cmd`):

- Session survives the guest's death, the guest respawns and stays in the host world.
- Both players see each other, can lock on, and interact; enemies fight the guest and the guest
  fights enemies. No crash, no kick, no "Beckoner".

### What the last two runs proved
- The kick that ruined the previous attempt was `OnMatchingCheck` (`0x013809BC`, Stop site
  `0x013809B7`): right after a kept respawn the match-validity check (`0x01338CE0`) fails for a
  moment and the game ends the session. The `-MatchCheck` patch group already covered exactly that
  pair; it had been left off since the early crash hunt on a wrong suspicion (the panics were the
  Lua wrapper, see the 2026-09-16 v7.1 entry). Turning it on is required for guest death.
- Covenants: with an oath rune equipped, the pairing is classified as covenant business
  ("the beckoner is an Executioner, and thus an adversary"), the guest joins as an adversary and
  enemies ignore him. With both oath slots empty the guest joins as **type 1** (white phantom).
- Swapping the own-world restore type `[[0x0553B188]+4]` at the kept death keeps the respawned
  guest a phantom (**CONFIRMED**: type 1 survived the respawn in the last run).

### Known side effect
- Bloodborne lowers a summoned guest's maximum HP. After a kept death and respawn the cap is not
  re-applied, so the guest comes back at full health. Not yet decided whether to restore it.

### Next
- Host death (the guest is still sent home), then lamps, items and boss credit.


## 2026-09-16 — v8.3: swap the own-world restore type (and the covenant finding)

### Evidence (v8.2 run, oath runes removed)
- Removing the oath rune on both characters fixed the "Executioner, and thus an adversary" pairing. With no covenant the guest joins as **type 1** (white phantom, `sign=-1`) instead of an adversary — this also explains why enemies ignored the guest on a fresh summon.
- After the kept death the respawn was still built as type 0. Order of events from the log: the character is created first, then `BBSC.Type.ReloadFinish` restores the sign code, then the slot type is synced back from the character. The reset itself comes from the own-world restore (`0x015C9940` / `0x01941C20`), which writes back the saved original type `[[0x0553B188]+4]` for types 1/2/12/13.
- `ApplyPhantomCharacterFlags` on an already built character (`0x1001098 -> 0x1004098`) made the guest **invisible to himself and immune to damage**, while he could still kill enemies. The bits belong at creation time only.

### Changed
- At the kept death the own-world restore type `[[0x0553B188]+4]` is swapped to the guest's phantom type, so the game's own restore rebuilds a phantom instead of a normal player. The original value is put back once the reinsert finishes (or on timeout).
- The phantom flag write is off by default; `SHADPS4_BBSC_PHANTOM_FLAGS=1` re-enables it for testing.
- Fix: the member list walk in the slot lookup compared against the address of the list field instead of the head node, which crashed inside the id compare (`0x00CA1C94`). Member ids are now validated before every compare.
- Dev build `BD3252C2B25FC023`.


## 2026-09-16 — v8.2: phantom type at creation, phantom flags, event names

### Evidence (v8.1 run, guest killed by an enemy)
- Nearly working: full HP respawn, both players see each other, enemies fight the guest again, the guest fights enemies, the host can lock on and hit the guest. Remaining: **the guest cannot hit or lock the host**.
- Logs: `sign type 0, extra 21` at the kept death. The summon sign code `global+0x16F0` is only set during the join (observed `-3`), so the ReloadFinish restore wrote 0 and the respawn was built as type 0. The post-respawn fallback then set slot and character type to 2.
- Therefore the respawned character misses the flags StageFlow player create applies to phantom types 1/2/12 (`0x019394D5`: at `+0x1E1`, clear bit 12, set bit 14) — **STRONGLY INDICATED** cause of the one-way hits.
- Unrelated session loss earlier in the day: the guest rang the Sinister Resonant Bell (invasion) against an Executioners host; the guest's game raised the Lua event `OnMatchingError` (`0x013809F0`, registered in the table at `0x0138D400`), which does Stop + remote cleanup.

### Changed
- The summon sign code is tracked while it is set (during the join) and restored at `BBSC.Type.ReloadFinish`, so the respawned character is created as the phantom type instead of being re-typed afterwards.
- Fallback: after the respawn, phantom creation flags are applied to the character (`ApplyPhantomCharacterFlags`).
- New trace site `BBSC.LuaEvent` (`0x01339870`) names every session-related Lua event (filtered to Session/Kick/Room/Match/Multiplay/Summon/Reload/Leave).
- Session shape log now includes each member's slot index, slot type and role byte.
- Dev build `1ED253C3CB011A12`.


## 2026-09-16 — Reinsert v8.1: keep the phantom player type

### Evidence (v8 run, guest killed)
- The players see each other after the death: restarting the remote insert task at FirstSyncWait works (**CONFIRMED**).
- Still broken: no lock-on or hits between players, enemies ignore the guest, the host can push the guest.
- Logs: the summoned guest has type 2 (host 0). At the kept death, type 2 was queued, but after respawn the slot type was 0. The type was written back after the character had already been built as type 0.
- Static analysis (**CONFIRMED**):
  - StageTransition `0x013CDE30` copies the live character type `[[0x0553E878]+0x60]+0x78` into slot `+0xA4`, and `0x01947310` keeps the two in sync.
  - `0x015C9850` ApplySummonType sets the type from the summon sign code `global+0x16F0`, called from `0x0178D9C0` (phase 3 → 4). The dead-guest SummonReloadStart branches clear `global+0x16F0`.
  - `0x015C9940` and `0x01941C20` restore the saved original type `[[0x0553B188]+4]` for types 1/2/12/13 when switching to own world.

### Changed
- At the kept death, `global+0x16F0/+0x16F4` are saved. The new trace hook `BBSC.Type.ReloadFinish` (`0x0178D9C0`) puts them back just before ApplySummonType runs for the respawn.
- After the respawn, both the slot type and the live character's `+0x78` are restored if they still differ.
- Trace sites `BBSC.Type.ReloadFinish`, `BBSC.Type.ApplySummonType` and `BBSC.Type.OwnWorldStep` (with caller). The session shape log now includes `chr_type` and `sign`.
- Fix: member-id compare is `0x00CA1C70`. The earlier `0x01CA1C70` was a wrong Ghidra-base conversion and crashed on bell ring.
- Dev build `308C05316F9EA850`.

## 2026-09-16 — CSMultiPlayerIns decoded; reinsert v8

### Save
- Guest save (user 1002) restored from the user's original copy (local files, hashes verified). The previous state is kept in `save-backups\1002-before-restore-20260916-002556`.

### Evidence (Ghidra, CONFIRMED statically)
- `CSMultiPlayerIns` constructor `0x01E4F9F0(this, member_id, mode)`. Static state table `0x0558EE28`: 1 Init, 2 StartNotifyWait, 3 StartWait, 4 FirstSyncWait, 5 Create, 6 CreateWait, 7 Update, 8 WaitExit, 9 Delete, 10 DeleteWait. Fields: `+0x50` current state, `+0x54` next, `+0xC8` member id, `+0xE8` mode, `+0xED` flags. Vtable `0x0534F650`: `+0x28` SetState `0x01E52560`.
- Creators (QUESTIONS #9): the session event handler `0x0178B5E0`:
  - case 7 (create session): `Ins(self, 3)`, phase 4.
  - case 8/9 (join): phase 1, `global+0x1521 = 1`, "OnJoinMutiplay", `Ins(self, 0)`.
  - case 0xE (MEMBER_JOINED): skipped when the member is already registered in `rules+0x68`; otherwise `Ins(member, 2)` for the session owner (`SprjSessionManager+0xD0`), else `Ins(member, 1)`. This is why the MEMBER_JOINED replay did nothing after a kept death.
  - Four `CSMultiPlayMan` vtable methods (`0x01E548F0/9C0/A90/B60`) do the same with modes 3/0/2/1.
- `CSMultiPlayMan [0x05540230]` keeps the tasks at `+0x10/+0x18`. `0x01E54830` destroys them all and is called by the stage destructor when phase ≠ 3.
- FirstSyncWait creates the remote PlayerIns from its slot (`[0x0553B130]+0x10`, type `+0xA4`) once flag bit0 is set.
- StageFlow player create `0x01939470` applies `[0x0553B130]+0x8C` (pending type) to the local slot type `[[0x0553B130]+8]+0xA4`, and uses that type for the new player.
- `0x0138D400` registers the Lua event functions: SummonReloadStart `0x01389440/620` and the revive function `0x01389D20` (Stop + RemoteCleanup + "OnReviveMagic_1").

### Changed
- Reinsert v8, enabled with `-Rejoin`:
  - At the kept death, the guest's player type is queued in `+0x8C` so the respawn stays a co-op phantom.
  - After the respawn settles (alive, no transition, 2.5 s), the type is restored if it changed. Every remote member insert task (mode 1/2) with no character gets flag bit0 and `SetState(4)`, which re-runs the game's remote-player creation.
- `BBSC session shape` log (host and guest, on change): session state, phase, player type, pending type, own-world flag, and every insert task's mode/state/next/flags/character-present.
- Dev build `BE3D6DBC43248694`.

## 2026-09-16 — Save-data finding: the reload native is an own-world snapshot (v7.1)

### Evidence (v7 run, guest killed)
- v7 snapshot at death (`phase 4 -> 3`, members=2), then the death load ran. There was no crash and no kick; the phase moved to 4 at player create and the session was kept. The result was the same as v6: the host sees the guest but cannot collide or lock on, and the guest does not see the host.
- `0x0156AA50` (save-file writer, **CONFIRMED** statically): when `global+0x1592 == 0` it writes WorldChrMan, `[0x0553B168]` and decal data from the `0x0131E5C0` snapshot buffers (`global+0x1698/+0x16B8/+0x16D8`) instead of the live world.
- `global+0x1592` = "playing in own world" (**STRONGLY INDICATED**): the host logs 1, and a guest in the host world logs 0. This answers QUESTIONS #25.
- `0x0131E5C0` therefore freezes the player's own world before entering another world. v6 and v7 called it while the guest was already in the host world. That overwrote the guest's own-world snapshot, and the guest save written at 00:17:21 may contain host-world character and decal state. Restore test saves from backup.
- Re-insertion is still open. Phase 3 at death keeps the session, but the respawned guest is not re-created as a summoned client. QUESTIONS #9 (CSMultiPlayerIns creator) is next.

### Changed
- v7.1 writes only `[[0x0553D6D0]+0x84] = 3` at the kept death and never calls the snapshot native.
- `ApplyDeferredSummonReload` drops any request when `global+0x1592 == 0` (save protection, logged). The descriptor-finalize path logs a warning in the same situation, but is not blocked because the fork's join path uses it.
- Dev build `89E02F7C04EB7F20`.

## 2026-09-16 — Summon reload mechanism decoded; rejoin v7

### Evidence (v6 run, guest killed)
- v6 called the reload native 4 s after respawn (`members=2, hp=1900, phase 4 -> 3`). No load followed. The host saw the guest but could not collide with or lock onto it, and the guest could not see the host.
- About 75 s later the guest's session watchdog stopped the session at `0x0178A524`, and the host returned to the title screen shortly after (host `StageTransition` with `0x1592=1`, cause not traced yet).
- Ghidra decompilation (**CONFIRMED** statically):
  - `0x0131E5C0` does not load anything. It snapshots WorldChrMan character data, `[0x0553B168]` and WorldDecalMan decals into `global+0x1698/+0x16B8/+0x16D8`, then sets the reload phase `[[0x0553D6D0]+0x84] = 3` when SprjSessionManager state is 6 or `[[global+0x16F8]+0x98] != -1`.
  - Session watchdog `0x01789B60`, where `param_2` is the summon reload rules object: while the phase is 1–3, the timer at `+0x108` grows, and at `[0x055640E0]` it calls Stop (the `0x0178A524` kick). The same function holds OnIrregularLeaveSession, OnRoomTimeOut and OnBeThxKickOut.
  - Stage task destructor `0x019470F0`: phase ≠ 3 → Stop plus CSMultiPlayMan cleanup `0x01E54830`.
  - Load step `0x019297E0`: phase == 3 → warp to the summoned placement map `global+0x14C4`, else to `global+0x0C`.
  - StageFlow player create `0x01939470`: phase == 3 → `0x0178D9C0` (phase = 4). With state 3 or 6, fewer than 2 members and `[rules+0xC5]==0` → Stop.
- Conclusion: a summoned reload is phase 3 set **before** the stage unloads. v4–v6 set it during or after the load, which is too late.

### Changed
- Rejoin v7 is now the default, and `SHADPS4_BBSC_REJOIN_V6=1` restores the old path. At the kept death `StageTransition` (state 6, request active) it calls the native `0x0131E5C0` directly, bypassing the Lua wrapper. It runs once per load and logs `BBSC rejoin v7: summon reload snapshot taken ... reload phase X -> Y`.
- Tool: `tools\decompile.cmd` plus `tools\ghidra-scripts\DecompileAt.java`. They take BBSC offsets; the GhidraOrbis base is `0x01000000`.
- Dev build `4B860CEA041BF346`.

## 2026-09-15 — Rejoin v6: gate the reload like the game's SummonReloadStart

### Evidence (v5 run, guest killed)
- No crash. The v5 handoff worked (`lua_event_man_missing_deferred_in_world`). The deferred reload then called `0x0131E5C0` about 0.5 s later, still inside the respawn load, and nothing happened. The guest stayed in its own world, the host saw "Beckoner" where the guest died, and neither player could see the other.
- Static analysis (**CONFIRMED**): the other two callers of `0x0131E5C0` are the game's `SummonReloadStart` handlers `0x01389440` and `0x01389620`. Both are registered from `0x0138D400`, and their debug strings are "SummonReloadStart room exists, dead" and "room absent, dead". Each handler:
  - requires `SprjLuaEventMan`, reads `n = [[global+0x16F8]+0x18]` and `alive = [[[[0x0553E878]+0x60]+0x3B0]+0x20]+0xF8 > 0`;
  - `n >= 2 && alive` → tail-jumps to the reload native `0x0131E5C0` (`0x01389620` first sets `[[0x0553B130]+0x70] = 1`);
  - `n >= 2 && dead` → `CSMultiPlayMan::Stop` (`0x01389536` / `0x01389724`, **not** covered by persistence) + remote cleanup `0x015BED10`, then clears `[[0x0553B108]+0x60]+0x40` and `global+0x16F0`;
  - `n < 2 && dead` → clears the same two fields only.
- `0x0131E5C0` takes no arguments. It serializes local character data (header `0x1509170043485220`) and updates `[0x0553D6D0]` (summon reload rules).

### Changed
- The in-world reload for a BBSC rejoin request now waits for at least 2 session members, a living local player, and 3 s of stability before calling the native. It logs `BBSC rejoin v6: in-world summoned reload called (members, hp, reload phase before -> after)`.
- Dev build `4B0BF30E65B0E6BC`.

## 2026-09-15 — Guest death panic: root cause + rejoin v5

### Evidence (Test A: v4 with MatchCheck off, guest killed)
- The guest crashed anyway (`0x8020b56d2`), so MatchCheck is **not** the cause.
- The `Engine.Panic` capture shows the panic called from `0x01336BC2`, inside `SummonedMapReload`.
- Disassembly shows `0x01336B90` is a wrapper: `cmp [0x0553B0C8],0; jz → Panic(FD4Singleton.h:177)` else `jmp 0x0131E5C0`. `[0x0553B0C8]` is the `SprjLuaEventMan` singleton (**CONFIRMED**).
- The v4 rejoin armed the descriptor-finalize reload, and at the death load's descriptor finalize the singleton is null (**CONFIRMED**). v1 through v3 went through the same reload and hit the same panic (**STRONGLY INDICATED**).

### Changed
- `SummonedMapReload` is never called while `SprjLuaEventMan` is null. This applies to both the descriptor-finalize path and the fork's in-world deferred reload.
- Rejoin v5: if the singleton is missing at the death-load descriptor, the request moves to the in-world deferred reload (`pending_summon_reload_map`). That reload runs after the respawn, once the singleton exists and no transition is active. It times out after 90 s.
- Dev build `7FFFBE7215DB5A25`.

## 2026-09-15 — RE tools + panic stack capture

### Added
- Tools in `tools\`: x64dbg `2026.05.27`, Ghidra `12.0.3` with the GhidraOrbis `1.0144` extension, and Temurin JDK `21.0.12.1` (local, used as Ghidra's `JAVA_HOME`). Headless project at `tools\ghidra-projects\BB109`, where the Orbis SELF loader imports the 01.09 eboot.
- Trace site `Engine.Panic` at `0x020B55B0` (the Dantelion2 panic handler). It logs the panic file, line and format string, then the call stack two ways: the rbp frame chain and a raw scan of the stack for eboot return addresses. This targets QUESTIONS #1, the caller of the `SprjLuaEventMan` panic.
- Dev build `537CB0EFD21FF10A`.

## 2026-09-15 — Rejoin v4: join-map fallback

### Evidence (v4 NOMATCHCHECK run, host death)
- No panic with `-MatchCheck` off and no memory writes at death (**CONFIRMED** for this one run). The room stayed alive until both games were closed.
- The guest's kept `StageTransition` logged `BBSC rejoin skipped: no host placement (seamless claim header) available`. The placement is consumed at `destination_reached`, so it is usually gone by the time a death happens.
- Host death sent the guest back to its own world, and lamps stayed blocked for both players. Expected: host-side rejoin isn't implemented, and the lamp gate patch only runs with `-Lamp`.

### Changed
- `ArmBbscRejoin` falls back to `bbsc_joined_host_map`. That map is recorded at `SummonedMapReload.Native` (`0x0131E5C0`) while `CSMultiPlayMan` state is 4 or 6.
- Dev build `9029D12ABC269B60`.

## 2026-09-15 — Guest rejoin v4 + crash isolation

### Evidence (v3 run)
- v3 wrote only the summoned placement banks and armed the descriptor reload; panic again ~15 log lines after the kept death, before any descriptor finalize (lines 218573–218594).
- Two suspects remain: (1) any write to the summoned placement banks during a summoned client's death load; (2) the `-MatchCheck` patch — all three crashing runs had it, all three non-crashing kept deaths (15:45, 20:45, 21:07) did not.

### Changed
- v4 writes no game memory at death; it only arms the descriptor-finalize reload and suppresses that reload's placement refresh (`bbsc_rejoin_suppress_placement_refresh`).
- New `start-two-instances-SEAMLESS-REJOIN-NOMATCHCHECK.cmd` for suspect (2).
- Dev build `d9e55964…`.

## 2026-09-15 — Guest rejoin v3

### Evidence
- v2 test (22:2x): stale-record cleanup worked (`error=0 status=200`), join OK. On guest death v2 redirected the load (setters + forced-warp byte + selector) and the engine panicked immediately with the same `SprjLuaEventMan: accessed an uninitialized singleton`, before any descriptor finalize. v1 and v2 share only the forced-warp step as a summoned client; earlier kept deaths without it (15:45, 20:45, 21:07) never crashed.

### Changed
- v3 does not set `+0x1520` nor call the selector. For a same-map respawn (`+0x0C` == host map) it only writes the placement banks and arms the descriptor-finalize `SummonedMapReload`; cross-map respawns are skipped with a log line. Timeout no longer touches `+0x1520`.
- `start-*-SEAMLESS-REJOIN.cmd` now `-Persist -MatchCheck -Rejoin` only (boss/lamp off while isolating).
- Dev build `7b79d909…`.

## 2026-09-15 — Stale seamless summon records

### Root cause (CONFIRMED from server log 22:09–22:13)
- The seamless `SummonBroker` retains Preparing/Claimed/Delivered records for 15 min and ignores ordinary removals. Bloodborne reuses the same `SessionId`/`UserId` after an emulator restart. After a crash or closed session, the host claimed the leftover record **before** the guest advertised (room 2 at 22:09:12, guest advertised 22:09:20) and the guest kept receiving a stale claim and never joined. Tests right after a server restart joined first try (20:43, 21:02).

### Fixed
- `http.cpp` `ClearStaleSeamlessSummonRecord`: before the first seamless `/summon_messenger/create` of the process, send `/summon_messenger/delete` with `Force:true` for the same `SessionId`/`UserId` (once per process, so in-session cross-map preparation is unaffected). Verified the broker accepts it: manual forced removal returned `consumed 1 retained 0`.
- Dev build `5d717721…`.

## 2026-09-15 — Guest rejoin v2 (after v1 crash)

### Evidence
- Rejoin v1 test: guest killed → own-world load → v1 dispatched a second `StageTransition` ("summoned reload 0x18010000 → 0x18010000") → request acknowledged → engine panic `Dantelion2 Panic … SprjLuaEventMan: accessed an uninitialized singleton` (`DL_PANIC`, crash at `0x020B56D2` inside the panic handler `0x020B55B0`). Host then returned to the title screen.
- Unrelated transient: `permission denied` opening `home\1002\savedata\…\userdata0000` during the save swap (files not read-only; likely a scanner lock).

### Changed
- Rejoin no longer starts any load. v2 acts inside the game's own death `StageTransition` (observed through the persistence no-op cleanup hook while the request byte is armed): writes the claim's host placement, calls the forced placement setters + forced-warp selector, and arms only the descriptor-finalize `SummonedMapReload` request, so the game's own respawn load becomes the summoned reload into the host world. Member replay is not armed for a redirected load.
- Tick only reports consumption, or after 20 s clears the request and the forced-warp byte `+0x1520` so later lamps/respawns cannot use a stale forced map.
- Dev build `69ba2fca…`.

## 2026-09-15 — Guest rejoin after death (experimental)

### Evidence
- Seamless + stale-placement fix: joins 2/2 (21:02:07 in 3 s, 21:04:57 in 0.4 s). Guest death kept the session and respawned on the host map, but reloaded the guest's OWN world: host saw the guest labeled "Beckoner" with no collision; guest saw no host; member replay (reinsert) had no effect.

### Added
- `SHADPS4_BBSC_REJOIN=1` (`-Rejoin`): when a kept session end happens while this process is a joined guest (CSMultiPlayMan state 6), wait for the own-world load to settle (`SHADPS4_BBSC_REJOIN_DELAY_MS`, default 4000), then rerun the fork's in-room cross-map sequence toward the claim's host placement: `WriteTransportedSummonPlacement` → forced placement setters → `SelectSummonedPlacement(0,-1)` → pending summon reload maps → `StageTransition`; the existing descriptor-finalize hook performs `SummonedMapReload` + reload state 3. Driven from `IsValidSos.Update` (game thread). 30 s cooldown prevents re-arming on its own load.
- Launchers `start-two-instances-SEAMLESS-REJOIN.cmd` (+ host/guest-only): seamless + `-Persist -MatchCheck -Rejoin -Reinsert joined -Boss -Lamp`.
- Dev build `a0238e2d…`, superseded by `d498dd23…` after code review:

### Fixed (review of the rejoin/reinsert code, same day)
- Rejoin no longer sets `pending_summon_reload_map`: on a same-map respawn the per-tick deferred reload would have called `SummonedMapReload` before the new descriptor existed (wrong-world load). Only the descriptor-finalize path is armed.
- Member replay is no longer armed on the guest's own death load when rejoin is enabled; it arms on the rejoin's own summoned reload instead (was firing mid-load).
- Rejoin waits for 60 consecutive in-world ticks (same map, player present, no pending request) instead of only the request byte, which clears ~0.5 s into a multi-second load.
- A dispatched rejoin clears its reload request after 20 s if the descriptor never finalized, so a later unrelated same-map load cannot trigger `SummonedMapReload`.
- Rejoin state guarded by a recursive mutex (hooks on different threads; `StageTransition` re-enters the persistence no-op hook on the same thread — a plain mutex would deadlock).
- Verified after rebuild: patches 13/13, native 9/9, trace 148/148, persistence 15/15.

### Limits
- Host position comes from the summon claim (where the host stood when summoning), not the host's live position.
- Guest lamp use stays blocked by design (host-driven travel is Phase 7).

## 2026-09-15 — Seamless join stall: fix + isolation switches

### Fixed
- `ApplyPreMatchCrossMapGuestWarp`: a same-map claim (`destination_reached`) now marks the transported host placement consumed. Previously it stayed armed, so a later guest map change (death respawn, Hunter's Dream) triggered a stale pre-match warp back to the old host position (observed as the guest teleporting to the host's old spot).

### Added (isolation switches, client)
- `SHADPS4_BBSC_NO_PLACEMENT_HEADER=1` (`-NoPlacementHeader`): host-placement header neither sent nor accepted (`http.cpp`).
- `SHADPS4_BBSC_NO_GUEST_HOOKS=1` (`-NoGuestHooks`): skip per-tick pre-match warp, deferred summon reload and responder re-ring.
- Launchers `start-two-instances-SEAMLESS-A-FIXED.cmd`, `-B-NOHEADER.cmd`, `-C-NOHOOKS.cmd`; server `start-server-traditional.bat`, `start-server-seamless.bat` now sets the config mode itself.

### Evidence
- Traditional mode (seamless off on server and clients) joined on the first try (20:43:41, 3 s after claim); seamless mode stalled 3 of 5 times with the guest never calling JoinRoom (RESEARCH-NOTES.md).

### Known, not fixable without rebuilding shadNet (needs Qt SDK)
- Seamless broker keeps returning an already-claimed guest to the host's searches (`repeated claim` every ~35 s), which may restart an in-progress summon.

## 2026-09-15 — Phase 2 experiment: member replay after kept load

### Added
- `np_matching2_mm.cpp` `BbscReplayRoomMembers(announce_left)`: re-delivers MEMBER_JOINED (optionally MEMBER_LEFT first, +1.5 s) and SIGNALING ESTABLISHED (+0.5 s) callbacks for every other room member from the unchanged room cache. No shadNet traffic, no cache/peer changes.
- `bloodborne_re.cpp`: a hit on `BBSC.Persist.SkippedCleanup` arms a one-shot replay, run from the per-frame `SosStatus.Update` hook once the stage request byte (`global+0x08`) is clear and `SHADPS4_BBSC_REINSERT_DELAY_MS` (default 6000) has passed. Mode `SHADPS4_BBSC_REINSERT=joined|leftjoined`.
- Launcher `-Reinsert joined|leftjoined`; `start-two-instances-REINSERT.cmd`, `start-two-instances-REINSERT-LEFTJOINED.cmd`.
- Replay trigger runs on its own 250 ms timer thread (gives up after 60 s) because `SosStatus.Update` mostly runs only while a bell is active and death silences it. Dev build `a3e91d61…`.

### Evidence that motivated it
- Guest death with `-Persist`: session kept; host still sees guest with all animations; guest sees no host and its enemies are frozen (RESEARCH-NOTES.md).

## 2026-09-15 — Email-verified accounts

### Added
- `server/account_service/bbsc_account_service.py` (Python stdlib, port 31316): 6-digit email codes (15 min TTL, 5 attempts, 60 s resend cooldown, per-IP pending limit), SMTP SSL/STARTTLS or console mode; creates the shadNet account only after verification via `shadnet-sample.exe` + `RegistrationSecretKey`. Pending passwords kept in memory only; log masks emails and codes are not stored.
- `server/bbsc-accounts.cfg` (SMTP settings, empty = console mode), `server/start-account-service.bat`.
- Interactive `create-account.cmd/.ps1` (host + friend package): username, email ×2, password ×2 (hidden), code with resend, then test login.
- Tested: status, validation errors, register, wrong code, resend cooldown, unknown user (no account created during tests).

### Removed
- `server/register-account.ps1` (replaced by `create-account.cmd`).

### Why not inside shadNet
Rebuilding shadNet needs the Qt 6 SDK (requires a Qt account); the front service needs no rebuild and uses shadNet's existing registration key to block unverified sign-ups.

## 2026-09-15 — Phase 1 (experimental) + research

### Added
- `src/core/bbsc_persistence.{h,cpp}` (new module, wired in `module.cpp` and `CMakeLists.txt`): byte-verified call retargets that keep the co-op session through stage loads and revive; optional boss, OnMatchingCheck and lamp groups. Off unless `SHADPS4_BBSC_PERSIST=1`.
- Trace site `BBSC.Persist.SkippedCleanup` (`0x013001E0`); analyzer section "PHASE 1 PERSISTENCE".
- Launcher switches `-Persist -Boss -Lamp -MatchCheck` in `start-bbsc.ps1` and host script; SETUP.md test modes; friend package rebuilt.
- `tools/bloodborne-xref-scan.exe` built from fork source (Zydis).
- `docs/RESEARCH-NOTES.md`: lamp 4th gate `+0x4B` / `0x0191B6E0` (shadp2p hook runs too early), pickup flag `chr+0x1E2 & 0x20` (EnableGetItem), re-insertion step table.
- Dev build sha256 `76edbabf…`; signatures: patches 13/13, native 9/9, trace 148/148, persistence 15/15.

## 2026-09-15 — Phase 0 instrumentation

### Added
- Fork builds locally: clang-cl 19.1.5 + NMake (`build/configure-shadp2p.cmd`, `build/build-shadp2p.cmd`). Unmodified build succeeded.
- Branch `bbsc/phase0-observers` in `src-shadp2p`: read-only trace site `WorldChrMan.RemoteCleanup` (`0x015BED10`) recording caller, `CSMultiPlayMan` state, `global+0x1592`, reload phase, map and frame chain (+16 lines, `bloodborne_re.cpp`).
- Dev build `client-dev/shadPS4.exe` (sha256 `508dbc6c…`). Signature check on user eboot: patches 13/13, native calls 9/9, trace sites 147/147.
- No gameplay behavior change: observers never alter registers or return values.
- `bbsc-friend package.zip` (32 MB): dev build, `start-bbsc.ps1`, prefilled `host_overrides.json`, `SETUP.md`.
- `tools/analyze_captures.py`: merged timeline + per-Stop kick attribution (caller → class, session state, `state_1592`); validated on a synthetic capture.
- `docs/PHASE1-DESIGN.md` (draft): Stop/cleanup classification table with exact call sites, Option A (per-site retarget) vs Option B (decision hook; fork hooks are read-only), acceptance tests.

## 2026-09-14 — Phase 0 (audit, no code modified)

### Added
- Source: shadp2p `6e97b40` with 44 submodules; shadnet-p2p `5a849a1` with protobuf.
- Releases: `shadps4-win64-sdl-2026-08-16-6e97b40.zip` (sha256 `a523ec37…`), `shadnet-win64-2026-08-16-5a849a1.zip` (sha256 `715302bb…`).
- Built `shadnet-sample.exe` (account tool) with MSVC 19.38.
- Server configured: `Host=<server-ip>`, `Matching2Enabled=true`, `BloodborneSeamlessCoop=true`, `StatsEnabled=false`; running with RE trace.
- Scripts: `server/start-server-seamless.bat`, `server/register-account.ps1`, `client/start-launcher-seamless-trace.ps1`.
- Read-only eboot analysis tools in `report/tools/`.
- Audit report (HTML) rev 1–2; docs set in `docs/`.

### Findings
- Fork patch/hook/trace signatures verified against user eboot (13/13, 9/9, 146/146).
- 43 callers of session Stop; universal kick pair Stop `0x01ED07A0` + remote cleanup `0x015BED10` (26 sites).
- `StageTransition 0x013CDE30` calls Stop unless `global+0x1592` set.
- Event-flag setter chain `0x01258310 → 0x013CFCC0`.
- Field reports contradict fork's cross-map claim (shadp2p#2).

### 2026-09-24 (g) - build C2B44142FBA37EBF: host's lamp for guest respawn, join warp default
- Test of 43B41805 (local, two windows): fast sign worked - ring to joined in about 12 s across
  maps (re-post 4 delivered the claim). The single summon load left the lighting flickering;
  `SHADPS4_BBSC_WARP_JOIN=1` (second real reload) fixed it. A dead guest respawned inside a boss
  arena (guest's own last lamp / nearest lamp of the map).
- Join warp is now default-on, but only after a connect-first (cross-map) join
  (`bbsc_cross_map_join`, set in BbscConnectFirstReloadFinished). Same-map joins get no reload,
  which is what caused the "Beckoner" re-type (2026-09-23). `SHADPS4_BBSC_WARP_JOIN=0` turns it off.
- Follow packet (SignalingBbscFollow) is now 0x22 bytes: + `lamp_point`, the host's
  global+0x1528. Receivers still accept the old 0x1E packet (lamp -1).
- Guest respawn order: host's last lamp if in the loaded map -> beside the host -> nearest lamp ->
  own body. The guest's own last lamp is no longer used. Log: "BBSC respawn: host's last lamp is
  point N (...)", and "host's last lamp is now point N" whenever the host rests somewhere new.
- Host and guest must both run this build (older builds ignore the longer packet, so warping
  together would stop working between mixed builds).

### 2026-09-24 (h) - build C25776CCDD0D7D33: kill echoes, Silencing Blank
- C2B44142 test: guest respawned at the host's last lamp (point 2302950) - works. Echoes still
  +671 guest / +1341 host; Silencing Blank froze the guest (camera stuck).
- Echoes: 0x01335400 is only the event reward. Kills pay through 0x018FC860, which multiplies by
  0.5 (0x04927EC0) when guest_share is set (caller 0x018C5070: SprjSessionManager+0x124 == 6 or
  [[0x05556678]+0x16F8]+0x98 != -1). New WideEdit `Echoes.GuestKillFull` 0x018FC94B points the
  load at 1.0 (0x04927EBC). `Echoes.GuestDouble` (2.0) replaced by `Echoes.GuestEventFull` (1.0 at
  0x04925B20), so event rewards are not doubled.
- Silencing Blank: host's OnLeaveMagic ends the room; the guest drops to session state 0 while
  still in the host's world and the kept-session patches skipped the trip home. TickBbscSessionEnd
  now suspends persistence (20 s, like quit to title) when a guest (state 6 seen) reaches state 0
  in the host's world. Log: "BBSC persistence suspended ... (guest sent home: ...)".

### 2026-09-25 - build FBDDDCBC221A131C: custom co-op messages, fog walls rebuilt from game data
- Earlier today: C25776CC (kill echoes via 0x018FC860 1.0, Silencing Blank sends the guest home),
  906F2315 (previous log kept as shad_log.prev.txt).
- **Messages.** Text lookup is 0x00F6F340(MsgRepository [0x05540300], 0, category, id); the FMG of a
  category is [[[repo+8]+ver*8]+cat*8] (+0x0C groups, +0x18 absolute offset-table pointer, groups
  at +0x28: index, first, last). TickBbscMessages points 73 event-text (category 30) entries at
  BBSC's own text every 2 s: joins ("has joined the hunt", "The hunters' journey begins"), deaths
  ("You died." instead of "Returning to your world", "They will rise again"), host fallen, and the
  38 boss "say farewell" lines ("<boss> was slain. Area cleared. / Prey slaughtered - the hunt goes
  on"). Table generated and checked by tools/gamedata/gen_messages.py -> src/core/bbsc_messages.inc.
  Switch: SHADPS4_BBSC_MESSAGES (default on). English whatever the game language.
- **Fog walls, from scratch.** New readers in tools/gamedata (DCX/BND4/FMG/MSB/EMEVD). Every co-op
  wall (18 base+DLC, 8397 Chalice placements) is switched by EMEVD 7600 or 12907600
  (InitializeEvent(slot, 7600, object, sfx)); no boss wall is. Chalice ids collide across blocks
  (79/88), so walls are now recognised by the running event: hooks on the bank 2005 (0x017C46E0) and
  2006 (0x017C64E0) handlers compute the event from [inst+0xB0] and the EMEVD event table, and the
  draw/collision/fog-on calls of 7600/12907600 are refused. All id lists, the boss-gate list and the
  1-second sweep are gone. The old list hid Old Yharnam's lamp object 2301950 (o009900) in every
  session - fixed by construction. Reference: docs/COOP-WALLS-AND-MESSAGES.md.

### 2026-09-25 (b) - build 467ABA62F013694D: more co-op messages, BBSC notices
- 95 replaced messages (was 73): invasions ("An enemy hunter, X, has invaded! Be prepared",
  invader slain / fled, bell-ringing woman warning, the invader's own "You have entered the world of
  ... as their adversary") and bells answered.
- New BBSC notices through the game's timed information window: AddInfomationBuffer (Lua) is
  0x01311150(30, id) - looks up category 101 then 30, expands tags, copies into [0x05546C88].
  BbscAnnounce borrows id 10010740 for one call and restores it. Shown when the host warps with a
  partner ("Your fellow hunter follows you through the night"), when the guest catches up after a
  follow warp, and on every kept-death respawn (beckoner's lamp / beside the beckoner / the lamp).
  Log: "BBSC notice shown: ...".

### 2026-09-25 (c) - build 278B78F2E2A991C9: guest keeps items from the host's world
- Map treasures (item +0x48 bit 0x80) are hidden from a summoned guest by 0x017D9E70 (make item
  pickable): CMP [session+0x124],6 / JZ ret0 and CMP [[0x05556678]+0x16F8]+0x98,-1 / JNZ ret0.
  Displacements at 0x017D9EE9 / 0x017D9EF7 zeroed (jump to next instruction).
- Pickup 0x017D8570: a guest only sent packet 0x2C to the host (JNZ 0x017D85FB -> JMP, so the guest
  takes the local path 0x017DE6E0 mark taken) and skipped giving the lot (JZ 0x017D88C2 -> NOP, so
  0x017D89F0 gives it). Inventory is saved live (save writer 0x0156AA50 only swaps world snapshots
  for a guest), so the guest keeps it. Group GuestItems, switch SHADPS4_BBSC_GUEST_ITEMS (default on).
- Found while reading the save writer: a guest's save stores its map from global+0x14F0, which the
  follow/join warp overwrites with the host's map (SetForcedSummonMap) - why a crash left a tester's
  save in the host's Old Workshop. Not fixed yet.

### 2026-09-25 (d) - build 78DE08F9AE812111: echo reward trace
- Echoes still reported halved for the guest (a tester's build had 23 sites, i.e. both echo patches).
  Checked: 0x018FC947 and 0x01335589 are the only reads of a 0.5 in the reward code; the player's
  add-echoes (vtable +0x3D0 = 0x018FC600) multiplies by the soul rate (+0xE0) of the player's
  special effects, and no SpEffectParam row has 0.5 there (lowest 0.7). So the guest's reward takes
  another path.
- Observers (log only, first 300 per run, session live): 0x018C5070 kill reward entry (dead chr
  flags +0x1E1 & 0x240000), 0x018FC860 reward calc (souls, guest_share, patch bytes read back),
  0x018FC1E0 reward for a killer (packet 0x26 path), 0x018FC600 add echoes (amount, scaled, caller).
  Log prefix "BBSC echo path:".

### 2026-09-25 (e) - build 3DF1DF86A5746E0D: the Hunter's Dream together
- A guest in the host's Hunter's Dream can use the Plain Doll (level up), Gehrman, the Workbench,
  Storage, the Memory Altar, attire and the messengers (shops, Insight shop, adornments, first
  weapons, notebook, bells). Headstones (t210000-210003, t210721) and Ritual Altars
  (t210050-210110) stay the host's. Switch SHADPS4_BBSC_DREAM (default on).
- New tools: tools/gamedata/esd.py (talk script reader; the comparison opcodes were checked against
  the scripts: 0x95 ==, 0x96 !=, 0x91-0x94 <= >= < >) and esd_dump.py (readable dump).
- Why a guest could not talk: every Dream talk script starts with "if f19() == 1 -> client
  waiting". f19 = byte +0x25 of the script's condition evaluator (vtable 0x0535AAB0, talk id
  +0x10; dispatcher 0x0203AD60 case 0x13), set every frame by 0x0203ABB0 when the local
  character type is not 0 or 8 and never cleared. Hook at 0x0204C694 (right after that call in the
  talk update 0x0204C0B0) clears it for the allowed talk ids.
- Why the Doll would still hang: her level-up waits for flags 72100101/72100102, set by m21_00
  events 12100112/12100113, and those end at once unless character 10000 has type 0 (4[03]).
  Hook at the check's evaluate 0x017A3BE0 (entity +0x28, type +0x2C, result bit 0 of +0x10)
  answers yes for 23 allowlisted Dream events only (Doll, Gehrman, workshop, messengers); the
  event is read from the instance in RDX (the compile step 0x017B9C50 evaluates each new condition
  with it) and remembered per condition object for later re-checks. Endings (12100000/002/180),
  boss events and common.emevd keep the vanilla answer.
- Gehrman: allowed only while not (flag 1028, or 1029 without 1030) - otherwise his script offers
  "Submit Your Life / Refuse", which a guest must not start in the host's world. Event flags are
  read with 0x01399BF0.
- The guest's arrival notice in the Dream says "The Hunter's Dream opens to you."
- Untested in game. Log prefix "BBSC Dream:".

### 2026-09-25 (f) - build 842751486E407C13: map changes after a kept death
- Reported: after deaths the host's lamp travel froze; the Silencing Blank left the guest stuck.
  Captures: all 5 StageTransitions after a kept death since 2026-09-24 got no
  Warp.StageTransition.AcknowledgeCheck (host and guest); last night's session had no deaths and
  every lamp warp worked.
- Cause (CONFIRMED statically): the HP-zero death handler 0x013813D0 sets event flag 4000 and the
  event manager request [[0x0553B108]+0x10]+8 = 4. The stage flow 0x0193AC10 arms a requested map
  change only if the multiplayer members are ready and, while that request is 4, the player's death
  flag [[player+0x18]+0x30]+0x18 bit 0 is set (0x0194083A..0x0194087A). Vanilla clears the request
  with the death reload; a kept death never reloads, so after the revive every map change waited.
- Fix: BbscRespawnInPlace resets the request 4 -> 0 (next to its existing resets of +0x1520/+0x1524
  and [0x0553B0D8]); TickBbscStageWatch also clears it after the player is alive 4 s in a session,
  and logs "BBSC map change not started after 5 s" with the stage-flow gates if one still stalls.
  Flag 4000 is left alone (repeat deaths keep going through TickBbscDeadWatchdog).
- Found on the way: the save "permission denied" errors are harmless (the files are written);
  0x0156D570 is the save routine, global+0x1547/0x1548 are save requests, +0x1554 the save state.
- New notice on the guest when the host dies (TickBbscHostDeathNotice): "Your beckoner has fallen.
  They will rise again at the lamp."
- The guest window's crash at startup in this test was on HavokWorkerThread (0xC0000096 at eboot
  0x007741C2, the physics engine), once in all logs - emulator side, not a BBSC hook.
- Echo trace result (local test, build 3DF1DF86): same kill paid +1341 to the host (guest_share 0)
  and +1341 to the guest (guest_share 1, patch bytes 6D B5 02 03 read back) - the kill fix works.

### 2026-09-25 (g) - build 1D7337AA722DB383: death curtain, "YOU DIED" on every death
- Requested: a kept death should look like the game's own - "YOU DIED", then a loading screen -
  without a real reload; the instant warp to the lamp looked bad.
- Death curtain (TickBbscDeathCurtain): instead of reviving in view, the kept death fades the screen
  to black (SprjFade [0x05540068]+0x20: target RGBA +0x1C, seconds left +0x38 - the stage flow's own
  fade), opens the game's loading screen (0x0176D380, kind MenuMan+0x808 = 2 like a death reload),
  revives and warps in the dark (BbscRespawnInPlace), holds 2.5 s, closes the loading screen
  (0x0176D3C0) and fades back in. The respawn line ("You awaken at the lamp") waits for the screen.
  Timeouts on every step (show 1.5 s, close 3 s, whole curtain 15 s with a forced close); no fade ->
  the old in-view respawn.
- "YOU DIED" on repeat deaths: the death handler 0x013813D0 returns at once while event flag 4000
  is set, and only the reload cleared it. ClearBbscStaleDeathRequest now also clears flag 4000
  (SetEventFlag 0x0132E5A0) after a kept respawn, so the game's own death script runs again.
  TickBbscDeadWatchdog no longer takes the 3 s repeat-death shortcut while that script is running
  (SoloPlayDeath / PartyGhostDeath seen within 2 s of the death); if it does not start, the watchdog
  still revives after 3 s, now through the curtain. Untested.

### 2026-09-25 (h) - build A6CB611C48862BBC: host's world on follow warps, solid Dream NPCs, banner lines
- Test of 1D7337AA (user, local, both windows): death curtain liked. Problems: Dream NPCs grey for
  the guest; after returning to Central Yharnam the guest's world was wrong (a boss died in front of
  the guest, doors shut "like a new save", the opening cutscene played); the host saw the summon line
  again on every warp, the guest saw "?ActionEventInfo?"; the "journey begins" summon line was bad.
- **World state on follow warps.** Log: after arriving in 0x18010000 the guest's member task restarted
  at FirstSyncWait and 11 ms later a kill paid it 3466 echoes (the host's dead boss). Event 12410000
  plays the opening cutscene and ends only on its own flag, so the guest's flags for m24_01 were not
  the host's at load. SprjEventFlagMan [0x0553B100] (CONFIRMED statically, 0x013CF0E0/0x013B5910/
  0x013CFD80): 1000 flags per group, 125 bytes, 1200 groups at +0x28, std::map key -> slot at +0x38.
  A map block owns the keys kind*10000 + area*100 + block*10 + 0..9 (0x013B6540); kind 5 are the item
  pickups (ItemLotParam getItemFlagId 5AABxxxx). New: the host sends its block's kind 1/6/7 groups
  (30 x 125 B, 4 chunks, new signaling packet 0x22 `SignalingBbscWorldFlags`, np_signaling_bbsc.h)
  every second; the guest waits up to 4 s for them before a follow load, writes them into its table,
  and writes them again when the load creates its character (0x0178D9C0), before the map's events run.
  Item pickups and the global groups (story/NPC flags below 100000) are not copied. Switch
  `SHADPS4_BBSC_WORLD_SYNC` (default on). Logs "BBSC world state (...)": bytes that differed before
  writing, and the difference again after arriving - tells whether the load itself resets the block.
  Signaling receive buffer 256 -> 2048 bytes.
- **Grey Doll / Gehrman.** The NPC constructor 0x018E4870 gives a character whose NpcParam row has
  isChangeWanderGhost (+0x145 bit 7) its ghostModelId (+0x10C) and type 4 when the local player is
  in someone else's world (SprjSessionManager +0x124 == 6 or [global+0x16F8]+0x98 != -1). In the Dream
  that is the Doll (803000 -> model 8035) and Gehrman (804000 -> 8045). BBSC clears the bit in those
  two rows once the params are loaded (row lookup 0x01F226C0, SoloParamRepository [0x05540340]).
  New tools: tools/gamedata/param.py (PARAM/PARAMDEF reader).
- **Co-op banner lines** (from the untested banner build, now included): the role table 0x0553D750
  (0x22 roles x 0x80) holds the summon line (+0x30, read by 0x0138A750) and the host's join line
  (+0x34, 0x0196BA20). Warping together re-runs both; role 0 has no guest line -> "?ActionEventInfo?".
  While a warp is on, both columns point at "You have caught up with <host>." / "<guest> has caught
  up with you." / "The Hunter's Dream opens to you." (placeholder texts 15000011/12/35). BBSC's own
  notices now go through the same banner (0x0138A750 with carrier 15000006); the old info-window
  route never showed. New summon texts: "<guest> has joined your hunt." / "You have joined the hunt
  of <host>."

### 2026-09-25 (i) - build 6CA81C78B81B1BB1: "YOU DIED" on repeat deaths
- User: "YOU DIED" still only on the first death (curtain liked). Log of 1D7337AA: the guest's
  2nd death ran no death step even with flag 4000 cleared ("repeat death in the same load") - the
  HP event is armed once per map load (0x0137D7A0), so clearing the flag alone does nothing.
- TickBbscDeadWatchdog now raises the text itself 0.5 s into a repeat death if no death script has
  started: 0x0132EAD0(event, 0xFFA, chr vtable +0x558, +0x598) - SoloPlayDeath's own first call -
  with the Lua event object [SprjLuaEventMan 0x0553B0C8]+8 (the object 0x015BAD90 raises
  "TextEffectEnd_NPCPK_Success" on). The curtain follows at 3.5 s (was 3 s). The guest's first
  death logs whether PartyGhostDeath's event object is that same object ("same"/"different").
- Includes everything in (h) (A6CB611C was not handed out on its own).

### 2026-09-26 - build 508FE82A75D53C44: load cover, item comparison
- Test of 6CA81C78 (user, local): the world state works - after every follow load the guest's
  block matched the host's exactly ("after arriving ... 0 groups / 0 bytes"); the load did not
  reset it (only 6-35 bytes differed before writing, the guest's own state). Reported: the guest
  does not see an item the host sees (Cathedral Ward, corpse by the lamp); the cross-map summon
  shows a black screen - use the death curtain's look.
- Items: treasures are enabled by 0x017D9E70 (the GuestItems patches let a guest through) from the
  item pickup flags, and those (kind 5, 5AABxxxx) are the guest's own - never copied. So a guest
  does not see what it already picked up in its own world (personal loot). The host now also sends
  its kind 5 groups (40 groups, 5 chunks), never written; the guest logs per map "items: N taken in
  our own world (hidden for us only), M taken by the host (still here for us)" after joining and
  after every follow load.
- Load cover (TickBbscLoadCover, switch SHADPS4_BBSC_LOAD_COVER): the guest's JoinSession_White
  (summon accepted) and every RequestBbscSessionWarp (follow, cross-map second reload) fade the
  SprjFade plate to black; it is set back to black every frame (current + target RGBA, timer 0 so
  the stage flow's fade gate stays open) until the join teleport / follow arrival places the guest,
  then fades in (0.8 s). One cover spans both cross-map loads. Ends by itself 3 s after the session
  is gone (failed summon) or after 75 s. The game's loading screen is not touched: the death
  curtain's calls to 0x0176D380 never showed it ("did not show" every time), so what the user
  liked is the fade.

### 2026-09-26 (b) - build 6D338776114ADE8F: the guest's map items exist
- User: the guest is NG+ and took nothing, so "already picked up in its own world" was wrong.
- Cause (CONFIRMED statically): spawning an item 0x017D8370 (called by the map's treasure setup
  0x017D2F30 and the drop code 0x017DC300 / 0x017DA960) sends a summoned guest's request to the host
  as packet 0x2C type 3 instead of creating it, and the create function 0x017DE870 also returns at
  once for session state 6. The guest never had any map items; parts 1/2 (show/pick up) had
  nothing to act on. The item manager has 20 such state-6 tests (0x017D4C40..0x017DE870).
- New GuestItems patches: SpawnOwn 0x017D83D2 JNZ -> JMP (0F 85 -> 90 E9, the guest creates its own
  items from its own map load and pickup flags), CreateOwn 0x017DE8D4 displacement 0x25C -> 0 (the
  create function's own guest test), MarkTaken 0x017D6ADC 74 0A -> EB 1E (0x017D6780 sets the
  pickup flag +0x28 / count flag +0xB8 for a guest too, so a taken item does not come back on the
  next load; host path unchanged). The created item is only broadcast while hosting (+0x124 == 3).
- Not changed: the item-picked-up events (first gate in 0x017D6780) still do not run for a guest.

### 2026-09-26 (c) - host package for other people: share/BBSC-host-package.rar
- No database (shadnet creates an empty db/shadnet.db on first start, database.cpp Open ->
  mkpath + CREATE TABLE IF NOT EXISTS), a fresh random RegistrationSecretKey (not ours), no logs.
- Three launchers (tiny C# exes, build/launchers/HostLauncher.cs + Build<Network>.cs, csc 4.0):
  START-HOST (same house / Radmin / Tailscale).exe -> tools/start-host.ps1 -Network Local|Radmin|
  Tailscale. Local = the physical Wi-Fi/Ethernet adapter's private address (VPN adapters like
  WireGuard/Proton 10.2.0.x are skipped); firewall rules per network, scoped LocalSubnet /
  26.0.0.0/8 / 100.64.0.0/10.
- join-host.ps1 (host package and both friend packages) accepts all three address kinds and uses a
  portable shadPS4's "user" folder next to the exe when there is one (a tester's setup).
- Defender: host package and exe-only rar clean.
- Update (same day): a tester's game server died at start with PermissionError WinError 10013 on
  port 20443 (reserved by WinNAT / Hyper-V, or held exclusively by another program), so the game
  said "Failed to acquire Bloodborne server information". START-HOST now test-binds all four ports
  first: a port held by a program -> names it; a reserved port -> one elevated step (net stop winnat,
  netsh add excludedportrange for 31313/31315/20443 TCP and 31314 UDP, net start winnat). It then
  waits for both servers to answer (31315 /status, 20443 ss.info) and says which one did not start.
  JOIN-HOST writes the redirect into every shadPS4 settings folder it finds (%APPDATA%, the package's
  "user", a running shadPS4's "user", or a dragged-in portable shadPS4.exe) and checks both servers.

### 2026-09-26 (d) - BBSC intro (client-dev only, not shipped yet)
- New ImGui layer src/imgui/bbsc_intro_layer.cpp (started from module.cpp after the BBSC installers,
  CUSA03173 only, switch SHADPS4_BBSC_INTRO default on): ~7.5 s title card on the foreground draw
  list over the game's boot logos - black cover, dark-red glow, rising embers, a blood thread that
  draws out and drips, "BLOODBORNE" letter by letter, "SEAMLESS CO-OP", tagline, build date,
  credits (shadPS4, Wozzardman's shadp2p, fhody125) and a not-affiliated notice. Any key/button/click
  skips (0.6 s fade). No assets: the title serif is read at startup from %WINDIR%\Fonts (Garamond,
  else Palatino, else Georgia; uppercase range only) as IMGUI_FONT_BBSC_TITLE in imgui_core.cpp.

### 2026-09-26 (e) - build 2CF01E3FB86C5A9E: the cinematic intro plays in the emulator (client-dev only)
- bbsc_intro_layer.cpp: when BBSC-intro.mp4 is next to shadPS4.exe, it plays instead of the drawn
  title card. FFmpeg decodes the soundtrack up front (AAC -> f32 stereo 48 kHz, SDL audio stream)
  and the video on its own thread into a 6-frame queue; the render thread shows the newest frame due
  (steady clock from audio start) through ImGui::Vulkan::StreamingTexture (new in imgui_impl_vulkan:
  device-local RGBA8 image + mapped staging buffer, copy + barriers per frame). Aspect-fit over a
  black cover, "any key to skip" hint, any key fades out in 0.45 s (audio gain too), the end fades
  into the game in 0.7 s. Missing/unreadable file, no frames within 6 s, or no texture -> the old
  title card. Colour matrix from the stream (BT.709 when tagged).
- The film itself (art/intro2, not code): 15 s, 2.39:1 in 1920x1080. Procedural Gothic cathedral,
  moonbeam through a leaded east window, a drop of blood forming on an iron spike under a hanging
  lantern, the camera diving with it 15 m, slow-motion splash on a carved ledger stone, the blood
  running along a crack into the V-cut letters BLOODBORNE, pewter-inlaid SEAMLESS CO-OP, "BY
  CLS_AMG", cut to black on the last bell. CC0 assets only (Poly Haven textures/models, Freesound
  CC0 bell/IR/wind/drops, OFL fonts). RENDER-INTRO.cmd renders, grades (post.py) and encodes.

### 2026-09-27 - build 53368727D5FC7248: the intro can open with shots from the game's own trailer (client-dev only)
- bbsc_intro_layer.cpp plays a playlist: optional BBSC-intro-cuts.json next to shadPS4.exe lists shots
  ({"in","out","speed"}) from a file in the running game (e.g. /app0/dvdroot_ps4/movie/sprj_advertise.mp4,
  resolved through MntPoints::GetHostPath), then BBSC-intro.mp4 (ours) closes it. Nothing of the game
  is shipped: every player's own copy supplies those shots. Each shot keeps its own sound (5.1 folded
  to stereo, 30 ms edge fades), or "music_from" plays one continuous stretch of the source's sound under
  all of them. "letterbox": 2.39 masks the trailer shots to the film's bars. 12-frame queue so seeks
  between shots do not stall; missing source -> only our video; missing video -> the title card.

### 2026-09-27 (b) - build A428C467C3A8412E: guest after a boss, black screens, guest items (untested)
Report 2026-09-27 (guest log shad_log (3).txt, host log of the same session): after the boss the
guest could not use ladders or interact; the guest kept seeing black screens; the guest could not
see items ("show them even if he took them").
- Boss clear (CONFIRMED statically): BlockClear2 0x013849A0 sets event flags 4047, 4000 and 4092
  for every living member (0x01384E5E..0x01384E86) and MenuMan [0x05562878]+0x2F0 = 1 (mission
  clear). Only the end of the chain undoes them: BlockClear2_3 0x01385930 (+0x2F0 = 0, 4047/4000 = 0;
  the host runs it) or the leave variant 0x013859F0 (+0x2F0 = 0, 4047/4000/4092 = 0, then leaves).
  The guest skips both to stay, so 4000 stayed set - the same flag that blocked map changes after
  kept deaths - and the old death-lock clearing only reset 4047. TickBbscBossUnlock now clears
  +0x2F0 and the three flags every second for 20 s after the guest's skipped BlockClear2_3.
  (Checked on the way: +0x2F0 is read by the in-game menu 0x01756680/0x01756A70; effect 4680 that
  BlockClear2 gives the guest has no duration; the recall event 0x0138D380 is only a message.)
- Black screens: the host crossing between Central Yharnam (0x18000000) and Cathedral Ward
  (0x18010000) on foot changed its map id, and TickBbscFollowHost reloaded the guest every time
  (4-7 s each, once twice). Maps of one area share one coordinate frame (the host moved 19 m across
  the border), so the guest now follows only when the host is in another area or more than 120 m
  away, and only after the maps have differed for 2.5 s.
- Guest items: the guest's items were created from its own pickup flags (kind 5), so what it had
  taken in its own world stayed hidden in the host's. ApplyBbscGuestLootView (before a follow load
  and at 0x0178D9C0 for every load into the host's world, the join included) sets the kind 5 groups
  of the whole area (5AAB0..5AAB9) to only what the guest picked up this session (bits that appear
  between our writes). Only the live table of the host's world changes; the guest's save keeps its
  own world. Forgotten when the session ends.
- Save "permission denied" lines in both logs: known (A4), the files are still written.
- Installed: client-dev, friend package, friend package; share\BBSC-A428C467-exe-only.rar.
  The friend packages now also show the drawn BBSC title card at start (skippable).

### 2026-09-27 (c) - build 1DF99ABA93CD47A7: Beckoning Bell without Insight, up to five hunters (untested)
- Free bell: EquipParamGoods 200 (Beckoning Bell) is the only bell with consumeHeroPoint (1, row
  byte 0x38). TickBbscFreeBell finds the loaded row with the game's lookup 0x01F1E3D0 and sets it
  to 0 once params are loaded. Switch SHADPS4_BBSC_FREE_BELL (default on).
- Five hunters: the engine is sized for 5 members (session object 0x017875D0 sets max 5; member
  list 5 entries; rooms are created with 5 slots). The summon builder 0x01874710 caps cooperators at
  summon manager +0x1FC (3 from 0x01870C50) and remote players at < 4; retail still stops at 2
  guests somewhere not found yet. ApplyBbscHunterCap (SummonBuild.Entry) raises +0x1FC to 4 and
  logs "BBSC hunters: summon build - ..." with every count, to find the remaining limit in the next
  3+ player test. Switch SHADPS4_BBSC_FIVE_HUNTERS (default on).

### 2026-09-27 (d) - build F3FB79DB87BC4421: boot intro off by default
User asked for no intro until the trailer is rendered. `SHADPS4_BBSC_INTRO` left
DefaultOnSwitches (now 25), so neither the title card nor BBSC-intro.mp4 plays; `SHADPS4_BBSC_INTRO=1`
brings it back. client-dev's BBSC-intro.mp4 + BBSC-intro-cuts.json moved to
archive/intro-removed-2026-09-27. Otherwise identical to 1DF99ABA. Installed in client-dev,
friend package, friend package; share/BBSC-F3FB79DB-exe-only.rar and
share/BBSC-friend-build-F3FB79DB-radmin.rar .

### 2026-09-27 (e) - build E3A607B3C69C3D09: the host's Hunter's Dream for the guest; follow to the lamp (untested)
Report (tester notes, 2026-09-27): in the host's Dream the guest saw a Bath Messenger in the wrong
place, "Memory Altar" at the mirror, could not use the Insight messengers. NPCs grey outside the
Dream, items invisible and the Memory Altar item problem went away when the tester disabled a mod
(not ours). User: the guest must see the host's copy of the Dream; a follow must end at the lamp.
- Dream state: the Dream is driven by flag groups outside m21_00 (story 1xxx, 2xxx, 5xxx-9xxx,
  lamps 1AAB78xx of every map, key items 5xxxxxxx, chalice 94x05xxx; list from m21_00_00_00.emevd
  + m21 talk ESDs, group 0 left out). Host in m21_00 sends those 57 groups as
  SignalingBbscWorldFlags version 2 (old builds drop it). Guest writes them at the follow load and
  at 0x0178D9C0 when the destination is 0x15000000, keeping its own copy; restores it (keeping bits
  the game changed meanwhile) when following out, or once out of the Dream / no longer a guest
  (TickBbscDreamStateRestore). Log "BBSC Dream state (...)".
- Follow to the lamp: the guest marks the host's first report after a >1.5 s silence (the host is
  quiet while loading) or a >30 m jump as its arrival point; the follow load and the placement use
  it (log "at the host's arrival point (lamp)"). No packet change.
share: BBSC-E3A607B3-exe-only.rar, BBSC-E3A607B3-exe-only+host-package.zip.

### 2026-09-27 (f) - build BE80FC0508E0C0A8: BBSC loading screen over the load cover (untested)
User: the cross-map summon shows a black screen (21-33 s in the logs) - show a loading screen.
The game's own Now Loading screen cannot be opened on demand (0x0176D380 never showed it), so
imgui/bbsc_loading_layer.cpp draws one on the overlay: black with a red glow and embers, "Crossing
into another hunter's world" (summon) / "Following the hunt" (follow), a co-op hint every 7 s,
"Now Loading" + pulsing blood drop bottom right. StartBbscLoadCover shows it, EndBbscLoadCover hides
it (0.6 s in / 0.8 s out, with the plate's own fade-in). Logs "BBSC loading screen: shown/hidden".
Includes (e). share: BBSC-BE80FC05-exe-only.rar, BBSC-BE80FC05-exe-only+host-package.zip.

### 2026-09-28 - build B3B670279472F8F8: cinematic camera (untested)
User: the Blender trailer looks wrong (textures, bosses, weapon grip, animation - no Havok
behavior/IK/cloth); film the real game instead. Retail 01.09 still has the developers' free camera
(SprjDebugCam, DbgFreeCam, Game.IsPlaytestMode_EnableFreeCamera, DebugCamPad, ModelViewer, Lens
Simulation "IMAX/70mm/35mm movie"). CONFIRMED statically: stage flow 0x0193AC10 calls the camera
director update 0x015715F0(dt, director=[flow+0x110]); director +0x10 render camera, +0xD0 debug free
camera, +0xC4 != 0 -> the debug camera is copied over the render camera (rows right/up/forward/pos at
+0x10..+0x4F, fov +0x50, projection +0x54..+0x5C) before SprjCamera [0x0553E8F8] copies it.
New: core/bbsc_cinematic.cpp (window side: F1 toggle; while on, keyboard/mouse go to the camera and
not to the pad mapping; controller untouched) + BbscCinematicCameraHook at 0x015715F0 entry: starts
from the render camera, WASD/QE/mouse-look/arrows, Shift/Alt speed, wheel speed, Z/X zoom, roll,
glide smoothing; writes the debug camera (or BBSC's own block if +0xD0 is null) and sets +0xC4.
Switch SHADPS4_BBSC_CINEMATIC (default on). The game's own "Display HUD" option hides the HUD.

### 2026-09-28 (b) - build 694AE85D161DC9A0: animated intro card back on
User: no Blender trailer as intro; use the animated BBSC card. SHADPS4_BBSC_INTRO is default-on again
(27 switches). With no BBSC-intro.mp4 next to shadPS4.exe the ImGui title card plays (BLOODBORNE /
SEAMLESS CO-OP, embers, blood thread; any key skips). Includes the cinematic camera (B3B67027).

### 2026-09-28 (c) - build 399766989A12B161: intro off again
User dropped the intro plan (text intro in art/intro4 kept, BBSC-intro.mp4 archived to archive/intro4). SHADPS4_BBSC_INTRO off by default (26 switches). Cinematic camera included. The trailer titles/credits (art/trailer) will be reused in a gameplay showcase video.

### 2026-09-28 (d) - build 12845064CAA3754E: whole-area world state, notice text, loading-screen font
Test (two instances, screenshots): guest joined in 0x18000000, walked to Cathedral Ward without a load -> Oedon Chapel lamp missing for the guest, chest state differed (block never synced); respawn notice showed "?ActionEventInfo?"; loading screen text "C???".
- Host sends every existing block of its area in turn (every other second, v1 packets for that block); guest keeps a snapshot per block (bbsc_world_blocks) and writes the target block + all fresh neighbouring blocks of the area at every load.
- BbscAnnounce keeps the carrier FMG entry pointed at the notice for 10 s (the banner resolves the text when it draws, after 0x0138A750 returns).
- Title/loading font glyphs: printable ASCII (was 0x20-0x5A only).

### 2026-09-28 (e) - build A15DA00CC4C27F73: item traces
User: the guest sees no item (not a lamp issue). Guest loot view reported 0 own-world flags for area 24, so flags do not hide them. New RE trace sites BBSC.Items.TreasureSetup 0x017D2F30, BBSC.Items.Spawn 0x017D8370, BBSC.Items.Create 0x017DE870 (first 8 hits + every 300th logged, captures written) to see whether the guest creates map items at all.

### 2026-09-28 (f) - build 5B726D9DD255B61E: game loading screen, no reload when the host walks
User: the BBSC overlay loading screen looks weird; use the game one (the death one: MENU_NowLoading, the "Bloodborne" logo). And no loading screen when walking between areas.
- Overlay loading screen no longer shown. TickBbscLoadCover lets the game loading screen through (MenuMan+0x804 != 0) and asks for the death kind (MenuMan+0x808 = 2) until it shows; black only before/after the load. Log "the game loading screen is up (kind N)".
- Follow: no reload when the host map is resident in the guest game (map list 0x0553B148) and the host did not arrive by a load (arrival_by_load = the host was silent >1.5 s). Arrival by jump only counts inside one map (coordinates are per map). Log "host walked into map ... no reload".

## 2026-10-04 (d) ALLOW-GAME.cmd
Guest could not join when the friend hosted (Beckoning Bell); the reverse worked. Likely the host PC's firewall blocking inbound UDP to shadPS4.exe (Windows allows Private only; Radmin/Hamachi often Public; Cancel makes a Block rule). New ALLOW-GAME.cmd + toolsllow-game.ps1 in both C8FB6283 packages and zips: inbound UDP/TCP allow for shadPS4.exe (VPN ranges + LocalSubnet, all profiles), switches off Block rules. HYPOTHESIS until tested.

## 2026-10-04 (e) mod 12 - boss wipe: the guest waits for the host's reload
Test (mod 11, logs test logs, CLS_AMG host, guest): guest died
first and spectated; the host died; the guest saw nobody alive and reloaded itself - into its own
respawn point (Hunter's Dream, map 0x15000000) while still in the session. During that load it sent
no life reports, the host's copy of it looked alive (partner revive at 20:58:57), the host's death
step (9 s after HP 0) chose spectate and the host stayed a corpse. Fix: a summoned guest (state 6)
never reloads itself while the session lives - with nobody alive it keeps spectating (and reporting
dead) until the host's reload rebuilds its character ("a load ended the boss fight" -> respawn in
place in the host's reset world); fallback own reload after 45 s. Log lines: "guest waits for the
host's reload". DLL 5191F184. UNTESTED.

## 2026-10-04 (f) mod 13 - camera probe (debug)
For "dead player spectates the living one". The follow camera class: constructor 0x0143A0E0,
vtable 0x0536B7C0 (base 0x0539FE30), tuning menu ChrFollowCam/ChrExFollowCam (0x0143FB60).
F9 in the game window: scans memory for the local PlayerIns pointer and logs every holder with its
owning object/vtable ("BBSC cam probe:" lines, "<- FOLLOW CAM" marks the camera class). F10: swaps
that pointer to the partner's in camera objects; F10 again restores. Includes mod 12. DLL C85967D3.

## 2026-10-04 (g) mod 14 - F1 free camera in the mod (replaces the mod 13 camera probe)
User's pick: instead of following the partner, the dead player presses F1 for the free camera.
The F1 camera (game debug camera via director 0x015715F0, see bloodborne_re.cpp) was emulator-only:
the mod's TakeCinematicInput returned nothing. Now the mod polls the keys (GetAsyncKeyState, only
while its own window is in front): same keys, speed on PageUp/PageDown (no wheel in a poll); right
mouse look by cursor delta + SetCursorPos. Keys still reach the game (a mod cannot swallow them).
On respawn (dead -> HP > 0) the camera switches itself off and goes back to the hunter. The F9/F10
probe is removed. Includes mod 12. DLL FF9B73F4. UNTESTED.

## 2026-10-04 (h) mod 15 - free camera on a controller
The mod swaps the game's scePadReadState import slot (0x053E4EA0) for a wrapper: it sees the pad
data the emulator gives the game (any controller the emulator supports). L3 + R3 toggles the camera
(same state as F1). While on: left stick fly, right stick look, R1/L1 up/down, R2/L2 zoom in/out,
Triangle reset zoom, Cross fast, Square slow, D-pad up/down speed, left/right glide; the game gets a
pad at rest, so the hunter does not move. New CinematicInput pad_* fields (the emulator build leaves
them 0). Respawn still turns it off. Log: "Pad: controller works the F1 camera too", "(L3 + R3)".
DLL A7BA3FE4. UNTESTED.

## 2026-10-04 (i) mod 16 - guest follows the host's reload after a wipe
Test (mod 15, test logs tester, CLS_AMG host, guest; guest clock 1 h
behind): guest died first and spectated; host died, saw nobody alive, reloaded (CONFIRMED: "reload
requested (no living partner at the death step)"); the guest waited (CONFIRMED: "guest waits for
the host's reload"). The host came back alive, but its reload does not reload a summoned guest, so
the guest's character never changed and it waited for a boss kill. F1 free camera CONFIRMED working.
Fix: a guest that saw a wipe, on seeing the host alive again, waits 2 s and requests its own full
reload into the host's map at the host's position (RequestBbscSessionWarp "wipe"); the new
character is revived by "a load ended the boss fight". Fallback: respawn in place. DLL 9F304F84.
UNTESTED.

## 2026-10-05 (a) mod 17 - auto free cam, mouse look, host scaling, boss bar, join screen, join banners
From the user's list after "everything works" (mod 16) and the tester's notes. DLL 1E8DA91E. All UNTESTED.
- Auto free cam: 2 s after spectating starts (dead, partner fights on) the F1 camera comes on by
  itself; F1 / L3+R3 turns it off for the rest of that death; respawn turns it off (as before).
  New Bbsc::CinematicForceOn (mod + emulator). Log "BBSC cinematic camera: on by itself".
- Mouse (mod): look without a button - cursor hidden and held in the middle of the game window while
  the camera is on and the window is in front; mouse wheel = speed (game window "SDL_app" subclassed
  for WM_MOUSEWHEEL / WM_SETCURSOR). PageUp/PageDown still work.
- Host decides the scaling: the host sends its 5 BBSC_SCALE_* percentages over the relay ("SHAD" 0x24 +
  5 x u16, every 2 s in state 3); a summoned guest uses them while fresh (15 s). Log "BBSC scaling: the
  host's settings are ..." and "... - the host's settings" on the scaling line.
- Boss HP bar: the boss bar HUD 0x0164C200 draws [[chr+0x3B0]+0x20] +0xF8 / +0xFC (CONFIRMED statically);
  the scaling raised HP above that max. TickBbscScaledMaxHp (twice a second, every WorldChrMan block)
  raises +0xFC to the HP when HP is above it. Log "BBSC scaling: character ... max raised".
- Join loading screen: between the summon load and the cross-map second reload (and after a load until
  the guest is placed) the load cover opens the game's loading screen itself (kind 2, like the death
  curtain) and closes it in EndBbscLoadCover. Log "BBSC load cover: loading screen kept up".
- Join banners: hook on the banner menu update 0x01BFF910 (byte 0x055813D8 = banner queued, text at
  [0x055813E0]); every banner is logged ("BBSC banner: ..."); while a guest joins (summon cover + 8 s
  after placing) they are hidden; BBSC's own notices always show.

## 2026-10-05 (b) mod 18 - Duel Bell (PvP toggle item)
DLL D61E1082. Includes mod 17. All UNTESTED.
- Item: goods 4313 (unused key item with empty name/info/caption slots) rewritten in every EquipParamGoods
  copy in memory: refCategory 2 / refId 7511, bell animation 14, Sinister Bell icon 7, usable in every
  state, not consumed. Texts (msg 10/20/24 id 4313) via gen_messages.py fill(). Goods 201/206/226 are the
  bells' ringing forms (replaceItemId_bySpEffect) - not free.
- Effect: SpEffect 7511 (unreferenced in params and scripts) = copy of 9000 with stateInfo (+0x156) 0 and
  1 s duration. Detected in the local player's SpEffect list [chr+0x1C8]+8 (+0x48 row, +0x58 next).
- Given once: inventory [player->vtable[0x1C8](player)+0x5B0], has-item check = the inventory hash
  lookup, add = 0x014D9A80(inv, &(0x40000000|4313), 1). Saves (home\<user>\savedata\CUSA00207 and
  savedata\<user>\CUSA00207, user 1004 skipped) copied to <mod>\save-backups\before-duel-bell-<time>
  first; no backup = no bell.
- PvP: team chr+0x88 (0x018C2A00: player type 0/1/2 -> team 19/20/21). While on, every OTHER player
  character is written team 21 each tick; original teams restored when off. Host decides: relay "SHAD"
  0x25 kind 0 = host state (every 1 s), kind 1 = guest asks. Off when the session ends.
- FindBbscParamTables takes min rows / first row id (goods table: 295 rows, first id 1).

## 2026-10-05 (c) mod 19 - hunter's kit, own prompts, invasions everywhere
DLL 305633DD. Includes mod 18.
- Kit (given once per character, new characters too; saves backed up first): Hunt Bell 4016 (copy of
  Beckoning Bell 200), Kin Bell 4121 (205), Bloodlust Bell 11201 (225), Duel Bell 4313, Parting Blank
  11251 (Silencing Blank 111). No Insight cost. Goes to the carried items. The vanilla 200/201/205/206/
  225/226/111 are taken out of the carried items and the storage box (TESTED: "taken out ... (gone)").
  Co-op with the new bells TESTED.
- Prompts: gen_messages.py replaces the bell/blank prompts in menu categories 2/30/200/201/204 (158
  messages).
- Host Silencing Blank: host with SpEffect 7 -> session patches suspended (the guest was frozen and the
  host fell to the title). TESTED that the guest's leave works.
- Invasions everywhere (switch SHADPS4_BBSC_INVADE_ANYWHERE, default on - it was off because unknown
  switches default to off): Bell Maiden SpEffect 9020 kept on every living player in their own world /
  hosting (AddSpEffect chr->vtable[0x3F0]). While the player has a sign out (9005 / 9025) 9020 is taken
  off (0x017E1D90(manager, node, 1), as EraseEventSpecialEffect 0x013183D0 does): with 9020 on both,
  the host found and claimed the invader sign and signalled, but the invader never joined. TESTED
  14:57: invader joined (JoinSession_ForceJoin, chr_type 2).
- Invader death rules (UNTESTED): invaders never count as partners (LivingPartnerExists); a dead
  invader -> persistence suspended + vanilla death reload (home); host dies with no co-op partner alive
  and an invader present -> same (session ends, invader home); invader alive and the host dead 6 s ->
  persistence suspended + CSMultiPlayMan::Stop. Logs "BBSC death: ... invader", "BBSC invasion won".
- HP cut: the fix now also clears 9026 (invader) and 9036/9046/9056 (other phantom kinds), all 0.7
  maxHpRate. UNTESTED.
