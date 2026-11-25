#!/usr/bin/env python3
"""Build the bounded-memory firmware index on a computer; never modify ROMs."""
import argparse,json,struct,zlib,os,tempfile
from pathlib import Path
import library
MAX_GAMES=100000
MAX_INDEX_BYTES=64*1024*1024

def unique_pairs(items):
    result={}
    for key,value in items:
        if key in result:raise ValueError('duplicate JSON field: '+key)
        result[key]=value
    return result

def build(root,check_files=True):
    root=Path(root).resolve();source=root/'library/games.json'
    if source.exists():
        if source.stat().st_size>64*1024*1024:raise ValueError('catalog exceeds 64 MiB')
        raw=source.read_bytes();catalog=json.loads(raw.decode('utf-8'),object_pairs_hook=unique_pairs)
        valid,errors=library.validate(root,catalog,check_files=check_files,max_games=MAX_GAMES)
        source_size=source.stat().st_size;source_time=zlib.crc32(raw)
    else:valid,errors,source_size,source_time=[],[],0,0
    paths={item['rom'].casefold() for item in valid};ids={item['id'] for item in valid}
    for system,extensions in library.EXTENSIONS.items():
        folder=root/'roms'/system
        if not folder.is_dir():continue
        for file in sorted(folder.rglob('*')):
            if any(part.startswith('.') for part in file.relative_to(folder).parts):continue
            if not file.is_file() or file.suffix.lower() not in extensions:continue
            rel=file.relative_to(root).as_posix()
            if rel.casefold() in paths:continue
            library.resolve(root,rel)
            identity='scan-'+library.hashlib.sha256(rel.encode()).hexdigest()
            if identity in ids:continue
            valid.append(dict(id=identity,system=system,title=file.stem[:100],rom=rel))
            paths.add(rel.casefold());ids.add(identity)
            if len(valid)>MAX_GAMES:raise ValueError('more than 100000 games')
    valid.sort(key=lambda item:(item['system'],item['title'].casefold(),item['rom']))
    target=root/'cache/library-v1.t5i';target.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix='library-',suffix='.tmp',dir=target.parent)
    try:
        with os.fdopen(fd,'w+b') as stream:
            stream.write(bytes(64));crc=0
            for game in valid:
                strings=[str(game.get(key,'')).encode('utf-8') for key in ('system','title','rom','cover','description','id')]
                limits=(16,160,240,240,2048,80)
                if any(len(s)>n or b'\0' in s for s,n in zip(strings,limits)):raise ValueError('invalid indexed record')
                record=struct.pack('<6H',*(len(s) for s in strings))+b''.join(strings)
                stream.write(record);crc=zlib.crc32(record,crc)
                if stream.tell()>MAX_INDEX_BYTES:raise ValueError('index exceeds firmware 64 MiB limit')
            total=stream.tell();stream.seek(0)
            stream.write(struct.pack('<8sIIIIQQ24x',b'T5LIBI01',1,len(valid),total,crc,source_size,source_time))
            stream.flush();os.fsync(stream.fileno())
        # An invalid new generation never replaces the prior valid file.
        os.replace(tmp,target)
    finally:
        if os.path.exists(tmp):os.unlink(tmp)
    return {'games':len(valid),'bytes':target.stat().st_size,'path':str(target),'invalid_entries':errors}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True)
    p.add_argument('--skip-file-check',action='store_true',help='metadata import/test fixtures only')
    a=p.parse_args();print(json.dumps(build(a.root,not a.skip_file_check),indent=2,ensure_ascii=False))
