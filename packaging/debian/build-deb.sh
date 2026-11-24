#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a binary .deb from the current working tree.
#
# dpkg-buildpackage only reads debian/ from the source root, so the tree is
# staged into packaging/debian/build/source and this recipe is copied in as
# source/debian. Only tracked files are staged: uncommitted edits are included,
# build output and local junk are not.
#
# usage: packaging/debian/build-deb.sh
set -eu

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$here/../.." && pwd)
build="$here/build"
stage="$build/source"

if [ "$(id -u)" -eq 0 ]; then
    echo "Run as a regular user; dpkg-buildpackage must not run as root." >&2
    exit 1
fi

version=$(cat "$root/VERSION")
changelog_version=$(dpkg-parsechangelog -l "$here/changelog" -S Version)
changelog_version=${changelog_version%-*}
if [ "$version" != "$changelog_version" ]; then
    echo "packaging/debian/changelog is $changelog_version but VERSION is $version" >&2
    exit 1
fi

rm -rf "$stage"
mkdir -p "$stage/debian/source"
(cd "$root" && git ls-files -z | tar --null -T - -cf -) | tar -xf - -C "$stage"
cp -a "$here/control" "$here/rules" "$here/changelog" "$here/copyright" "$stage/debian/"
cp -a "$here/kdeglance.prerm" "$stage/debian/kdeglance.prerm"
cp -a "$here/source/format" "$stage/debian/source/format"

echo ">>> Building kdeglance $version in $stage"
cd "$stage"
dpkg-buildpackage -b -us -uc -j"$(nproc)"

echo ">>> Package written to:"
ls -1 "$build"/kdeglance_"${version}"-*.deb
