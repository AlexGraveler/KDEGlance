#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Semi-automatic checks of an INSTALLED KDEGlance on the real Plasma desktop
# with the real Dolphin (spec 16: integration with real Dolphin and Plasma
# Wayland). Run it from a terminal inside your Plasma session:
#
#   tests/desktop/run-desktop-checks.sh [cycles]
#
# It creates a test folder, opens it in a new Dolphin window and drives the
# preview through D-Bus (the same code path as the Space key). Do not touch
# the mouse/keyboard while it runs (about a minute). A report is written to
# ~/kdeglance-desktop-report.txt. Things only a human can judge are listed in
# tests/desktop/MANUAL-CHECKLIST.md.

set -u
CYCLES="${1:-30}"
REPORT="$HOME/kdeglance-desktop-report.txt"
DIR="${XDG_RUNTIME_DIR:-/tmp}/kdeglance-desktop-test"
PASS=0
FAIL=0
exec > >(tee "$REPORT") 2>&1

ok() { echo "PASS  $*"; PASS=$((PASS + 1)); }
bad() { echo "FAIL  $*"; FAIL=$((FAIL + 1)); }
info() { echo "INFO  $*"; }

viewer() { gdbus call --session -d org.kde.kdeglance -o /Viewer -m "org.kde.kdeglance.Viewer.$1" "${@:2}" 2>/dev/null; }
# gdbus prints ('json',) - strip the tuple syntax.
unwrap() { sed -E "s/^\('(.*)',\)$/\1/"; }
status() { viewer Status | unwrap; }
field() { python3 -c 'import json,sys; d=json.loads(sys.argv[1] or "{}"); v=d.get(sys.argv[2]); print("" if v is None else (str(v).lower() if isinstance(v,bool) else v))' "$1" "$2"; }
now_ms() { date +%s%3N; }

wait_for() { # wait_for <timeout ms> <command...>
    local limit=$1; shift
    local start; start=$(now_ms)
    while ! "$@"; do
        [ $(( $(now_ms) - start )) -gt "$limit" ] && return 1
        sleep 0.02
    done
    return 0
}
preview_visible() { [ "$(field "$(status)" visible)" = "true" ]; }
preview_hidden() { [ "$(field "$(status)" visible)" = "false" ]; }

echo "== KDEGlance desktop checks $(date -Iseconds)"
info "session: ${XDG_SESSION_TYPE:-?}, desktop: ${XDG_CURRENT_DESKTOP:-?}"
info "plasma: $(plasmashell --version 2>/dev/null | tail -1), dolphin: $(dolphin --version 2>/dev/null | tail -1), qt: $(qtpaths6 --qt-version 2>/dev/null)"
command -v kscreen-doctor >/dev/null && kscreen-doctor -o 2>/dev/null | sed -n 's/\x1b\[[0-9;]*m//g; /Output:\|Scale\|Geometry/p' | sed 's/^/INFO  /'

# --- 1. installation -------------------------------------------------------
PLUGINDIR="$(qtpaths6 --plugin-dir 2>/dev/null || echo /usr/lib/qt6/plugins)"
for f in /usr/bin/kdeglance "$PLUGINDIR/kf6/overlayicon/kdeglance_dolphinhook.so" "$PLUGINDIR/kf6/kfileitemaction/kdeglance_quicklook.so" \
         /usr/share/dbus-1/services/org.kde.kdeglance.service /usr/lib/systemd/user/kdeglance.service /usr/share/kdeglance/kwin/placement.js; do
    [ -e "$f" ] && ok "installed: $f" || bad "missing: $f"
done

# --- 2. service activation and KWin script --------------------------------
kdeglance --quit >/dev/null 2>&1; sleep 0.5
t0=$(now_ms)
if viewer Ping >/dev/null; then ok "D-Bus activation started the service in $(( $(now_ms) - t0 )) ms"; else bad "service could not be activated"; fi
sleep 1.5
[ "$(field "$(status)" kwinScript)" = "true" ] && ok "KWin placement script loaded" || bad "KWin placement script not loaded (window centring/focus may differ)"

# --- 3. test folder and Dolphin ---------------------------------------------
rm -rf "$DIR"; mkdir -p "$DIR"
for i in 1 2 3 4 5; do
    ffmpeg -nostdin -loglevel error -y -f lavfi -i "testsrc=size=$((800 + i * 200))x$((600 + i * 50))" -frames:v 1 "$DIR/photo$i.jpg"
done
ffmpeg -nostdin -loglevel error -y -f lavfi -i testsrc=size=640x360:rate=25 -f lavfi -i sine -t 4 -c:v libx264 -pix_fmt yuv420p -c:a aac "$DIR/photo6-video.mp4"
ffmpeg -nostdin -loglevel error -y -f lavfi -i sine=duration=4 -metadata title="Desktop Test" -metadata artist=KDEGlance "$DIR/photo7-audio.mp3"
FILES=$(ls "$DIR" | wc -l)
info "test folder: $DIR ($FILES files)"

before=$(gdbus call --session -d org.freedesktop.DBus -o /org/freedesktop/DBus -m org.freedesktop.DBus.ListNames | tr ',' '\n' | grep -o "org.kde.dolphin-[0-9]*" | sort)
dolphin --new-window "$DIR" >/dev/null 2>&1 &
sleep 3
names=$(gdbus call --session -d org.freedesktop.DBus -o /org/freedesktop/DBus -m org.freedesktop.DBus.ListNames | tr ',' '\n' | grep -o "org.kde.dolphin-[0-9]*" | sort)
DOLPHIN=""
for n in $names; do
    st=$(gdbus call --session -d "$n" -o /KDEGlanceBridge -m org.kde.kdeglance.Bridge.State 2>/dev/null | unwrap)
    if [ -n "$st" ] && [ "$(field "$st" viewFileCount)" = "$FILES" ]; then DOLPHIN=$n; fi
done
[ -z "$DOLPHIN" ] && for n in $names; do DOLPHIN=$n; done
bridge() { gdbus call --session -d "$DOLPHIN" -o /KDEGlanceBridge -m "org.kde.kdeglance.Bridge.$1" 2>/dev/null; }
BSTATE=$(bridge State | unwrap)
if [ -z "$BSTATE" ]; then
    bad "KDEGlance hook not loaded in Dolphin ($DOLPHIN) - restart all Dolphin windows"
else
    ok "hook active in $DOLPHIN"
    [ "$(field "$BSTATE" modelAccess)" = "true" ] && ok "Dolphin view order available (model access)" || bad "no model access: $(field "$BSTATE" missingSymbols)"
    [ "$(field "$BSTATE" selectionAccess)" = "true" ] && ok "selection sync available" || bad "no selection access: $(field "$BSTATE" missingSymbols)"
    [ "$(field "$BSTATE" focusInFileView)" = "true" ] && ok "keyboard focus is in the file view" || info "focus is not in Dolphin's file view (keep the new Dolphin window focused)"
fi

# --- 4. open, navigate, sync, close -------------------------------------------
t0=$(now_ms)
bridge TriggerFocusedView >/dev/null
if wait_for 5000 preview_visible; then
    ok "preview opened in $(( $(now_ms) - t0 )) ms"
    S=$(status)
    [ "$(field "$S" count)" = "$FILES" ] && ok "navigation list = Dolphin's $FILES files" || bad "navigation list has $(field "$S" count) files"
    [ "$(field "$S" active)" = "true" ] && ok "preview window has keyboard focus" || bad "preview window did not get the focus"
    first=$(field "$S" url)
    viewer Navigate 1 >/dev/null; sleep 0.4
    second=$(field "$(status)" url)
    [ "$first" != "$second" ] && ok "arrow navigation: $(basename "$first") -> $(basename "$second")" || bad "navigation did not change the file"
    target=$(field "$(bridge State | unwrap)" target)
    [ "$target" = "$second" ] && ok "Dolphin selection follows the preview" || bad "Dolphin selection is '$target'"
    viewer Close >/dev/null
    wait_for 3000 preview_hidden && ok "closed" || bad "did not close"
    sleep 0.3
    target=$(field "$(bridge State | unwrap)" target)
    [ "$target" = "$second" ] && ok "selection stays on the last previewed file" || bad "selection after close is '$target'"
else
    bad "preview did not open"
fi

# --- 5. media -------------------------------------------------------------------
gdbus call --session -d org.kde.kdeglance -o /Viewer -m org.kde.kdeglance.Viewer.Open "['file://$DIR/photo6-video.mp4']" 0 "{}" >/dev/null
sleep 1.5
S=$(status)
[ "$(field "$S" hasVideo)" = "true" ] && [ "$(field "$S" playing)" = "true" ] && ok "video plays" || bad "video: $S"
gdbus call --session -d org.kde.kdeglance -o /Viewer -m org.kde.kdeglance.Viewer.Open "['file://$DIR/photo7-audio.mp3']" 0 "{}" >/dev/null
sleep 1.5
S=$(status)
[ "$(field "$S" title)" = "Desktop Test" ] && [ "$(field "$S" playing)" = "true" ] && ok "audio mini player (title, playing)" || bad "audio: $S"
viewer Close >/dev/null; sleep 0.3
[ "$(field "$(status)" playing)" != "true" ] && ok "no sound after closing" || bad "still playing after close"

# --- 6. open/close cycles -----------------------------------------------------
total=0; okc=0
for i in $(seq 1 "$CYCLES"); do
    t0=$(now_ms)
    bridge TriggerFocusedView >/dev/null
    if wait_for 5000 preview_visible; then
        total=$(( total + $(now_ms) - t0 )); okc=$((okc + 1))
    fi
    viewer Close >/dev/null
    wait_for 3000 preview_hidden
    sleep 0.1
done
[ "$okc" = "$CYCLES" ] && ok "$CYCLES open/close cycles, average open $(( total / (okc > 0 ? okc : 1) )) ms" || bad "only $okc of $CYCLES cycles opened"
BSTATE=$(bridge State | unwrap)
[ "$(field "$BSTATE" hotkey)" != "" ] && ok "hotkey still configured after cycles: $(field "$BSTATE" hotkey)" || bad "bridge state lost"
info "service memory: $(ps -o rss= -C kdeglance | awk '{s+=$1} END {printf "%.0f MiB", s/1024}')"

echo
echo "== $PASS passed, $FAIL failed. Report: $REPORT"
echo "== Now go through tests/desktop/MANUAL-CHECKLIST.md (visual and hands-on checks)."
[ "$FAIL" = 0 ]
