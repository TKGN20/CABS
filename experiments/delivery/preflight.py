import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from verify_artifacts import check_approval_baseline


def capture(command):
    p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    return dict(exit_code=p.returncode,output=p.stdout)


def inventory(path):
    result=dict(file=path.name,symlink=path.is_symlink())
    try:
        with path.open('rb') as f:
            header=f.read(24)
            if len(header)!=24:raise ValueError('Truncated CSR header')
            n,d,nnz=struct.unpack('<QQQ',header)
            expected=24+8*(n+1)+8*nnz
            if path.stat().st_size!=expected:raise ValueError('CSR size/header mismatch')
            h=hashlib.sha256(header)
            for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
        result.update(status='PASS',n=n,d=d,nnz=nnz,bytes=expected,sha256=h.hexdigest(),scope='header/size/hash; full row validation is separate')
    except (OSError,ValueError) as error:result.update(status='BLOCKED',error=str(error))
    return result


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    git=['git','-c','safe.directory='+str(ROOT),'-C',str(ROOT)]
    result=dict(timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                host=dict(system=platform.platform(),cpu=capture(['lscpu']) if os.name=='posix' else platform.processor()),
                commit=capture(git+['rev-parse','HEAD']),branch=capture(git+['branch','--show-current']),
                worktree=capture(git+['status','--porcelain']),freeze=check_approval_baseline(source_check=True),
                inputs=[inventory(ROOT/'datasets/SPLADE'/name) for name in ('base_small.csr','base_1M.csr','queries.dev.csr')],
                server=dict(status='BLOCKED',reason='Owner confirmed 2026-10-23: server is not ready; no remote measurement authorized or performed'))
    if os.name=='posix':
        result['resources']={name:capture(command) for name,command in [('memory',['free','-b']),('disk',['df','-B1',str(ROOT)]),('compiler',['c++','--version']),('rust',['rustc','--version']),('cargo',['cargo','--version'])]}
    (a.out/'preflight.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:result[k] for k in ('commit','freeze','inputs','server')},indent=2))


if __name__=='__main__':main()
