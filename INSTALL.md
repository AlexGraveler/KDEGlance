# Installation — KDEGlance 1.0.0

## Supported setup

Use a normal user account in CachyOS/Arch Linux with KDE Plasma 6 and Dolphin
24.02 or newer, in a Wayland session. The installer checks the distribution,
Python/PyQt/Qt, package availability, desktop session, user D-Bus/systemd, and
Qt/KF build metadata. Minimum Python is 3.10; minimum Qt/PyQt is 6.8.

Required Arch packages:

```text
python python-pyqt6 python-gobject qt6-base qt6-multimedia qt6-wayland
qt6-imageformats kimageformats kwindowsystem kservice dolphin kio-fuse
kio-extras at-spi2-core glib2 dbus systemd gcc pkgconf
plasma-workspace
```

The application includes Mutagen's pure-Python source. Test-only dependencies
are listed separately in TESTING.md. No compiler is required during normal
previewing after the activation bridge has been built.

## Check, install, update

```bash
bash install.sh --check
bash install.sh
```

Run from the extracted/cloned project, or use an absolute path to install.sh
from any directory. If Python itself is absent, the shell script first offers
`sudo pacman -Syu --needed python`. Other missing packages are listed after
Python is available. Every package-manager action requires confirmation and
uses `-Syu` to avoid a partial Arch upgrade. The installer never runs pip.
A noninteractive invocation does not silently accept installation prompts.

The local native bridge is built using pkg-config's Qt6Gui and KF6WindowSystem
flags. The installer then backs up its integration targets, copies a versioned
runtime into the user's XDG data directory, verifies the running version, and
only then replaces the Dolphin action. An update uses the same command.
Do not manually replace a running runtime's files.

User installation targets (XDG overrides are respected):

- `~/.local/share/kdeglance2/releases/` and its `current` symlink.
- `~/.local/bin/kdeglance`, `kdeglance2` (compatibility), `kdeglance-manage`.
- `~/.config/systemd/user/kdeglance2.service`.
- `~/.local/share/dbus-1/services/io.github.KDEGlance2.service`.
- `~/.local/share/applications/io.github.KDEGlance2.desktop`.
- `~/.local/share/kio/servicemenus/kdeglance-space-probe.desktop`.
- A backed-up user Dolphin launcher enabling accessibility and disabling its
  D-Bus shortcut launch path so the accessibility environment reaches Dolphin.

The existing `kdeglance2` internal identities are deliberately retained for
compatibility with previous saved Dolphin actions; they are not a version number.
Existing unrelated integration files/symlinks are refused, rather than silently
replaced. Modified owned files are preserved/refused on removal. No global
system desktop files or system services are installed.

## Set Space safely

1. Close and reopen Dolphin using the normal launcher.
2. Open **Settings → Configure Keyboard Shortcuts**.
3. Search **KDEGlance: Quick Look**. In Russian this is
   **KDEGlance: быстрый просмотр**.
4. Assign Space. If Dolphin reports another action using Space, review that
   conflict and choose the desired binding yourself.
5. Select a supported file and press Space. The same action is also available
   in the file's context menu when applicable.

The installer does not edit `kglobalshortcutsrc`, `dolphinrc` or existing keyboard
bindings. If your Dolphin build does not expose service actions in its shortcut
editor, use the context menu/CLI and report the version; there is no unsafe
fallback that rewrites shortcut configuration.

Make sure `~/.local/bin` is on PATH for the CLI. Examples:

```bash
~/.local/bin/kdeglance --version
~/.local/bin/kdeglance state
systemctl --user status kdeglance2.service
journalctl --user -u kdeglance2.service -b --no-pager -n 100
```

You can close a preview before switching to the terminal; doing so is expected
because the preview closes on focus loss. State/logs remain available afterward.
Do not share private filenames, server addresses or credentials from diagnostics.

## Removal

```bash
bash uninstall.sh
```

This disables/stops only KDEGlance's own service, restores backed-up integration,
removes its owned runtime directory, reloads D-Bus/systemd/KDE metadata, and keeps
shared system packages, user preferences, logs and backups. A second removal is
safe and reports that there is nothing to remove.

If the checkout/archive was deleted, `kdeglance-manage uninstall` performs the
manifest-based removal as well. Preferences are kept by default. To explicitly
request their removal while retaining logs/backups:

```bash
bash uninstall.sh --purge-settings
```

That command asks separately before removing only KDEGlance audio.ini/video.ini.
Do not store unrelated files in the application-owned runtime directory.

## Unsupported distributions

This installer deliberately refuses Fedora/Kubuntu and other non-Arch-family
systems. Do not translate the package list blindly. Their packaging and the
experimental universal KIO work are future work, not features of 1.0.0.
