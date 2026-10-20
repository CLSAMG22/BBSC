BBSC's BBport build - source
============================
emulator\bb-probe.exe is built from:
  https://github.com/Supermedo/bloodborne_pc  commit e5d04e2 ("Version 1.7.2")
plus bbport-1.7.2-bbsc.patch in this folder (git apply). Build: bash build.sh in MSYS2 CLANG64
(see BBport's README). License: GPL-2.0 (emulator\LICENSE-BBport.txt); the shadPS4/shadp2p parts
are GPL-2.0-or-later.
Changes (gpu/shadps4 = BBport's copy of shadp2p):
  - core/libraries/network/p2p_sockets.cpp, sockets.h: relay fallback through the BBSC server
    (UDP 3478) when no direct reply arrives within 2 s
  - core/libraries/np/np_signaling/*: rejoin fix (re-activation checks the peer, new peer
    connection id restarts the handshake)
  - core/libraries/np/np_utility.cpp/.h (new): sceNpLookupNpId answered from the online id
  - np_matching2: peer endpoints for the relay; bbnet/bbnet_glue.cpp registers NpUtility