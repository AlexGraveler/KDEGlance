#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic document fixtures; only Office conversion needs LibreOffice."""
import os, pathlib, shutil, subprocess, sys, tempfile

out = pathlib.Path(sys.argv[1]).resolve()
mode = sys.argv[2] if len(sys.argv) > 2 else 'all'
if mode not in ('all', 'core', 'office'):
    raise SystemExit('mode must be all, core or office')
out.mkdir(parents=True, exist_ok=True)


def write_pdf(path):
    # A complete PDF fixture using the standard library, independent of Office.
    stream = b'BT /F1 18 Tf 40 400 Td (KDEGlance independent PDF preview) Tj ET\n'
    objects = [
        b'<< /Type /Catalog /Pages 2 0 R >>',
        b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 640 480] '
        b'/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>',
        b'<< /Length ' + str(len(stream)).encode() + b' >>\nstream\n' + stream + b'endstream',
        b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    ]
    pdf = bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n')
    offsets = [0]
    for number, obj in enumerate(objects, 1):
        offsets.append(len(pdf))
        pdf.extend(f'{number} 0 obj\n'.encode() + obj + b'\nendobj\n')
    startxref = len(pdf)
    pdf.extend(f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode())
    for offset in offsets[1:]:
        pdf.extend(f'{offset:010d} 00000 n \n'.encode())
    pdf.extend(f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{startxref}\n%%EOF\n'.encode())
    path.write_bytes(pdf)


if mode != 'office':
    text = 'KDEGlance\nРусский текст\nDeutsch: Größe und Straße\nEnglish text\n'
    (out/'unicode.txt').write_text(text, encoding='utf-8')
    (out/'utf16.txt').write_text(text, encoding='utf-16')
    (out/'cyrillic.log').write_bytes(('Русский текст\n'*10).encode('cp1251'))
    (out/'german.txt').write_bytes('Größe und Straße\n'.encode('cp1252'))
    (out/'example.md').write_text('# Heading\n\n**Bold** text and a list:\n\n- first\n- second\n', encoding='utf-8')
    write_pdf(out/'standalone.pdf')
if mode == 'core':
    raise SystemExit(0)

office = '/usr/bin/libreoffice' if os.access('/usr/bin/libreoffice', os.X_OK) else shutil.which('libreoffice')
if not office:
    print('SKIP: Office conversions require optional LibreOffice (DOC, DOCX, DOCM, RTF, ODT, OTT, FODT and Office-exported PDF).', flush=True)
    raise SystemExit(77 if mode == 'office' else 0)

source = pathlib.Path(__file__).with_name('sample.fodt')
(out/'sample.fodt').write_bytes(source.read_bytes())
formats = [('doc','MS Word 97'),('docx','Office Open XML Text'),('docm','MS Word 2007 XML VBA'),('rtf','Rich Text Format'),('odt','writer8'),('ott','writer8_template'),('pdf','writer_pdf_Export')]
with tempfile.TemporaryDirectory(prefix='kdeglance-fixtures-') as profile:
    env = os.environ.copy()
    env['SAL_USE_VCLPLUGIN'] = 'svp'
    env.pop('LD_PRELOAD', None)
    for extension, filter_name in formats:
        path = out/('sample.'+extension)
        if path.exists():
            path.unlink()
        command = [office, '-env:UserInstallation='+pathlib.Path(profile).as_uri(), '--headless', '--norestore', '--convert-to', extension+':'+filter_name, '--outdir', str(out), str(out/'sample.fodt')]
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=60)
        print(extension, result.returncode, result.stdout.strip(), result.stderr.strip(), flush=True)
        if result.returncode or not path.exists() or not path.stat().st_size:
            raise RuntimeError('Could not generate '+extension)
