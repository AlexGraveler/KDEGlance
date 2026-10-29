# KDEGlance 2.0.0 manual desktop acceptance

Use synthetic or explicitly selected files in a normal Plasma Wayland session.
Do not change global KDE bindings/MIME associations or remove unrelated files.
Qt event injection and KWin method calls do not replace these physical checks.

1. Restart Dolphin after installation. Space on a file opens; Space/Escape closes.
   Space in rename/location/filter fields and on directories retains Dolphin behavior.
2. Navigate a mixed image/video/audio/text/PDF/office folder. Arrow/side-click
   navigation follows its order and updates only the original Dolphin window.
   Repeat with two Dolphin windows, context-menu opening and many close/open cycles.
3. Photo defaults to natural size bounded by work area. Wheel grows/shrinks the
   photo and window, continues image zoom at the window limit, and zooms out of a
   crop before shrinking the window. Check a noncentral cursor and fast reversals
   with both a wheel and a high-resolution touchpad.
4. **Plain drag pans an overflowing photo; Alt-drag moves the window.** Confirm
   window movement preserves local scale/offset. Fitted content can also move the
   window by plain drag. Alt at a border/corner starts proportional resize; without
   Alt freely change the window shape and inspect undistorted photo/black bars.
5. Manually resize, close/reopen and restart the viewer: manual dimensions return.
   Temporary wheel size, zoom and pan do not persist. Double-middle reset, close,
   reopen, move and zoom again: cursor behavior remains correct.
6. Drag to the top/left/right screen edges; test native KWin maximize/tile/restore.
   No stale-position jump should throw the window away. Close/reopen after maximize
   and confirm manual size returns. Evaluate physical resize/drag smoothness.
7. Video: play/pause, seek by bar and wheel, volume/mute, fullscreen/Escape,
   four counterclockwise rotations and wheel zoom/pan. Click Repeat on; let the
   video end and confirm it restarts. Turn Repeat off and confirm it stops at end.
8. Audio with/without artwork: title/artist, play/pause, seek, volume/mute, file
   switch. Closing or Enter must immediately stop audible playback.
9. Text: UTF-8, UTF-16, representative Cyrillic/Western legacy files, Markdown M,
   chunk continuation, doubled scrolling, document dragging and Shift selection.
   PDF/Writer: continuous pages, scrolling, zoom, fit and reflow for text-layer PDFs.
   No permanent menus/toolbars or system title bars should appear in preview views.
10. Enter and keypad Enter on each module open the original file with its actual
    default application. Holding Enter must not cause duplicate launches. Repeat
    with an accessible SMB file/server; do not equate an HTTP fixture with SMB.
11. Give all six modules distinct sizes/preferences. Double-middle reset one module
    and confirm only it resets immediately; restart and confirm the others persist.
12. Check focus-loss behavior according to the current preference, KWin global
    Alt/Meta bindings, another screen/DPI if available, and a longer normal session.

Record which physical scenarios were actually performed. Unavailable hardware,
network servers or compositor input automation must be listed as unverified.
