BBSC - HOST SERVER PACKAGE
==========================
Lets you host BBSC (Bloodborne Seamless Co-op) for your friends: the shadNet server (accounts,
matchmaking, co-op summons) and the BBSC game server (messages, bloodstains, server list).
Every player, you included, also needs the BBSC game package (BBSC-<build>.zip: launcher +
emulator + BBSC mod) and their own copy of Bloodborne. No game files are included here.

WHAT YOU NEED
  - Windows 10/11
  - One network you and your friends are all on: Radmin VPN, Hamachi, Tailscale, ZeroTier,
    or the same house (same router / Wi-Fi).
    Not a privacy VPN (ProtonVPN, NordVPN...) - friends cannot reach you through those.

FIRST TIME (HOST)
  1. Unzip this folder somewhere permanent (e.g. C:\BBSC-Host). Do not run it from inside the zip.
  2. Double-click START-HOST.cmd
     - if this PC is on more than one network it asks which one your friends use
       (START-HOST-OTHER-NETWORK.cmd asks again later)
     - say Yes to the Windows prompt (installs the C++ runtime, opens the firewall for VPN
       addresses and your own local network only - nothing is opened to the internet;
       older BBSC firewall rules that only allowed Radmin are updated)
     - it creates your own empty account database and your own registration key
     - it points YOUR game at your servers too
  3. Double-click GENERATE-CARD.cmd. It asks a name, uses the same address as START-HOST
     (Enter) and makes cards\BBSC-card-<name>.json with the address and your registration key.
     Send that file to your friends in a DM - it holds your key, never post it publicly.
  4. Your account: BBSC launcher > Settings > Accounts, pick your card, type a name and
     password, Create Account on Server. (Or NEW-ACCOUNT.cmd, for yourself or a friend.)

YOUR FRIENDS
  1. Drag your card onto JOIN-HOST.cmd (in the BBSC game package). It connects their game to
     your servers and adds the card to their launcher.
     (No card? Double-click JOIN-HOST and paste the address START-HOST showed.)
  2. BBSC launcher > Settings > Accounts > pick the card > name + password >
     Create Account on Server. Accounts from other hosts do not exist on your server.

EVERY TIME (HOST)
  Double-click START-HOST.cmd and keep the two small server windows open while you play.
  Changed network (e.g. Radmin to Hamachi)? START-HOST-OTHER-NETWORK.cmd, then GENERATE-CARD
  again and send the new card.

playit.gg
  Account creation works through a playit tunnel, but co-op needs ports 31313-31315, 20443 and
  20444 with the same numbers on one address, which playit's free tunnels do not give. Use
  Radmin VPN, Hamachi or Tailscale for playing.

KEEP PRIVATE
  server\shadnet.cfg (your registration key), server\db\ (everyone's accounts),
  server\host-address.txt and cards\ (your key again) are yours.
  Never share them or zip them up for someone else - give out a fresh copy of this package.

LOGS
  server\logs\ - send these with a bug report.

CREDITS
  See CREDITS.txt. shadNet server: shadPS4 team / Wozzardman (shadnet-p2p). Server list format
  and game server groundwork: Fahad (fhody125), No Dream For The Hunter.

SOURCE CODE (GPL-2.0-or-later)
  shadnet.exe and shadnet-sample.exe are built from https://github.com/Wozzardman/shadnet-p2p
  (commit 5a849a1). The emulator BBSC runs on is built from https://github.com/Wozzardman/shadp2p
  with the BBSC changes. Source for the BBSC changes is available from CLS_AMG on request.