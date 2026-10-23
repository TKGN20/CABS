import argparse
import hashlib
import json
from pathlib import Path,PurePosixPath
import re
import tarfile
import urllib.request


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--method',choices=['SOSIA','SINDI','SEISMIC'],nargs='+',default=['SOSIA','SINDI','SEISMIC']);a=p.parse_args()
    pins=json.loads(Path(__file__).with_name('sources.json').read_text())['methods']
    a.out.mkdir(parents=True,exist_ok=True)
    for name in a.method:
        pin=pins[name];commit=pin['commit'];repo=pin['repository']
        if not re.fullmatch('[0-9a-f]{40}',commit) or not re.fullmatch('[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',repo):raise ValueError('Invalid source pin')
        target=a.out/(name+'-'+commit)
        if target.exists():raise FileExistsError('Preserve existing checkout; choose a fresh destination: '+str(target))
        archive=a.out/(name+'-'+commit+'.tar.gz')
        if not archive.exists():
            request=urllib.request.Request('https://codeload.github.com/'+repo+'/tar.gz/'+commit,headers={'User-Agent':'CABS-reproducibility'})
            with urllib.request.urlopen(request,timeout=120) as remote,archive.with_suffix('.part').open('xb') as out:
                for block in iter(lambda:remote.read(1024*1024),b''):out.write(block)
            archive.with_suffix('.part').rename(archive)
        target.mkdir();count=0;roots=set()
        with tarfile.open(archive,'r:gz') as tar:
            for member in tar:
                path=PurePosixPath(member.name)
                if path.is_absolute() or '..' in path.parts or not path.parts:raise ValueError('Unsafe archive path')
                roots.add(path.parts[0]);relative=Path(*path.parts[1:])
                if len(roots)!=1:raise ValueError('Multiple archive roots')
                if len(path.parts)==1:continue
                output=target/relative
                if member.isdir():output.mkdir(parents=True,exist_ok=True)
                elif member.isfile():
                    output.parent.mkdir(parents=True,exist_ok=True)
                    with tar.extractfile(member) as source,output.open('xb') as out:
                        for block in iter(lambda:source.read(1024*1024),b''):out.write(block)
                    count+=1
                else:raise ValueError('Unsupported special archive entry: '+member.name)
        receipt=dict(repository=repo,commit=commit,archive_sha256=digest(archive),files=count,
                     source_sha256={f.relative_to(target).as_posix():digest(f) for f in sorted(target.rglob('*')) if f.is_file()},
                     scope='Anonymous pinned upstream archive; not a build, parity or paper-lineage pass')
        (a.out/(name+'-'+commit+'.receipt.json')).write_text(json.dumps(receipt,indent=2),encoding='utf-8')
        print(json.dumps({k:v for k,v in receipt.items() if k!='source_sha256'}),flush=True)


if __name__=='__main__':main()
