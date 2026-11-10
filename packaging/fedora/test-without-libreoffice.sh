#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Hide the converter only inside an unprivileged mount namespace, never uninstall it.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=$(realpath "${1:-$root/build}")
command -v bwrap >/dev/null
viewer="$build/src/kdeglance"
test -x "$viewer"
test -x "$build/tests/documenttest"
# Bind onto the resolved target as /usr/bin/libreoffice may be a symlink.
office=$(readlink -f /usr/bin/libreoffice)
mask=()
if [[ -e "$office" ]]; then mask=(--ro-bind /dev/null "$office"); fi
run_without_office() {
    bwrap --ro-bind / / --bind "$build/tests" "$build/tests" \
        --tmpfs /tmp --dev /dev --proc /proc "${mask[@]}" \
        --setenv QT_QPA_PLATFORM offscreen --setenv KDEGLANCE_LANG en \
        --setenv XDG_CONFIG_HOME "$build/tests/config-without-office" -- "$@"
}
run_without_office "$build/tests/documenttest" officeUnavailableKeepsOtherViewsWorking
# The generated media suite exercises all available non-Office decoders.
test -d "$build/tests/format-samples"
run_without_office "$viewer" --probe-formats "$build/tests/format-samples"
