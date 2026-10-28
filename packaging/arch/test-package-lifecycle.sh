#!/bin/bash
# File/database lifecycle only. Uses a disposable root; never runs host hooks.
# Full runtime dependencies and post_upgrade are checked separately on the host.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 OLD_PACKAGE NEW_PACKAGE" >&2; exit 2; fi
OLD="$(realpath "$1")"
NEW="$(realpath "$2")"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kdeglance-package-test.XXXXXXXX")"
trap 'rm -rf -- "$TEST_ROOT"' EXIT
fakeroot -- bash -s -- "$TEST_ROOT" "$OLD" "$NEW" <<'SH'
set -euo pipefail
root="$1";old="$2";new="$3"
mkdir -p "$root/var/lib/pacman" "$root/empty-hooks" "$root/var/cache/pacman/pkg"
cat > "$root/pacman.conf" <<'CONF'
[options]
Architecture = auto
SigLevel = Never
LocalFileSigLevel = Never
CONF
pm=(pacman --root "$root" --dbpath "$root/var/lib/pacman" --config "$root/pacman.conf" --cachedir "$root/var/cache/pacman/pkg" --hookdir "$root/empty-hooks")
# The temporary root deliberately has no OS dependencies or executable /bin/sh.
# Do not misrepresent -dd/--noscriptlet as a dependency or maintainer-script test.
"${pm[@]}" -Udd --noscriptlet --noconfirm "$old"
"${pm[@]}" -Q kdeglance
"${pm[@]}" -Udd --noscriptlet --noconfirm "$new"
"${pm[@]}" -Q kdeglance
"${pm[@]}" -Qkk kdeglance
for file in usr/bin/kdeglance usr/lib/qt6/plugins/kf6/overlayicon/kdeglance_dolphinhook.so usr/lib/qt6/plugins/kf6/kfileitemaction/kdeglance_quicklook.so usr/share/dbus-1/services/org.kde.kdeglance.service usr/lib/systemd/user/kdeglance.service usr/share/kdeglance/kwin/placement.js; do
 test -f "$root/$file"
done
mapfile -t owned < <("${pm[@]}" -Qlq kdeglance)
"${pm[@]}" -Rdd --noscriptlet --noconfirm kdeglance
if "${pm[@]}" -Q kdeglance; then echo 'package remained installed' >&2; exit 1; fi
for file in "${owned[@]}"; do
 [[ "$file" == */ ]] && continue
 if [ -e "$root$file" ] || [ -L "$root$file" ]; then echo "leftover: $file" >&2; exit 1; fi
done
echo 'Isolated Pacman file/database install, upgrade, integrity and removal PASS'
SH
