#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
VIEWER="$1"
WORK="$2"
python3 "$(dirname "$0")/generate-samples.py" "$WORK"
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" "$VIEWER" --probe-formats "$WORK"
