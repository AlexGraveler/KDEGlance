# Testing KDEGlance 2.0.0

## Build and mandatory checks

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Arch `makepkg` runs check() automatically. Package release builds must not use
`--nocheck`. Tests use separate configuration directories; D-Bus integration tests
start private buses. Codec/document tests create synthetic media and use the
installed decoders. A successful build is not a desktop-acceptance result.

| CTest set | Coverage |
|---|---|
| settingstest | Defaults, atomic save/recovery, migrations and independent module reset/geometry |
| geometrytest | Fit, aspect resize, viewport cursor anchors and pan bounds |
| filetypetest | MIME/content/extension classification and file navigation |
| imageviewtest | Real decoders, EXIF, animation/TIFF/SVG, async cancellation, image transforms, plain pan/Alt window-move |
| mediaviewtest | Decoded video frames, audio metadata/artwork, playback/seek/volume, rotation and Repeat |
| previewwindowtest | Shared close/navigation, manual vs temporary geometry, zoom beyond screen, cursor/reset/move regression, high-resolution wheel, isolation and compositor states |
| viewerapptest | Session-scoped D-Bus behavior, selection/list ownership, settings and stale KWin reports |
| stresstest | Mixed-media cycles, rapid navigation and resource/idle behavior |
| formats | Generated image/audio/video files decoded by the viewer; mandatory failures fail the test |
| dolphinintegrationtest | Mock Dolphin internals with real plugins and resident service on a private bus |
| kwinplacementtest | Script simulation, including separate geometry/state signals and asynchronous module changes; Node.js required |
| av1fallback | AV1 must deliver a readable video frame with the unusable Vulkan path |
| documenttest | Text encodings/Markdown, PDF/reflow/scroll/cache and office cancellation |
| documentformats | Synthetic TXT/LOG/MD and DOC/DOCX/DOCM/RTF/ODT/OTT/FODT/PDF conversions |
| remoteresolvertest | Private-bus network resolution, delayed HTTP/cancellation and temporary-cache cleanup |
| defaultopentest | Local/original remote URLs, Enter/keypad Enter, stop-before-open, launch failures and resident-service behavior |

Format support is documented in [FORMATS.md](FORMATS.md). Runtime probing is
available through `kdeglance --probe-formats DIR`; optional and experimental
failures must be distinguished from mandatory failures, not hidden.

## Desktop verification

`tests/desktop/check-photo-reset.py` exercises real Wayland windows, Dolphin
plugins, KWin state transitions, photo controls and independent reset with a
**test-only Qt event adapter** built with `-DBUILD_DESKTOP_TEST_ADAPTER=ON`.
It temporarily uses the session D-Bus name, separate settings and synthetic media,
then restores the installed viewer. Run it only when desktop testing has been
authorized and no user preview is open. The adapter is not shipped in the package.
The environment variables documented in the script select a built/extracted or
installed binary, plugin path, KWin script and output directory.

These tests validate application handlers and actual compositor states; they do
not generate physical Wayland seat drags. Real-device motion, smoothness,
modifier interception and electric-edge triggers require the
[manual acceptance checklist](MANUAL-ACCEPTANCE.md).

`tests/desktop/check-system-open.py` checks real default image/text/video GUI
applications. `check-default-open.py` checks Enter/keypad Enter across all modules
with isolated MIME associations and a capture application; it does not replace
the user's associations. Review their scopes before use. Do not change
user MIME associations or use personal files merely to obtain a passing test.
Screenshots use synthetic media and must not expose private documents.

## Package and release validation

- Check CMake, VERSION, plugin metadata, AppStream, PKGBUILD and .SRCINFO agree.
- Build in a fresh directory with the standard Arch recipe and record check().
- Validate desktop entries/AppStream and examine the package file list and RPATH.
- Test isolated Pacman file installation, upgrade and removal without removing
  the running desktop package; report dependency/script isolation limits.
- An actual desktop package replacement requires the user's prior confirmation.
- Verify the archive contains no caches, personal settings, tokens or build output.
- Preserve runtime source behavior from the stable checkpoint; only authorized
  version/license metadata changes are expected.
- Record evidence and manual limits in [RELEASE-VALIDATION.md](RELEASE-VALIDATION.md).
- Publish only after mandatory checks pass. Keep an unsigned local package clearly
  identified as such; checksums verify content, not publisher identity.

The source archive includes the intentional tiny AV1 regression fixture and
synthetic FODT input, not generated media libraries or a developer's settings.
