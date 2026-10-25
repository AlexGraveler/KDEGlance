#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build only the allowlisted user distribution, including corresponding source."""
import gzip
import hashlib
import io
from pathlib import Path
import tarfile
from distribution import ROOT,archive_files
VERSION=(ROOT/'VERSION').read_text().strip()
assert VERSION=='1.0.1'
files=archive_files()
archive=ROOT/('KDEGlance-'+VERSION+'.tar.gz')
with archive.open('wb') as destination:
    with gzip.GzipFile(filename='',mode='wb',fileobj=destination,mtime=0,compresslevel=9) as compressed:
        with tarfile.open(fileobj=compressed,mode='w|',format=tarfile.PAX_FORMAT) as output:
            for name,path in files.items():
                data=path.read_bytes();info=tarfile.TarInfo('KDEGlance-'+VERSION+'/'+name)
                info.size=len(data);info.mtime=1792800000;info.uid=info.gid=0;info.uname=info.gname='root'
                info.mode=0o755 if name in ('install.sh','uninstall.sh') else 0o644
                output.addfile(info,io.BytesIO(data))
checksum=hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_name(archive.name+'.sha256').write_text(checksum+'  '+archive.name+'\n')
print(archive.name+' '+checksum+' ('+str(len(files))+' required source/license files, '+str(archive.stat().st_size)+' bytes)')
