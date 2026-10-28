#!/bin/bash
# Builds the package in a clean Arch chroot (devtools), runs check() there,
# then installs, inspects and removes it in a throw-away copy of the chroot.
# Nothing on the running system is changed. Needs: sudo pacman -S devtools
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CHROOT="${CHROOT:-$HOME/.cache/kdeglance-chroot}"
cd "$HERE"
./make-source-tarball.sh

if [ ! -d "$CHROOT/root" ]; then
  mkdir -p "$CHROOT"
  mkarchroot "$CHROOT/root" base-devel
fi
arch-nspawn "$CHROOT/root" pacman -Syu --noconfirm

# Build + check() in a fresh copy of the clean root.
makechrootpkg -c -r "$CHROOT"

PKG="$(ls -t "$HERE"/kdeglance-*.pkg.tar.zst | grep -v debug | head -1)"
echo "=== built: $PKG"

# Install / upgrade / remove test in another throw-away copy.
TEST="$CHROOT/installtest"
sudo rm -rf "$TEST"
sudo cp -a "$CHROOT/root" "$TEST"
sudo cp "$PKG" "$TEST/root/"
sudo arch-nspawn "$TEST" pacman -U --noconfirm "/root/$(basename "$PKG")"
sudo arch-nspawn "$TEST" bash -c '
  set -e
  pacman -Ql kdeglance
  test -x /usr/bin/kdeglance
  test -f /usr/lib/qt6/plugins/kf6/overlayicon/kdeglance_dolphinhook.so
  test -f /usr/lib/qt6/plugins/kf6/kfileitemaction/kdeglance_quicklook.so
  test -f /usr/share/dbus-1/services/org.kde.kdeglance.service
  test -f /usr/lib/systemd/user/kdeglance.service
  test -f /usr/share/kdeglance/kwin/placement.js
  QT_QPA_PLATFORM=offscreen kdeglance --version
  pacman -Qkk kdeglance
'
sudo arch-nspawn "$TEST" pacman -U --noconfirm "/root/$(basename "$PKG")"   # reinstall = upgrade path
sudo arch-nspawn "$TEST" pacman -Rns --noconfirm kdeglance
sudo arch-nspawn "$TEST" bash -c '! pacman -Q kdeglance && ! test -e /usr/bin/kdeglance && ! test -e /usr/lib/qt6/plugins/kf6/overlayicon/kdeglance_dolphinhook.so'
sudo rm -rf "$TEST"
echo "=== clean chroot: build, tests, install, upgrade and removal OK"
