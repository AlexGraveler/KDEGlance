#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the Dolphin integration test on a private D-Bus session bus on which
# the KDEGlance viewer is D-Bus-activatable (like on the desktop).
# usage: run-integration.sh <test-binary> <kdeglance-binary>
TEST_BIN="$1"
VIEWER_BIN="$2"
T=$(mktemp -d)
mkdir -p "$T/data/dbus-1/services" "$T/config" "$T/runtime"
chmod 700 "$T/runtime"
cat > "$T/data/dbus-1/services/org.kde.kdeglance.service" <<SERVICE
[D-BUS Service]
Name=org.kde.kdeglance
Exec=$VIEWER_BIN --service
SERVICE
export XDG_DATA_HOME="$T/data" XDG_CONFIG_HOME="$T/config" XDG_RUNTIME_DIR="$T/runtime"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" KDEGLANCE_LANG=en
dbus-run-session -- "$TEST_BIN"
rc=$?
rm -rf "$T"
exit $rc
