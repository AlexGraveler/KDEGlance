# KDEGlance 2.0.0

## Overview

KDEGlance 2.0.0 expands Dolphin's Quick Look-style previews with text, Markdown,
PDF and office-document modules alongside images, camera RAW, animations, audio
and video. This is the stable C++20 / Qt 6 / KDE Frameworks 6 implementation,
packaged for CachyOS and Arch Linux with KDE Plasma 6 and Wayland.

The existing GitHub tags v1.0.0 and v1.0.1 belong to the earlier Python/PyQt6
implementation. Their image, audio and video capabilities are not presented as
new inventions in this release. The current C++ implementation and document
modules are a substantial expansion relative to that published baseline.
The final locally accepted playback, photo zoom and mouse behavior is preserved.

## Highlights

- Read-only text/Markdown, continuous PDF viewing and LibreOffice-based Writer
  document previews in the same minimal, frameless application.
- Enter and keypad Enter close the preview and open the original URL in the
  system's default application through KDE KIO, without leaving playback running.
- Explicit separation of manual window geometry, automatic fitting and temporary
  photo zoom. Only manually resized window dimensions are remembered.
- Independent image, video, audio, PDF, text and office size/settings groups;
  double-middle-click reset affects the active module only.
- Native Dolphin hook/context-menu plugins, resident D-Bus service, source-window
  selection synchronization and a temporary KWin placement helper.
- Software video decoding fallback, broad installed-format support and a checked
  Arch package workflow. No global KDE mouse/shortcut configuration is modified.

## Supported File Types

- Images: JPEG, PNG/APNG, GIF, BMP, TIFF, WebP, ICO/ICNS, TGA, PNM, PCX,
  PSD/PSB composite, SVG/SVGZ and HDR. Installed plugins add AVIF, HEIF/HEIC,
  JPEG XL, JPEG 2000 and OpenEXR.
- RAW: LibRaw-supported cameras, including Canon CR2/CR3, Nikon NEF, Sony ARW,
  Fujifilm RAF, Olympus ORF, Panasonic RW2, Pentax PEF and DNG.
- Video: common MP4/MKV/WebM/MOV/AVI/MPEG/TS/WMV/FLV/3GP/OGV containers;
  actual codec support depends on Qt Multimedia/FFmpeg. Release fixtures include
  H.264, HEVC, AV1, VP8/VP9, Xvid, MJPEG, ProRes and DNxHD.
- Audio: MP3, FLAC, WAV/AIFF, AAC/M4A/ALAC, Vorbis, Opus, WMA, WavPack,
  APE and other FFmpeg-supported codecs. MIDI needs FluidSynth and a SoundFont
  and remains experimental.
- Text: TXT, LOG, MD, CSV, INI, JSON, XML, YAML and plain-text source/config files.
- Documents: PDF; DOC, DOCX, DOCM, RTF, ODT, OTT and FODT through LibreOffice.

See [FORMATS.md](docs/FORMATS.md) for dependency and experimental-format limits.
No spreadsheet, presentation, archive-editing or general office-suite support
is claimed. Recognizing an extension does not guarantee every variant decodes.

## Image Preview

Images initially use natural size, bounded by the work area. A saved manual
window size is restored and the image is fitted without distortion. Ordinary
border resizing allows arbitrary window proportions and black bars; Alt held
when starting a border/corner resize preserves the photo's ratio.

Wheel zoom follows the cursor within the available pan range. The window grows
or shrinks with the displayed photo until the work-area limit; zoom then continues
inside the fixed window. Zooming out first removes the crop, then shrinks the
window. Wheel scale, temporary window size and pan are not saved.

**The final controls are: ordinary drag pans an overflowing image; Alt + drag
moves the whole window.** A fitted image can also be dragged to move the window
when there is nothing to pan. Window movement does not alter local photo state.
Rotation/reflection are view-only. EXIF orientation, animation and multi-page
TIFF handling remain available.

## Video Preview

Video includes play/pause, progress-bar seeking, volume/mute, fullscreen, view-only
zoom/pan and 90-degree counterclockwise rotation. There is one rotation button;
four presses return to the original orientation. Side clicks use the shared
mixed-file navigation and avoid the playback controls.

**Repeat playback is supported.** The two-arrow Repeat toggle restarts the current
video when it reaches the end. It is distinct from the rotation button and its
behavior has not been removed or redesigned.

KIO Fuse can expose network files to the player, with a cancellable KIO copy
fallback. Hardware decoding is used when usable; software decoding is selected
when hardware checks fail. Closing/switching stops the previous source.

## Audio Preview

A compact audio player shows title, artist and embedded/adjacent artwork when
available, or a placeholder. It provides play/pause, seeking, volume/mute and
shared file navigation. Audio has its own remembered window size and volume/mute
preferences. Closing the preview or pressing Enter stops its playback.

## Documents and PDF

Text uses automatic BOM/Unicode and legacy-encoding detection, including common
Cyrillic encodings, with no encoding dropdown or permanent toolbar. Markdown can
switch between source and rendered content. Read-only scrolling keeps the existing
doubled wheel speed. Large text is loaded progressively.

PDF uses Poppler for continuous pages, scrolling, fit-to-width, zoom and optional
text reflow. Page rendering is asynchronous and cached. Reflow depends on a text
layer; it does not perform OCR.

Writer documents are converted to a temporary PDF by headless LibreOffice using
an isolated profile with macros/link updates disabled. The original is not edited.
Import fidelity depends on LibreOffice and the source file.

## Keyboard and Mouse Controls

| Input | Action |
|---|---|
| Space in Dolphin | Open/toggle preview |
| Space / Escape in viewer | Close; Escape leaves fullscreen first |
| Enter / keypad Enter | Open original file in its default application and close preview |
| Left / Right; side content clicks | Previous / next file |
| Center click; single middle click | Close |
| Double middle click | Reset active module only |
| Wheel on photo | Temporary zoom and adaptive window size |
| Ordinary drag on overflowing photo / Alt + drag photo | Pan image / move window |
| Drag border without Alt / with Alt | Free / photo-proportional manual resize |
| Video Repeat | Repeat current video |
| Video rotation button or R | Rotate 90 degrees counterclockwise |
| Video F / double click | Toggle fullscreen |
| Wheel on seek / volume controls | Seek / change volume |
| Image +/−, F/0, 1 | Zoom, fit, 100% |
| Markdown M | Rendered/source toggle |
| PDF/Office Ctrl+wheel or +/−, F/0, R | Zoom, fit width, reflow |

The README contains the full module-specific control table. KDEGlance does not
replace KWin's global gesture configuration.

## Installation

Download `kdeglance-2.0.0-1-x86_64.pkg.tar.zst` and the checksum file from this
release. Verify the downloaded release files, then:

```bash
sudo pacman -U ./kdeglance-2.0.0-1-x86_64.pkg.tar.zst
```

Restart Dolphin after installation/update. The viewer starts on demand through
D-Bus. For a source build, extract `kdeglance-2.0.0.tar.gz`, enter `packaging/arch`,
run `./make-source-tarball.sh`, then `makepkg -si` as a regular user.

The package upgrades an earlier Pacman installation under the same `kdeglance`
package name. An older manually installed Python launcher/service may shadow the
packaged viewer; read [migration instructions](docs/INSTALL.md#updating-and-migration).
No old user files or shortcuts are automatically deleted.

## Requirements

CachyOS/Arch Linux, KDE Plasma 6, Dolphin, KWin and a Wayland user session;
Qt 6.8+, KDE Frameworks 6, Qt Multimedia/FFmpeg, KDE image plugins, LibRaw,
Qt 5Compat, Poppler Qt6, LibreOffice Writer and KIO Fuse. The package declares the
runtime dependencies. C++20, CMake, Ninja, pkgconf and base-devel build the source.

Only an x86_64 binary is supplied. No untested Fedora/Debian/other-distribution
packages are attached.

## Known Limitations

- Codec/camera variants and document layout fidelity depend on system libraries;
  experimental formats are not guaranteed.
- Dolphin integration uses private symbols and may need adaptation for future
  Dolphin releases.
- KWin owns placement, tiling and maximization. Physical dragging, smoothness,
  all Alt/Meta binding combinations and mixed-DPI multi-monitor operation require
  manual acceptance beyond Qt event injection.
- Automatic legacy encoding detection is heuristic. PDF reflow is not OCR.
- Network preview/Enter behavior depends on KIO, credentials and server support.
  HTTP fixtures are not a universal SMB/AFC certification.
- Image scale/pan is temporary; manual window size persists. This is intentional.
- Validation details distinguish clean compilation, automatic tests, real desktop
  windows and remaining physical-input checks in
  [RELEASE-VALIDATION.md](docs/RELEASE-VALIDATION.md).
