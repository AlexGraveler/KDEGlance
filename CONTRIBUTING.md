# Contributing

Use a current Arch/CachyOS Plasma 6 Wayland development session. Read INSTALL.md
and TESTING.md first. Changes are GPL-3.0-or-later unless an existing third-party
file carries its own compatible license; retain all third-party notices.

Keep decoding and media access off the GUI thread. Preserve cancellation and
request-generation checks. Do not rewrite working display/playback components
to add a backend. Existing D-Bus, desktop-action and service identities need a
migration plan if changed: users may have saved Space bindings.

Run the relevant isolated tests and the package checks. Add a targeted test for
behavioral fixes, not a test that merely repeats implementation details. Never
run a developer install/uninstall against a real user HOME as part of automation.
Report native Wayland/Dolphin/manual checks separately from offscreen tests.

Use original or explicitly licensed fixtures. Never commit personal media,
credentials, logs, absolute developer-home paths, generated .so files, or .work/.
Include build/OS versions in bug reports, but remove private paths/server names.
No public CI badge or clean-machine compatibility claim should be added without
an actual corresponding run.
