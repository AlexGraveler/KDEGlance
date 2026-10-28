#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Generates sample files and checks that KDEGlance really decodes them.
# Fails if a sample of a mandatory format does not decode.
# usage: run-formats.sh <kdeglance-binary> <work-dir>
VIEWER="$1"
WORK="$2"
DIR="$(dirname "$0")"
"$DIR/generate_samples.sh" "$WORK" || exit 1
echo "--- formats that could not be generated on this machine:"
cat "$WORK/SKIPPED.txt"
echo "--- decoding:"
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" "$VIEWER" --probe-formats "$WORK"
