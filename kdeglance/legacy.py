# SPDX-License-Identifier: GPL-3.0-or-later
"""Conservative recognition and backup of the retired LogSelection experiment."""
import configparser
import hashlib
import json
import os
from pathlib import Path
import shlex
import stat
import time

MENU='kdeglance-space-probe.desktop'

def is_experimental_menu(data,data_home):
    if len(data)>65536:return False
    parser=configparser.ConfigParser(interpolation=None,strict=True)
    parser.optionxform=str
    try:
        parser.read_string(data.decode('utf-8'))
        if parser.defaults() or set(parser.sections())!={'Desktop Entry','Desktop Action LogSelection'}:return False
        entry=parser['Desktop Entry'];action=parser['Desktop Action LogSelection']
        allowed={'Type','Name','Icon','MimeType','Actions','X-KDE-Protocols','X-KDE-Protocol',
                 'X-KDE-ServiceTypes','ServiceTypes','X-KDE-Submenu','X-KDE-Priority','X-KDE-RequiredNumberOfUrls'}
        if any(key not in allowed and not (key.startswith('Name[') and key.endswith(']')) for key in entry):return False
        if any(key not in {'Name','Icon','Exec'} and not (key.startswith('Name[') and key.endswith(']')) for key in action):return False
        if entry.get('Type')!='Service' or entry.get('Actions') not in ('LogSelection;','LogSelection'):return False
        command=shlex.split(action.get('Exec',''))
        if not command or command.pop(0) not in ('python3','/usr/bin/python3'):return False
        if command and command[0]=='-B':command.pop(0)
        if not command or command.pop(0)!=str(Path(data_home)/'kdeglance-space-probe/log_selection.py'):return False
        return command in ([],['%U'],['%F'],['%u'],['%f'],['--','%U'],['--','%F'])
    except (ValueError,UnicodeError,configparser.Error):return False

def read_owned_menu(path):
    path=Path(path)
    if path.is_symlink() or path.parent.is_symlink():raise RuntimeError('Refusing a symlinked legacy menu')
    with os.fdopen(os.open(path,os.O_RDONLY|os.O_NOFOLLOW),'rb') as stream:
        info=os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_size>65536:
            raise RuntimeError('Legacy menu is not a small regular file owned by this user')
        data=stream.read(65537)
    return data,info

def save_backup(data,data_home,state_home,source):
    if not is_experimental_menu(data,data_home):raise RuntimeError('Unrecognized legacy menu; no changes made')
    directory=Path(state_home)/'kdeglance-migration'
    if directory.is_symlink():raise RuntimeError('Refusing symlinked migration backup directory')
    directory.mkdir(parents=True,exist_ok=True,mode=0o700)
    checksum=hashlib.sha256(data).hexdigest()
    backup=directory/(str(time.time_ns())+'-'+checksum[:12]+'.desktop')
    descriptor=os.open(backup,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(descriptor,'wb') as stream:
        stream.write(data);stream.flush();os.fsync(stream.fileno())
    record=backup.with_suffix('.json')
    descriptor=os.open(record,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(descriptor,'w') as stream:
        json.dump({'source':str(source),'sha256':checksum,'backup':backup.name},stream,indent=2)
        stream.flush();os.fsync(stream.fileno())
    return backup

def archive_experimental_menu(data_home,state_home):
    target=Path(data_home)/'kio/servicemenus'/MENU
    if not target.exists() and not target.is_symlink():return None
    data,info=read_owned_menu(target)
    backup=save_backup(data,data_home,state_home,target)
    latest,now=read_owned_menu(target)
    if latest!=data or (now.st_ino,now.st_dev,now.st_mtime_ns)!=(info.st_ino,info.st_dev,info.st_mtime_ns):
        raise RuntimeError('Legacy menu changed during backup; original retained')
    target.unlink()
    return backup
