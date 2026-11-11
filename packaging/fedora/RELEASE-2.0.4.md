# Fedora 44 / Fedora 45: KDEGlance 2.0.4

The release uses the existing, separately built x86_64 RPMs without rebuilding
or modifying them. Both RPM headers report kdeglance 2.0.4, release 1.fc44 / 1.fc45,
and architecture x86_64. Header and payload SHA-256 digests were verified.

| RPM | SHA-256 |
| --- | --- |
| kdeglance-2.0.4-1.fc44.x86_64.rpm | `245200bc21c90b5d7b4ec6c70ec4b70df3fb4a31a033b4ea314e83ce37ace9a7` |
| kdeglance-2.0.4-1.fc45.x86_64.rpm | `a8fb50d6274992557d888843d0638cff625438c950bfd525360c8d64e15deed5` |

Both SRPMs embed the identical `kdeglance-2.0.4.tar.gz`, SHA-256
`edf3ffe9b320fdf86fe9aef68ec4d723dea68c1f0d7b2728921c5581b5c8825e`.
All 150 files in that archive were compared with the release tree. Application
code, tests, data, version metadata and the Fedora spec match byte for byte.
Only release documentation was finalized afterward. Embedded RPM documentation
therefore still describes the unpublished candidate. Installed KWin script and
AppStream metadata were also compared directly with the RPM payloads.

The release owner completed manual acceptance, including scaling and video
playback. Existing automated build results: Fedora 45 16/16 CTest suites passed;
Fedora 44 15/16 passed, with an RLE TGA format-test failure, followed by packaging
with `--nocheck`. RPMs are unsigned. Fedora 45 container/offscreen testing does
not certify a complete Fedora 45 KDE Wayland/Live session.

The published 2.0.3 Arch/CachyOS package and its build configuration are unchanged.
See [release notes](../../RELEASE-NOTES.md) for the fixes and downloads.
