#!/usr/bin/env python3
"""Generate a small, uncompressed synthetic Bayer DNG for LibRaw checks."""
import struct
import sys

width, height = 128, 96
entries = []
def tag(number, kind, values):
    if kind == 2:
        data = values.encode() + b'\0'; count = len(data)
    elif kind in (1, 7):
        data = bytes(values); count = len(values)
    else:
        formats = {3: 'H', 4: 'I', 5: 'I', 10: 'i'}
        data = struct.pack('<' + formats[kind] * len(values), *values)
        count = len(values) // (2 if kind in (5, 10) else 1)
    entries.append((number, kind, count, data))
tag(254,4,[0]); tag(256,4,[width]); tag(257,4,[height])
tag(258,3,[16]); tag(259,3,[1]); tag(262,3,[32803])
tag(271,2,'KDEGlance'); tag(272,2,'Synthetic Bayer test')
tag(273,4,[0]); tag(274,3,[1]); tag(277,3,[1]); tag(278,4,[height]); tag(279,4,[width*height*2])
tag(284,3,[1]); tag(33421,3,[2,2]); tag(33422,1,[0,1,1,2])
tag(50706,1,[1,4,0,0]); tag(50707,1,[1,1,0,0]); tag(50708,2,'KDEGlance test DNG')
tag(50710,1,[0,1,2]); tag(50711,3,[1]); tag(50714,3,[0]); tag(50717,4,[65535])
tag(50718,5,[1,1,1,1]); tag(50719,4,[0,0]); tag(50720,4,[width,height])
tag(50721,10,[1,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,1,1]); tag(50728,5,[1,1,1,1,1,1]); tag(50778,3,[21])
entries.sort()
offset=8+2+12*len(entries)+4
extra=bytearray(); records=[]
for number,kind,count,data in entries:
    if len(data)>4:
        value=struct.pack('<I',offset+len(extra));extra+=data
        if len(extra)%2:extra+=b'\0'
    else:value=data.ljust(4,b'\0')
    records.append((number,struct.pack('<HHI',number,kind,count)+value))
pixels_offset=offset+len(extra)
records=[struct.pack('<HHII',273,4,1,pixels_offset) if n==273 else record for n,record in records]
pixels=struct.pack('<'+'H'*(width*height),*[4000+int(45000*x/(width-1)) for y in range(height) for x in range(width)])
with open(sys.argv[1],'wb') as f:
    f.write(b'II*\0'+struct.pack('<I',8)+struct.pack('<H',len(entries))+b''.join(records)+b'\0'*4+extra+pixels)
