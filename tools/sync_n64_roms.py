#!/usr/bin/env python3
"""Copy supplied N64 cartridges to a mounted Tab5 SD, with full SHA readback.

No downloads, source changes, destination overwrites, formatting or flashing.
The emulator must have handed exclusive SD ownership to USB mass storage.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import library
import library_index


def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()


def inventory(source):
    source=Path(source)
    if not source.is_dir():raise ValueError('Pasta de ROMs inexistente')
    games={};ignored=[]
    for path in sorted(source.iterdir()):
        if not path.is_file() or path.name.startswith('.'):
            ignored.append(path.name);continue
        name=re.sub(r'(\.(?:z64|n64|v64)) \d+$',r'\1',path.name,flags=re.I)
        if Path(name).suffix.lower() not in ('.z64','.n64','.v64'):
            ignored.append(path.name);continue
        size=path.stat().st_size
        with path.open('rb') as f:header=f.read(4)
        if size<4096 or size>64*1024*1024 or size%4 or header not in (bytes.fromhex('80371240'),bytes.fromhex('37804012'),bytes.fromhex('40123780')):
            raise ValueError('ROM N64 invalida: '+path.name)
        sha=digest(path)
        if sha in games:
            games[sha]['aliases'].append(path.name);continue
        if any(ord(c)<32 or c in '\\:' or ord(c)==127 for c in name):raise ValueError('Nome de ROM invalido')
        games[sha]={'source':str(path.resolve()),'file':name,'bytes':size,'sha256':sha,'aliases':[path.name]}
    if not games:raise ValueError('Nenhuma ROM N64 valida')
    return list(games.values()),ignored


def plan(source,root):
    games,ignored=inventory(source);root=Path(root).absolute()
    if root.is_symlink() or not root.is_dir():raise ValueError('Raiz Tab5-Emu invalida')
    root=root.resolve();names={}
    for game in games:
        target=root/'roms/n64'/game['file']
        for p in (target,*target.parents):
            if p.is_symlink():raise ValueError('Link simbolico no destino')
            if p==root:break
        if len(target.relative_to(root).as_posix().encode())>240:raise ValueError('Caminho acima do limite do firmware')
        collision=target.exists() and (not target.is_file() or digest(target)!=game['sha256'])
        if collision or target.name.casefold() in names:
            target=target.with_name(target.stem+'-'+game['sha256'][:12]+target.suffix)
        if target.is_symlink() or target.exists() and (not target.is_file() or digest(target)!=game['sha256']):
            raise FileExistsError('Destino ocupado: '+str(target))
        if target.name.casefold() in names and names[target.name.casefold()]!=game['sha256']:
            raise FileExistsError('Colisao de nomes FAT')
        names[target.name.casefold()]=game['sha256']
        game.update(destination=str(target),relative=target.relative_to(root).as_posix(),action='verified_existing' if target.exists() else 'copy')
    return {'format':'tab5-n64-transfer-v1','complete':False,'applied':False,'root':str(root),
            'unique_games':len(games),'source_aliases':sum(len(g['aliases']) for g in games),
            'bytes':sum(g['bytes'] for g in games),'ignored':ignored,'games':games}


def refresh_index(root,report):
    # Keep exact prior generations both in local evidence and on the SD.
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    root=Path(root);paths=[root/'library/games.json',root/'cache/library-v1.t5i']
    backups=[]
    for path in paths:
        if path.is_symlink():raise ValueError('Link em catalogo/indice')
        if path.exists():
            backup=Path(str(path)+'.before-n64-'+stamp+'.bak')
            with path.open('rb') as src,backup.open('xb') as out:
                shutil.copyfileobj(src,out);out.flush();os.fsync(out.fileno())
            if digest(backup)!=digest(path):raise OSError('Backup de catalogo/indice divergente')
            backups.append((path,backup))
        else:backups.append((path,None))
    catalog=library.read_catalog(paths[0]) if paths[0].exists() else {'schema_version':1,'games':[]}
    if catalog.get('schema_version')!=1 or not isinstance(catalog.get('games'),list):raise ValueError('Catalogo existente invalido; preservado')
    existing={g.get('rom','').casefold() for g in catalog['games'] if isinstance(g,dict)}
    ids={g.get('id') for g in catalog['games'] if isinstance(g,dict)}
    for game in report['games']:
        if game['relative'].casefold() in existing:continue
        identity='n64-'+game['sha256']
        if identity in ids:identity='n64-'+hashlib.sha256(game['relative'].encode()).hexdigest()
        catalog['games'].append({'id':identity,'system':'n64','title':Path(game['destination']).stem,
                                 'rom':game['relative'],'sha256':game['sha256']})
        ids.add(identity);existing.add(game['relative'].casefold())
    paths[0].parent.mkdir(parents=True,exist_ok=True)
    temp=Path(str(paths[0])+'.n64-new-'+stamp)
    try:
        with temp.open('xb') as f:
            f.write((json.dumps(catalog,indent=2,ensure_ascii=False)+'\n').encode());f.flush();os.fsync(f.fileno())
        os.replace(temp,paths[0])
        index=library_index.build(root)
        report['index']={'games':index['games'],'bytes':index['bytes'],'invalid_entries':index['invalid_entries']}
        report['catalog_backups']=[str(backup) for _,backup in backups if backup]
    except BaseException:
        for path,backup in backups:
            if backup is not None:shutil.copyfile(backup,path)
            elif path.exists():path.unlink()
        raise
    finally:
        temp.unlink(missing_ok=True)


def transfer(report,apply=False,update_index=True,progress=None):
    if not apply:return report
    root=Path(report['root']);needed=sum(g['bytes'] for g in report['games'] if g['action']=='copy')
    if shutil.disk_usage(root).free<needed+16*1024*1024:raise OSError('Espaco livre insuficiente no SD')
    folder=root/'roms/n64';folder.mkdir(parents=True,exist_ok=True)
    lock=folder/'.tab5-n64-transfer.lock'
    with lock.open('xb') as f:f.write(b'Exclusive SD copy in progress\n')
    try:
        for number,game in enumerate(report['games'],1):
            source=Path(game['source']);destination=Path(game['destination'])
            if game['action']=='copy':
                created=False
                try:
                    with source.open('rb') as src,destination.open('xb') as out:
                        created=True;h=hashlib.sha256();copied=0
                        while data:=src.read(1024*1024):out.write(data);h.update(data);copied+=len(data)
                        out.flush();os.fsync(out.fileno())
                    if copied!=game['bytes'] or h.hexdigest()!=game['sha256']:raise OSError('Origem mudou durante a copia')
                except BaseException:
                    if created:destination.unlink(missing_ok=True)
                    raise
            if destination.stat().st_size!=game['bytes'] or digest(destination)!=game['sha256']:
                raise OSError('Releitura SHA do destino falhou: '+destination.name)
            game['readback_verified']=True
            if progress:progress(number,len(report['games']),destination.name)
        if update_index:refresh_index(root,report)
        report['applied']=report['complete']=True
        return report
    finally:
        lock.unlink()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=Path('ROMs/Nintendo 64'))
    p.add_argument('--root',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--apply',action='store_true');p.add_argument('--no-index',action='store_true');args=p.parse_args()
    if args.report.exists():raise FileExistsError(args.report)
    report={'complete':False}
    try:
        report=plan(args.source,args.root)
        transfer(report,args.apply,not args.no_index,lambda n,total,name:print(f'{n}/{total} SHA conferido: {name}',flush=True))
    except Exception as error:report['error']=repr(error);raise
    finally:args.report.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
    print(json.dumps({k:report[k] for k in ('complete','applied','unique_games','source_aliases','bytes')},indent=2))


if __name__=='__main__':main()
