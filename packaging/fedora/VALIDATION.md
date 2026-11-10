# Fedora validation — 2026-11-06

Local port validation, not a published release or a clean `mock` build.

## System

| Component | Tested version |
|---|---|
| Fedora KDE Plasma Desktop | 44, x86_64 |
| Plasma workspace | 6.6.4 |
| Dolphin / dolphin-libs | 25.12.3 |
| Qt | 6.10.2 |
| KDE Frameworks | 6.25.0 |
| GCC | 16.0.1 |
| CMake | 4.3.0 |
| Poppler Qt6 | 26.01.0 |
| LibRaw | 0.22.1 |
| systemd | 259.5 |
| Session | Plasma Wayland |

Installing development packages with `updates` enabled encountered an existing
repository conflict: `kf6-kmime` 6.31 conflicts with files owned by `kmime`
25.12.3. That transaction failed before installation. Development dependencies
were then installed with `dnf --disable-repo=updates install ...`, using versions
matching the existing desktop. No desktop update or package removal was needed.
This is an environment workaround, not a KDEGlance dependency or repository
configuration change.

## Build and automated results

- Native CMake/Ninja build with all optional compiled features enabled passed.
- All **16/16 CTest sets passed**, both in the development build and in RPM
  `%check`: unit, image, media, preview, service, stress, formats, mock Dolphin
  integration, KWin script, AV1 fallback, documents, remote resolver and default
  opening. Tests requiring private D-Bus and loopback ran outside the restricted
  execution sandbox, without root.
- Desktop entry and offline AppStream validation passed.
- RPM built with Fedora hardening/LTO flags and baseline x86_64 settings;
  binary RPM, SRPM, debuginfo and debugsource were generated locally.
- RPM runtime requirements contain no LibreOffice dependency or weak dependency.
- Without LibreOffice (hidden only inside bubblewrap), the missing-converter
  regression passed and text/PDF remained usable. Probing 79 generated media
  files produced **0 mandatory failures**. Experimental MIDI lacked
  FluidSynth/SoundFont. The host LibreOffice installation was retained.
- The generator could not create ICNS, HEIC, H.264, HEVC, MTS, F4V and E-AC-3
  samples with the available encoders. Some other uncommon audio formats have
  no available sample encoder. These are reported in `SKIPPED.txt`; their
  absence is not decoder certification. The H.264-named test file uses the
  generator's existing MPEG-4 fallback. AV1 decoding was tested separately.
- Source archive hygiene and version consistency passed. The metadata checker
  now decodes percent-escaped Markdown links and also checks the Fedora version.
- Arch-style CMake configuration with `lib/qt6/plugins` remains valid. The Arch
  PKGBUILD, dependency list and pinned release checksum are unchanged. An actual
  Arch rebuild was not performed on this Fedora host.

## Installed RPM lifecycle and desktop

`dnf install` installed only KDEGlance; no runtime dependency additions or
desktop upgrades were required. `rpm -V kdeglance` passed when run outside the
sandbox (the sandbox maps system file ownership, making its ownership-only
verification output unsuitable for this check).

The installed `/usr/bin/kdeglance` reports 2.0.2. On the real Plasma session,
`test-installed.py` verified:

- D-Bus activation starts the installed systemd user unit.
- The KWin placement script loads.
- A dedicated real Dolphin process loads the installed overlay hook.
- Private model and selection access are available.
- Space opens an image, the two-file navigation list matches Dolphin, and
  navigation synchronizes the Dolphin selection.
- Closing works and the installed context-menu action opens a preview.

The test uses synthetic files and injected Qt events through the existing
development adapter. It does not certify physical Wayland seat input, gestures,
all display layouts, network protocols or all codecs.

The service was activated again before `dnf remove --no-autoremove kdeglance`.
Removal stopped the service, released the D-Bus name and removed all owned
program files. No user settings file existed to checksum during this run;
the RPM scriptlets contain no user-settings deletion. Dependencies were retained.
The final system state has KDEGlance **uninstalled**, with local RPMs available
for installation.

## Packaging warnings

`rpmlint`: **0 errors**, three warnings:

- `package-with-huge-docs`: existing screenshot documentation dominates size.
- `no-manual-page-for-binary`: the project provides README/CLI help, no man page.
- `invalid-url Source0`: the port intentionally uses a locally generated source
  archive; no Fedora-port release URL has been published.

Packages are unsigned local artifacts. No GitHub push, PR, tag or release was
created.
