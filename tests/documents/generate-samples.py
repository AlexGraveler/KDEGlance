#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real document fixtures using only synthetic content and system LibreOffice."""
import os, pathlib, subprocess, sys, tempfile
out=pathlib.Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
source=pathlib.Path(__file__).with_name('sample.fodt')
text='KDEGlance\nРусский текст\nDeutsch: Größe und Straße\nEnglish text\n'
(out/'unicode.txt').write_text(text,encoding='utf-8')
(out/'utf16.txt').write_text(text,encoding='utf-16')
(out/'cyrillic.log').write_bytes(('Русский текст\n'*10).encode('cp1251'))
(out/'german.txt').write_bytes('Größe und Straße\n'.encode('cp1252'))
(out/'example.md').write_text('# Heading\n\n**Bold** text and a list:\n\n- first\n- second\n',encoding='utf-8')
(out/'sample.fodt').write_bytes(source.read_bytes())
formats=[('doc','MS Word 97'),('docx','Office Open XML Text'),('docm','MS Word 2007 XML VBA'),('rtf','Rich Text Format'),('odt','writer8'),('ott','writer8_template'),('pdf','writer_pdf_Export')]
with tempfile.TemporaryDirectory(prefix='kdeglance-fixtures-') as profile:
 env=os.environ.copy();env['SAL_USE_VCLPLUGIN']='svp';env.pop('LD_PRELOAD',None)
 for extension,filter_name in formats:
  path=out/('sample.'+extension)
  if path.exists():path.unlink()
  command=['/usr/bin/libreoffice','-env:UserInstallation='+pathlib.Path(profile).as_uri(),'--headless','--norestore','--convert-to',extension+':'+filter_name,'--outdir',str(out),str(out/'sample.fodt')]
  result=subprocess.run(command,env=env,text=True,capture_output=True,timeout=60)
  print(extension,result.returncode,result.stdout.strip(),result.stderr.strip(),flush=True)
  path=out/('sample.'+extension)
  if result.returncode or not path.exists() or not path.stat().st_size:raise RuntimeError('Could not generate '+extension)
