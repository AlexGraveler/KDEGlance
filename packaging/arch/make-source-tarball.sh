#!/bin/bash
# Export an allow-listed source tree; never include local desktop reports/configs.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
python3 - "$ROOT" "$HERE" <<'PY'
import gzip,hashlib,io,os,pathlib,re,sys,tarfile
root=pathlib.Path(sys.argv[1]);here=pathlib.Path(sys.argv[2])
version=re.search(r'project\(kdeglance VERSION ([0-9.]+)',(root/'CMakeLists.txt').read_text()).group(1)
name='kdeglance-'+version
files=set()
for directory in ('src','data','tests'):
 for p in (root/directory).rglob('*'):
  if '__pycache__' in p.parts or p.suffix in ('.pyc','.moc'):continue
  if p.parent==root/'tests/desktop' and p.name not in {'sessionadapter.cpp','check-photo-reset.py','check-default-open.py','check-system-open.py','run-desktop-checks.sh','MANUAL-CHECKLIST.md'}:continue
  if p.is_symlink():raise SystemExit('Refusing source symlink: '+str(p.relative_to(root)))
  if p.is_file():files.add(p.relative_to(root))
for file in ('CMakeLists.txt','config-kdeglance.h.in','COPYING','LICENSE','README.md','CHANGELOG.md',
 'RELEASE-NOTES.md','CONTRIBUTING.md','VERSION','.gitignore',
 'docs/ARCHITECTURE.md','docs/FORMATS.md','docs/TESTING.md','docs/INSTALL.md',
 'docs/RELEASE-VALIDATION.md','docs/MANUAL-ACCEPTANCE.md',
 'packaging/arch/PKGBUILD','packaging/arch/kdeglance.install',
 'packaging/arch/make-source-tarball.sh','packaging/arch/test-in-clean-chroot.sh',
 'packaging/arch/test-package-lifecycle.sh',
 'packaging/verify-release.py'):
 p=root/file
 if not p.is_file():raise SystemExit('Missing release source: '+file)
 files.add(pathlib.Path(file))
for p in (root/'docs/screenshots').glob('*.png'):files.add(p.relative_to(root))
archive=here/(name+'.tar.gz')
with archive.open('wb') as raw,gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as zipped,tarfile.open(fileobj=zipped,mode='w') as tar:
 for relative in sorted(files):
  p=root/relative;data=p.read_bytes()
  info=tarfile.TarInfo(name+'/'+relative.as_posix());info.size=len(data)
  info.uid=info.gid=0;info.uname=info.gname='root'
  info.mode=0o755 if p.stat().st_mode&0o111 else 0o644
  info.mtime=int(os.environ.get('SOURCE_DATE_EPOCH','0'))
  tar.addfile(info,io.BytesIO(data))
checksum=hashlib.sha256(archive.read_bytes()).hexdigest()
pkgbuild=here/'PKGBUILD';text=pkgbuild.read_text()
text=re.sub(r'^pkgver=.*$', 'pkgver='+version,text,flags=re.M)
text=re.sub(r"^sha256sums=.*$", "sha256sums=('"+checksum+"')",text,flags=re.M)
pkgbuild.write_text(text)
print(archive);print('sha256: '+checksum);print(str(len(files))+' allow-listed source files')
PY
