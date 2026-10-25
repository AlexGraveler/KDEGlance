# KDEGlance 1.0.1-1 — Arch/CachyOS package testing

This is a local AUR preparation, not an AUR publication. Keep pkgver=1.0.1 and
pkgrel=1 for this first test. Do not run makepkg as root. Test actual installation
and GUI behavior on the separate CachyOS KDE laptop, not the primary computer.

## Build and install on the test laptop

Copy this entire packaging/aur directory to the laptop as ~/kdeglance-aur,
including PKGBUILD, .SRCINFO, all helpers/templates and this document. Alternatively,
extract the provided kdeglance-1.0.1-1.src.tar.gz and use its kdeglance directory. Files in
source= are checksummed. The 1.0.1 source archive is LOCAL: keep the prepared KDEGlance-1.0.1.tar.gz
alongside PKGBUILD. The transferable source bundle already includes it. No
published GitHub 1.0.1 release is assumed; neither install.sh nor sudo runs
inside package(). After an approved upload, download/verify the actual asset
before replacing source[0] with its real release URL.

```bash
sudo pacman -Syu --needed base-devel
cd ~/kdeglance-aur
makepkg --verifysource
makepkg -si
```

Review dependencies and pacman prompts. makepkg -s installs missing build/runtime
packages with your confirmation; -i installs the resulting package. The package
uses gcc/pkgconf from base-devel to build its native bridge, and desktop-file-utils
as a build validation dependency. Runtime does not need a compiler or test tools.

The package files are installed under /usr/lib/kdeglance, /usr/bin,
/usr/lib/systemd/user, /usr/share/dbus-1/services, /usr/share/kio/servicemenus,
/usr/share/applications, /usr/share/licenses and /usr/share/doc.
All are tracked by pacman. No user HOME files, autostart enablement, keyboard
configuration, service starts or Dolphin restarts occur during package building
or installation. System Mutagen is used; no bundled library duplication.

## First launch and Dolphin setup (normal desktop user)

Run in a Plasma 6 Wayland graphical session, not via sudo:

```bash
kdeglance-setup --check
systemctl --user daemon-reload
busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
kdeglance-setup --enable-dolphin
kbuildsycoca6 --noincremental
```

The explicit --enable-dolphin command backs up an existing user Dolphin launcher
and writes only that launcher's accessibility environment. It does not alter Space,
restart Dolphin or enable/start KDEGlance. Close all Dolphin windows and reopen
Dolphin yourself with the normal launcher. If another instance remains running,
fully quit it yourself first so its accessibility environment is refreshed.

In Dolphin: Settings → Configure Keyboard Shortcuts → KDEGlance: Quick Look
(or its translated name), then assign Space or another key after reviewing
conflicts. The action name/ID is retained from manual installation. D-Bus starts
the same kdeglance2.service lazily when a preview/client state request is made.
The service is not enabled globally or started by package installation.

```bash
kdeglance --version
kdeglance state
systemctl --user status kdeglance2.service
journalctl --user -u kdeglance2.service -b --no-pager -n 50
```

Do not share private media paths/server addresses from state/logs. The initial
state command activates the daemon; --version alone only reports the version.

## Migration from install.sh (per desktop user)

Never run the manual uninstaller as root. Existing manual files live under HOME,
so they do not collide with pacman's /usr files; they can shadow the package's
D-Bus/user service/action. Package commands refuse these legacy overrides, listing
exact paths, rather than removing them or starting a second viewer.

1. Close the preview. Optionally back up ~/.config/kdeglance2, which contains
   audio/video preferences. The old uninstaller preserves these by default.
2. Remove the old manual installation with its own manifest-based manager:

   ```bash
   ~/.local/bin/kdeglance-manage uninstall
   ```

   Alternatively, run `bash uninstall.sh` from the old extracted manual installer.
   Do not use --purge/--purge-settings and do not delete the backup directories.
   If the old manager refuses changed files, preserve/review them before continuing;
   do not force-delete them or use pacman --overwrite.
3. Install the package with makepkg -si (if not already installed), then run:

   ```bash
   hash -r
   kdeglance-setup --check
   ```

4. If files restored by the old uninstaller still shadow the system identities,
   --check lists them. Review and back them up manually; unrelated user overrides
   are not automatically removed. Continue only when --check succeeds.
5. Follow the first-launch/setup commands above. Settings/logs/backups retain their
   old XDG paths; preferences are not migrated, reset or owned by pacman.

A package may be installed while another user's manual version still exists; that
user must migrate separately. The original installer remains unchanged and can
still be used without the package. Avoid switching back until the package has
been removed; old manual overrides deliberately take precedence for that user.
No automatic cross-user migration or service termination is performed.

## Update/reinstall

Review the new PKGBUILD and checksum changes, then run makepkg -si again. For
rebuilding the same 1.0.1-1 test package use makepkg --force, then install it with:

```bash
sudo pacman -U ./kdeglance-1.0.1-1-x86_64.pkg.tar.zst
```

Pacman leaves ~/.config/kdeglance2 and HOME backups/logs untouched. Close the
preview and explicitly restart the user service after an update when convenient;
no automatic restart is performed by this package. Validate the reported version.

After publication, `paru -S kdeglance` is the expected AUR workflow. That command
is not a claim that this preparation is already published or available in AUR.

## Remove and restore optional user setup

Before removing the package, close the preview and run as the desktop user:

```bash
kdeglance-setup --restore-dolphin
systemctl --user disable --now kdeglance2.service
sudo pacman -R kdeglance
systemctl --user daemon-reload
busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
kbuildsycoca6 --noincremental
```

Only the explicit helper restores the user launcher it previously backed up; it
refuses if that launcher/backup was modified afterward. If setup was not used,
restore is a safe no-op. Shortcuts, preferences and logs are retained. Pacman
removes only its tracked system files and does not enumerate/kill desktop users.
Stop the service before removal: uninstalling files alone does not guarantee an
already running process has exited. Reopen Dolphin yourself to refresh its menu.

## Acceptance checklist on the separate laptop

Record each result; do not infer GUI success from build/static/offscreen checks:

1. Clean makepkg -si, pacman -Ql kdeglance, pacman -Qkk kdeglance.
2. --check detects manual overrides; setup creates no shortcut changes.
3. --version/state and user service/D-Bus activation use /usr paths and one daemon.
4. Actual photo/GIF/HEIC/CR2, embedded/no-cover audio and known-good video playback.
5. Dolphin Space/context menu, mixed navigation/order/current keyboard item,
   focus-loss close, zoom/transforms, seek/volume/fullscreen and hover controls.
6. Restart the desktop session and verify lazy activation with no forced autostart.
7. Removal with preferences intact; no package files left or foreign files removed.
8. Reinstall and same-version upgrade preserve preferences and helper backups.
9. Manual-to-package migration, including intentionally changed user overrides.
10. Run namcap PKGBUILD and namcap on the built package; investigate warnings.

CachyOS Plasma Wayland is the known working application baseline and the package
build-test host. The source release's media support was tested separately; this
does not establish native GUI acceptance of the package. Arch is the packaging
target, with clean-machine acceptance outstanding. Fedora/Kubuntu/X11 and other
CPU architectures are not officially tested/supported by this PKGBUILD.

Reference: https://man.archlinux.org/man/PKGBUILD.5.en

## Build and review limitations

The local binary is for inspection; rebuild on the test laptop. makepkg honors
the builder's CXXFLAGS/LDFLAGS, and CachyOS may use -march=native. Do not assume
a binary built on another CPU/Qt installation is portable to an old laptop.

namcap may warn about local imports such as core.VERSION or audio.SeekBar because
the private /usr/lib/kdeglance modules are not site-packages. They are shipped
and exercised by staged-package tests. It may also mark dynamic Qt plugins,
Dolphin, AT-SPI, KIO and session tools as apparently unused dependencies; these
are runtime integration requirements and should not be deleted blindly. Record
actual namcap output and investigate any new error.

Before eventual AUR submission the owner should review/update the Maintainer
contact comment. This preparation does not invent an email address or publish
anything or change published source assets; this task prepares version 1.0.1 only.

## Diagnosing the confirmed 1.0.0-1 CLI migration problem

The test laptop had /usr/local/bin/kdeglance -> /opt/kdeglance/run_kdeglance.sh
ahead of /usr/bin/kdeglance in PATH. That image-only launcher produced the
"no valid image path" error. It is not the 1.0.x package parser. Dolphin uses an
absolute /usr/bin/kdeglance and was therefore unaffected. No player code is changed.

```bash
type -a kdeglance
/usr/bin/kdeglance --version
/usr/bin/kdeglance state
ls -l /usr/local/bin/kdeglance
readlink /usr/local/bin/kdeglance
pacman -Qo /usr/local/bin/kdeglance
```

On the TEST LAPTOP only, after confirming that the path is the unowned symlink
to exactly /opt/kdeglance/run_kdeglance.sh, preserve it rather than deleting it:

```bash
if [ -L /usr/local/bin/kdeglance ] &&
   [ "$(readlink /usr/local/bin/kdeglance)" = /opt/kdeglance/run_kdeglance.sh ] &&
   ! pacman -Qo /usr/local/bin/kdeglance >/dev/null 2>&1; then
    sudo install -d -m 700 /var/backups/kdeglance-migration
    sudo mv -n -- /usr/local/bin/kdeglance \
        /var/backups/kdeglance-migration/kdeglance-legacy-launcher
fi
hash -r
type -a kdeglance
kdeglance --version
kdeglance state
```

If the backup name already exists, mv -n leaves the original in place: review
the existing backup and choose another name manually. Do not delete /opt files
or overwrite package-owned commands. Aliases/functions or other PATH entries
reported by type -a require their own manual review. The package never performs
this root-level migration automatically and cannot override shell aliases.

## Retired experimental Dolphin menu

The old manual uninstaller restored its original backup, including the pre-2.0
log_selection.py experiment. That explains the remaining menu after uninstall.
Version 1.0.1's manual uninstaller backs up this recognized original instead of
reactivating it. When migrating from an older uninstaller, run explicitly:

```bash
kdeglance-setup --archive-legacy-menu
kdeglance-setup --check
kbuildsycoca6 --noincremental
```

Recognition requires the original action ID, a single Python command pointing
to the user's XDG data/kdeglance-space-probe/log_selection.py, only known keys,
a regular non-symlink file owned by the user, and no extra actions/commands.
The bytes and SHA-256 record are preserved in XDG state/kdeglance-migration.
The old helper program, preferences, shortcut files and prior backups are not
removed. Unknown/customized entries are refused and must be preserved/reviewed
manually. --check is read-only; it never archives a file automatically.

## Release status

The user confirmed real makepkg -si, D-Bus activation, Dolphin integration, Space
preservation and previews for 1.0.0-1 on CachyOS KDE Plasma 6. The new 1.0.1-1
still needs its own laptop upgrade/GUI checks. Do not publish or delete the
previous preparation directory without separate owner approval.
