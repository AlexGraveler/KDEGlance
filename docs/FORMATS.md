# KDEGlance 2.0.0 format support

Source of truth: `src/core/filetype.cpp`, `src/viewer/formatregistry.cpp`, the
installed Qt plugins, LibRaw, FFmpeg, Poppler and LibreOffice. File-type detection
uses MIME/content with extension fallbacks; it does not make every named codec,
camera model or document variant universally decodable.

| Group | Supported/recognized types | Backend and limits |
|---|---|---|
| Common images | JPEG/JFIF, PNG/APNG, GIF, BMP/DIB, TIFF, WebP, ICO/CUR, ICNS, TGA including RLE, PNM/PBM/PGM/PPM | Qt image plugins; TIFF can have multiple pages; animation depends on decoder support |
| Extended images | PCX, PSD/PSB composite, Radiance HDR, AVIF, HEIF/HEIC/HIF, JPEG XL, JPEG 2000, OpenEXR | kimageformats and corresponding codec libraries; conditional formats require their plugin |
| Vector | SVG/SVGZ | Qt SVG; preview rendering, not editing |
| Camera RAW | CR2/CR3/CRW, NEF/NRW, ARW/SRF/SR2, RAF, ORF, RW2/RWL, PEF/PTX, DNG, SRW; other LibRaw formats | Embedded preview or full LibRaw decode; exact camera/version matters |
| Additional RAW | 3FR/FFF, IIQ/EIP, ERF, DCR/KDC/DCS, MRW, MOS, MEF and others recognized by the registry | Conditional; Sigma X3F is experimental; no claim of fixtures for every camera |
| Video containers | MP4/M4V, MKV, WebM, MOV/QT, AVI/DivX, WMV/ASF, MPEG/MPG/MPE/M1V/M2V, TS/MTS/M2TS, VOB, FLV/F4V, 3GP/3G2, OGV, MXF, DV, RM/RMVB | Qt Multimedia FFmpeg backend; RealMedia and some production containers are conditional |
| Video codec fixtures | H.264, HEVC, AV1, VP8, VP9, MPEG-4/Xvid, MPEG-2, MJPEG, ProRes, DNxHD | Actual decoded frames are tested; Vulkan failure is covered by software fallback tests |
| Audio | MP3/MP2/MP1, FLAC, WAV/WAVE, AIFF/AIFC, AAC/M4A/M4B/ALAC, Vorbis/OGG/OGA, Opus, WMA, APE, WavPack, Musepack, TAK, TTA, Shorten, AC-3/E-AC-3, DTS, AU/SND | FFmpeg backend; support follows installed codecs |
| Conditional audio | AMR, RealAudio, MOD/XM/S3M/IT and other trackers | Backend dependent; tracker playback is not a library/playlist manager |
| MIDI | MID/MIDI/KAR/RMI | Experimental FluidSynth rendering; needs a SoundFont; missing tools are reported |
| Text | TXT, LOG, MD/MARKDOWN, CSV, INI, JSON, XML, YAML/YML, CONF, TOML and common source text | Read-only Qt text view; automatic encoding heuristic; CSV/JSON/XML are displayed as text, not structured editors |
| PDF | PDF | Poppler Qt6; continuous pages, asynchronous raster cache, zoom, optional text-layer reflow; no OCR |
| Writer documents | DOC, DOCX, DOCM, RTF, ODT, OTT, FODT | Headless LibreOffice conversion to temporary PDF; tested fixtures are generated from synthetic FODT |
| Apple Pages | PAGES | Experimental LibreOffice import only; not guaranteed or listed as stable office support |

QOI, XCF, Krita/OpenRaster and some uncommon registry entries are experimental.
Recognition without a working decoder is reported honestly. No preview support
for spreadsheets, slides, archive extraction or document editing is promised.

## Remote URLs

KIO Fuse is tried for a local readable mount; otherwise a cancellable KIO copy
supplies a temporary local file. Original URLs are retained for Enter/KIO opening.
A working KIO protocol handler, server credentials and network connection are
required. Protocol recognition is not proof that every SMB/AFC/SFTP server has
been tested. See the release validation report for the actual network checks.

## Reproduce format checks

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(formats|documentformats|av1fallback)$' --output-on-failure
QT_QPA_PLATFORM=offscreen build/src/kdeglance --probe-formats /path/to/samples
```

Mandatory decoder failures fail the format test. Conditional/experimental formats
are distinguished in its output rather than silently removed. Release validation
records sample counts and any missing optional SoundFont separately.
