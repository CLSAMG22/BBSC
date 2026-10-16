#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Installs BBSC for BBport on Linux from this package (build-mod.sh makes it) and adds
# "Bloodborne (BBSC)" to the application menu: BBport's own launcher, started with BB_PROBE pointing
# at bbsc-probe, so every game it starts has BBSC. BBport started any other way stays without it.
#
#   ./install.sh [BBport AppImage, or launcher/bb-launcher.sh of a source build]
#   ./install.sh --uninstall
#
# Without a path, the newest Bloodborne-bbport*.AppImage next to this folder, in ~/Applications,
# ~/Downloads, ~/Games or ~ is used. The mod goes to ~/.local/share/bbport/bbsc (BBSC_DIR: another
# folder); logs, captures and save backups already there are kept. bbsc-play in it starts the same
# launcher from a terminal (bbsc-play --play: the game with the saved settings, no window).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
dest=${BBSC_DIR:-$data/bbport/bbsc}
desktop=$data/applications/bbsc-bbport.desktop
files=(libbbsc.so bbsc-probe bb-gpu-capabilities LICENSE.txt NOTICE.txt SUPPORTERS.txt)

if [[ ${1:-} == --uninstall ]]; then
    rm -f "$desktop" "$dest/bbsc-play"
    echo "Menu entry removed. $dest is kept (logs, save backups); delete it by hand if you want."
    exit 0
fi
for file in "${files[@]}"; do
    if [[ ! -e $here/$file ]]; then
        echo "BBSC: $file is missing next to install.sh - extract the whole package again" >&2
        exit 1
    fi
done

launcher=${1:-}
if [[ -z $launcher ]]; then
    shopt -s nullglob
    for folder in "$here/.." "$HOME/Applications" "$HOME/Downloads" "$HOME/Games" "$HOME"; do
        for candidate in "$folder"/Bloodborne-bbport*.AppImage; do
            if [[ -z $launcher || $candidate -nt $launcher ]]; then
                launcher=$candidate
            fi
        done
    done
    shopt -u nullglob
    if [[ -z $launcher ]]; then
        echo "BBSC: BBport's AppImage was not found. Give its path:" \
             "$0 /path/to/Bloodborne-bbport-x86_64.AppImage" >&2
        exit 1
    fi
fi
launcher=$(realpath -- "$launcher")
if [[ ! -f $launcher ]]; then
    echo "BBSC: $launcher does not exist" >&2
    exit 1
fi
if [[ ! -x $launcher ]]; then
    chmod +x -- "$launcher"
    echo "Made $launcher executable"
fi

mkdir -p "$dest" "${desktop%/*}"
for file in "${files[@]}"; do
    cp -a -- "$here/$file" "$dest/"
done
dest=$(cd -- "$dest" && pwd)
{
    echo '#!/usr/bin/env bash'
    echo '# Starts BBport with BBSC. Written by install.sh: run it again if BBport moves.'
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
echo "BBport: $launcher"
echo "Start the game with \"Bloodborne (BBSC)\" in the application menu, or $dest/bbsc-play"
# A BB_PROBE in the launcher's extra variables wins over this one and adds BBSC to every start.
settings=${XDG_CONFIG_HOME:-$HOME/.config}/bbport-launcher/settings.json
if [[ -f $settings ]] && grep -q 'BB_PROBE=' "$settings"; then
    echo "Note: BBport's launcher still has BB_PROBE in Settings > Developer > Extra variables -" \
         "remove it there" >&2
fi
