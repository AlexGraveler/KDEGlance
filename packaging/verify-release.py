#!/usr/bin/env python3
"""Verify release metadata and inspect an optional release source archive."""
import argparse,json,pathlib,re,tarfile,xml.etree.ElementTree as ET
root=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--archive',type=pathlib.Path);args=parser.parse_args()
version=re.search(r'project\(kdeglance VERSION ([0-9.]+)',(root/'CMakeLists.txt').read_text()).group(1)
assert (root/'VERSION').read_text().strip()==version
recipe=(root/'packaging/arch/PKGBUILD').read_text()
assert re.search(r'^pkgver=(.*)$',recipe,re.M).group(1)==version
assert re.search(r'^pkgrel=(.*)$',recipe,re.M).group(1)=='1'
release=ET.parse(root/'data/org.kde.kdeglance.metainfo.xml').getroot().find('releases/release')
assert release is not None and release.attrib['version']==version
component=ET.parse(root/'data/org.kde.kdeglance.metainfo.xml').getroot()
assert component.findtext('project_license')=='GPL-3.0-or-later'
assert "license=('GPL-3.0-or-later')" in recipe
for doc in ('README.md','RELEASE-NOTES.md','docs/INSTALL.md','docs/TESTING.md','docs/ARCHITECTURE.md','docs/FORMATS.md','docs/MANUAL-ACCEPTANCE.md'):
 for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)',(root/doc).read_text()):
  if '://' in target or target.startswith('#'):continue
  relative=target.split('#',1)[0]
  if relative:assert (root/doc).parent.joinpath(relative).exists(),(doc,target)
for name in ('hook/kdeglance_dolphinhook.json','menu/kdeglance_quicklook.json'):
 metadata=json.loads((root/'src/dolphin'/name).read_text())['KPlugin']
 assert metadata['Version']==version
assert version in (root/'README.md').read_text() and version in (root/'RELEASE-NOTES.md').read_text()
if args.archive:
 with tarfile.open(args.archive) as tar:
  names=tar.getnames()
  for member in tar:
   p=pathlib.PurePosixPath(member.name)
   assert p.parts[0]=='kdeglance-'+version and '..' not in p.parts and not p.is_absolute()
   assert not member.issym() and not member.islnk()
   assert not any(part in {'.git','.development-history','.codex','.aws','.agents','__pycache__','checkpoints','build','pkg'} for part in p.parts)
   assert not p.name.endswith(('.log','.pkg.tar.zst','.pyc','.so','.o'))
   assert 'docs/development/' not in member.name
   assert str(pathlib.Path.home()) not in member.name
   if member.isfile() and p.suffix not in ('.png','.mkv'):
    content=tar.extractfile(member).read()
    assert str(pathlib.Path.home()).encode() not in content,member.name
    assert not re.search(rb'(?:gh[pousr]_[A-Za-z0-9_]{24,}|AKIA[A-Z0-9]{16}|-----BEGIN [A-Z ]*PRIVATE KEY-----)',content),member.name
  for needed in ('CMakeLists.txt','VERSION','src/viewer/main.cpp','tests/fixtures/av1/av1.mkv','packaging/arch/PKGBUILD'):
   assert 'kdeglance-'+version+'/'+needed in names
 print('Archive paths and allow-list hygiene OK:',len(names),'files')
print('Release metadata consistent:',version)
