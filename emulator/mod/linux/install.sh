#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Installs the BBSC mod for BBport on Linux (after build-mod.sh) and adds "Bloodborne (BBSC)" to the
# application menu: BBport's own launcher, started with BB_PROBE pointing at bbsc-probe, so every
# game it starts has BBSC. BBport started any other way stays without it.
#
#   install.sh <BBport AppImage or launcher/bb-launcher.sh> [mod folder]
#   install.sh --uninstall [mod folder]
#
# Mod folder: ~/.local/share/bbport/bbsc by default. Logs, captures and save backups already in it
# are kept. bbsc-play in it starts the same launcher from a terminal (bbsc-play --play: the game
# with the saved settings, no window).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$here/../../.." && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
desktop=$data/applications/bbsc-bbport.desktop
usage() {
    echo "usage: $0 <BBport AppImage or bb-launcher.sh> [mod folder] | --uninstall [mod folder]" >&2
    exit 1
}
[[ $# -ge 1 ]] || usage

if [[ $1 == --uninstall ]]; then
    dest=${2:-$data/bbport/bbsc}
    rm -f "$desktop" "$dest/bbsc-play"
    echo "Removed the menu entry. The mod folder $dest is kept (logs, save backups)."
    exit 0
fi

launcher=$(realpath -- "$1")
dest=${2:-$data/bbport/bbsc}
pkg=$repo/build/mod-linux/bbsc
[[ -x $launcher ]] || { echo "Not an executable: $launcher" >&2; exit 1; }
[[ -f $pkg/libbbsc.so ]] || { echo "No build in $pkg: run emulator/mod/build-mod.sh first" >&2; exit 1; }

mkdir -p "$dest" "${desktop%/*}"
cp -a "$pkg/." "$dest/"
dest=$(cd -- "$dest" && pwd)
{
    echo '#!/usr/bin/env bash'
    echo '# Starts BBport with BBSC (written by BBSC'"'"'s install.sh).'
    printf 'export BB_PROBE=%q\n' "$dest/bbsc-probe"
    printf 'exec %q "$@"\n' "$launcher"
} > "$dest/bbsc-play"
chmod 755 "$dest/bbsc-play"
cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Bloodborne (BBSC)
Comment=Bloodborne Seamless Co-op on BBport
Exec="$dest/bbsc-play"
Icon=applications-games
Categories=Game;
EOF
if command -v update-desktop-database > /dev/null; then
    update-desktop-database -q "${desktop%/*}" || true
fi
echo "BBSC installed in $dest"
echo "Menu entry: Bloodborne (BBSC) ($desktop)"
# A BB_PROBE in the launcher's extra variables wins over this one and adds BBSC to every start.
settings=${XDG_CONFIG_HOME:-$HOME/.config}/bbport-launcher/settings.json
if [[ -f $settings ]] && grep -q 'BB_PROBE=' "$settings"; then
    echo "Note: the launcher's extra variables still hold a BB_PROBE - remove it there" >&2
fi
