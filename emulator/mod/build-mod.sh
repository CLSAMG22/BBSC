#!/usr/bin/env bash
# Builds the BBSC mod for BBport on Linux: libbbsc.so, which linux/bbsc-probe preloads into
# BBport's bb-probe (the counterpart of bbsc.dll and bbsc-launch.exe, build-mod.cmd).
# Output: <repo>/build/mod-linux/BBSC-<version>-BBport-Linux.tar.gz, the package for players (its
# folder next to it): libbbsc.so, bbsc-probe, install.sh, README.txt and the files the mod checks
# for. <version> is mod.json's. Objects in <repo>/build/mod-linux/obj/.
# libbbsc.so = the DLL's sources + bbsc_platform_linux.cpp. The C++ runtime is linked in and no
# symbol is exported: BBport (the AppImage) runs bb-probe with its own glibc and libstdc++, which
# must not mix with this build's. CXX picks the compiler (default g++).
set -euo pipefail
src=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$src/../.." && pwd)
out=$repo/build/mod-linux
obj=$out/obj
version=$(grep -o '"version": *"[^"]*"' "$src/mod.json" | head -1 | grep -o '[0-9][^"]*')
name=BBSC-$version-BBport-Linux
pkg=$out/$name
ext=$src/../externals
cxx=${CXX:-g++}
for header in "$ext/fmt/include/fmt/format.h" "$ext/xbyak/xbyak/xbyak.h"; do
    if [[ ! -f $header ]]; then
        echo "Missing $header - run: git submodule update --init" \
             "emulator/externals/fmt emulator/externals/xbyak" >&2
        exit 1
    fi
done
flags=(-std=c++23 -O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Wall
       -include "$src/linux/glibc_compat.h"
       -DBBSC_MOD -DFMT_HEADER_ONLY
       -I"$src/shim" -I"$src/../src" -I"$ext/fmt/include" -I"$ext/xbyak")
sources=("$src/bbsc_mod.cpp" "$src/bbsc_hooks.cpp" "$src/bbsc_relay.cpp" "$src/bbsc_channel.cpp"
         "$src/bbsc_http.cpp" "$src/bbsc_discord.cpp" "$src/bbsc_platform_linux.cpp"
         "$src/../src/core/bbsc_memscan.cpp" "$src/../src/core/bloodborne_re.cpp")
rm -rf "$pkg" "$pkg.tar.gz"
mkdir -p "$obj" "$pkg"
pids=()
objects=()
for source in "${sources[@]}"; do
    object=$obj/$(basename -- "${source%.cpp}").o
    objects+=("$object")
    "$cxx" "${flags[@]}" -c "$source" -o "$object" &
    pids+=($!)
done
failed=0
for pid in "${pids[@]}"; do
    wait "$pid" || failed=1
done
if (( failed )); then
    echo "build-mod.sh: compilation failed" >&2
    exit 1
fi
"$cxx" -shared -o "$pkg/libbbsc.so" "${objects[@]}" -static-libstdc++ -static-libgcc -pthread \
    -Wl,--exclude-libs,ALL -Wl,--version-script="$src/linux/libbbsc.map" -Wl,-z,defs -Wl,--as-needed
install -m 755 "$src/linux/bbsc-probe" "$src/linux/install.sh" "$pkg/"
install -m 644 "$src/linux/README.txt" "$pkg/README.txt"
ln -sfn bbsc-probe "$pkg/bb-gpu-capabilities"
# The mod stays off without them (bbsc_mod.cpp NoticesPresent), as in the Windows packages.
install -m 644 "$repo/LICENSE" "$pkg/LICENSE.txt"
install -m 644 "$repo/NOTICE.md" "$pkg/NOTICE.txt"
install -m 644 "$repo/SUPPORTERS.txt" "$pkg/SUPPORTERS.txt"
tar -czf "$pkg.tar.gz" -C "$out" --owner=0 --group=0 "$name"
echo "Built $pkg.tar.gz"
