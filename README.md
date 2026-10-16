# Bloodborne Seamless Co-op (BBSC)

Play the whole of Bloodborne together. BBSC is a mod for the [shadPS4](https://github.com/shadps4-emu/shadPS4)
emulator that keeps a co-op session alive through deaths, lamps, area changes, bosses and the Hunter's
Dream - up to five hunters, each keeping their own character.

**Downloads:** Nexus Mods (link coming with the release) · **Community:** [Discord](https://discord.gg/Df7BCmfWdt)

> 0.0.1 is the first public test. Back up your saves before playing.

## Features

- One co-op session that lasts through deaths, lamps, area changes, bosses and the Hunter's Dream
- Up to 5 hunters: the host and 4 guests
- Guests follow the host between areas and lamps and respawn with the group
- Everyone keeps their own levels, echoes and items
- Shared progress (optional): the host's boss kills and shortcuts are added to the guest's world
- Guests get boss rewards and enemy drops; items already picked up do not come back
- Enemy scaling by the number of hunters
- A co-op kit with no Insight cost: Hunt Bell, Kin Bell, Bloodlust Bell, Parting Blank, Duel Bell (PvP toggle)
- Clear co-op messages in place of the vanilla "beckoner / resonance" lines
- Public server, self-hosted servers (Radmin VPN, Tailscale, LAN, VPS) and split screen (Nucleus Co-op)
- Runs in shadPS4 and in BBport (the Windows build; Linux: test build, see `docs/LINUX-BBPORT.md`)

## Requirements

- Your own Bloodborne **CUSA03173, version 01.09** - no game files are included
- Windows (the server also runs on Linux), or BBport for Linux (`docs/LINUX-BBPORT.md`)

## Downloads

| File | For |
|---|---|
| `BBSC-<version>-Play.zip` | Players: launcher + emulator + BBSC, connects to the public server |
| `BBSC-<version>-Server.zip` | Hosting your own server (Windows) |
| `BBSC-<version>-Nucleus.zip` | Split screen with the [Bloodborne Nucleus Co-op Handler](https://github.com/framilano/BloodborneNucleusCoopHandler) |
| `BBSC-<version>-BBport.zip` | Test build for the [BBport Windows build](https://github.com/Supermedo/bloodborne_pc) - same server as shadPS4 players |
| `BBSC-<version>-BBport-Linux.tar.gz` | Test build for BBport on Linux: extract, run `./install.sh` (`docs/LINUX-BBPORT.md`) |

## Quick start

1. Unzip the Play package and keep its files together.
2. Run `PLAY BBSC.cmd`. Your shadPS4 settings, patches and saves are used as they are.
3. Launcher > Settings > Accounts > Create Account on Server.
4. Host rings the Hunt Bell, guests ring the Kin Bell.

## Repository layout

| Folder | Contents |
|---|---|
| `emulator/` | The shadPS4 fork (shadp2p based). BBSC game code: `src/core/bloodborne_re.cpp`; the mod DLL: `mod/` |
| `launcher/` | The Qt launcher fork (Mods and Accounts tabs) |
| `server/` | Host package scripts and the BBSC game server (`bbsc_gameserver.py`) |
| `tools/` | Game data readers and helper tools |
| `docs/` | Architecture, world sync, networking, known limitations, changelog |

## Building

- Emulator: `build/build-shadp2p.cmd` (Windows, Visual Studio 2022, CMake, Ninja)
- Mod DLL: `emulator/mod/build-mod.cmd`
- Mod for BBport on Linux: `emulator/mod/build-mod.sh` (`docs/LINUX-BBPORT.md`)
- Game server: Python 3, no extra packages (`server/gameserver/bbsc_gameserver.py --shadnet <address>`)

## Reporting bugs

Post in `#bug-reports` on the [Discord](https://discord.gg/Df7BCmfWdt) with the version, host or guest,
what happened and when, and both logs: the BBSC log (`mods/CUSA03173/bbsc/logs/`) and the emulator log
(`user/log/shad_log.txt` or `%APPDATA%\shadPS4\log\`).

## Credits

- **CLS_AMG** - BBSC
- **framilano (fra98_)** - Bloodborne Nucleus Co-op Handler; tester
- **Wozzardman** - [shadp2p](https://github.com/Wozzardman/shadp2p) and shadnet-p2p, the online fork BBSC is built on
- **Fahad (fhody125)** - No Dream For The Hunter, the game server groundwork (MIT)
- **shadPS4 team and contributors** - the emulator; **shadNet team** - shadNet
- **The Hunter's Dream team**
- **deadinside28** (bbport) and **Supermedo** (its Windows build)
- **Testers** - Hardwater, Onesadeye, aboood888848, Fahad, Mob, lelial, J2mF, Kukar3x
- Supporters: see `SUPPORTERS.txt`

## Support

BBSC is free for everyone. If you want to support the time that goes into it:
[Patreon](https://www.patreon.com/c/CLS_AMG) or [Buy Me a Coffee](https://buymeacoffee.com/j2mf).

## License

GPL-2.0-or-later, like shadPS4. See `LICENSE` and `NOTICE.md`. Third-party parts keep their own licenses.

Bloodborne is developed by FromSoftware and published by Sony Interactive Entertainment. BBSC is a free
fan project and is not affiliated with or endorsed by either.
