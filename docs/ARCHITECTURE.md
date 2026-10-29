# KDEGlance 2.0.0 architecture

The application is C++20/Qt6/KF6. The resident viewer, Dolphin plugins, settings
schema and D-Bus identities are preserved from the accepted local implementation.
The release preparation changes metadata/documentation/packaging, not rendering
or interaction logic.

## Processes and activation

Dolphin loads two native plugins: an overlay-plugin hook used as an entry point
(it adds no overlay icons) and a KFileItemAction context-menu plugin. The hook
installs a focused-file-view Space filter and communicates with the resident
`org.kde.kdeglance` service at `/Viewer`. The D-Bus service and systemd user unit
start `kdeglance --service` on demand. The CLI forwards file requests to it.

A `Bridge` per Dolphin process supplies the file order and synchronizes selection
using Dolphin's private symbols plus QObject methods. It restricts shortcut
handling to the file view and diagnoses unavailable private interfaces. Session
IDs keep navigation updates scoped to the source window; another Dolphin window
must not receive its selection updates.

## Viewer responsibilities

| Component | Responsibility |
|---|---|
| `ViewerApp` | Session/list/URL ownership, D-Bus, settings writes, neighbour prefetch, KWin reports |
| `PreviewWindow` | Shared close/navigation/Enter/reset controls, content switching, viewport geometry and explicit resize origin |
| `ImageView`, `ImageLoader`, `AnimationPlayer` | Image decoding, animation, local zoom/offset, transforms and cache |
| `MediaView`, `MediaControls` | Qt Multimedia playback, seek/volume, Repeat, video rotation/zoom, artwork and metadata |
| `TextView` | Read-only chunked text, encoding heuristic and Markdown |
| `PdfView` | Poppler continuous pages, background rendering, bounded cache, zoom and text reflow |
| `OfficeView` | Cancellable LibreOffice conversion with isolated temporary profile; delegates display to PdfView |
| `InfoView` | Loading/error/unsupported-file cards and recognized stream links |
| `RemoteResolver` | KIO Fuse mount attempt, then temporary cancellable KIO copy |
| `DefaultFileOpener` | Original-URL KIO OpenUrlJob using KDE MIME associations, duplicate/re-entry protection and asynchronous errors |
| `Settings` | Atomic JSON, backup/recovery/migration and independent module groups |
| `KWinIntegration`, `placement.js` | Temporary runtime script, initial placement/focus, work areas and managed photo state |

## Photo state

The window has position, viewport dimensions, remembered manual dimensions and
compositor-managed state. The image has natural dimensions, scalar zoom and local
viewport offset. Moving the window never recomputes image coordinates from its
absolute desktop position.

`ResizeOrigin` separates Automatic, Manual, Wheel and Compositor. Persistence is
granted by an explicit manual resize session, not by resizeEvent alone. Ordinary
resize fits the image into arbitrary window proportions; Alt border/corner resize
uses the oriented photo's ratio.

Wheel events retain one precise image state across the host resize transaction.
The host caps only viewport growth; the requested image scale can continue beyond
it. Zoom-out of a crop keeps the viewport until the photo fits, then shrinks it.
Cursor anchoring uses local coordinates and `Geometry::clampPan` enforces bounds.
No free pan offset exists on an axis whose photo fits entirely in the viewport.

Plain drag pans overflowing photo content; Alt-drag requests system window move.
A fitted photo can move the window with a plain drag. Neither operation persists
image zoom or pan. Image transformations are view-only.

After initial photo placement, KWin owns position/tile/maximize. The script does
not restore a stale top-left over a compositor transition. Managed-state reports
carry the preview session ID, so a late report from an old surface is ignored.
KWin global configuration and bindings are never rewritten.

## Settings and resource lifetimes

`$XDG_CONFIG_HOME/kdeglance/settings.json` retains its existing schema. Image,
video, audio, PDF, text and office window sizes are separate; old document geometry
is migrated into independent document groups. Audio/video volume and mute are
scoped, video Repeat is scoped, autoplay/hardware policies remain global.
Double-middle-click resets only the active module. An explicit whole-application
reset is a separate command/Settings action.

Closing releases media sources and cancels loads/conversions but leaves the
resident service available. File switches invalidate asynchronous generations.
Image neighbours are prefetched; full-resolution decode is requested after zoom
settles. Text/remote/PDF work runs asynchronously. PDF pages use a bounded raster
cache. Office conversion uses temporary files/profile, disables macros/link
updates and does not save to the source. Enter closes/cancels playback before
launching the original URL through KIO, without quitting the resident service.
