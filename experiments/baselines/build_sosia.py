import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

HERE=Path(__file__).resolve().parent


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--upstreams',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    pin=json.loads((HERE/'sources.json').read_text())['methods']['SOSIA'];name='SOSIA-'+pin['commit']
    source=a.upstreams/name;receipt=json.loads((a.upstreams/(name+'.receipt.json')).read_text())
    if receipt['commit']!=pin['commit'] or receipt['repository']!=pin['repository']:raise ValueError('Wrong SOSIA source receipt')
    for path in (source/'src').iterdir():
        if path.is_file() and digest(path)!=receipt['source_sha256'][path.relative_to(source).as_posix()]:raise ValueError('Upstream source changed')
    a.out.mkdir(parents=True,exist_ok=False);shutil.copytree(source/'src',a.out/'source/src')
    for path in (a.out/'source/src').iterdir():
        if path.is_file():
            content=path.read_bytes().replace(b'\r\n',b'\n')
            path.write_bytes(content if content.endswith(b'\n') else content+b'\n')
    patch=HERE/'sosia_runtime.patch'
    with (a.out/'build.log').open('w') as log:
        subprocess.run(['patch','--batch','-p1','-d',str(a.out/'source'),'-i',str(patch)],stdout=log,stderr=log,check=True)
        command=['g++','-std=c++17','-O1' if a.sanitize else '-O3','-mavx','-ffp-contract=off','-fopenmp','-I'+str(a.out/'source/src'),str(HERE/'sosia_fixed.cpp'),str(a.out/'source/src/Preprocess.cpp'),str(a.out/'source/src/basis.cpp'),str(a.out/'source/src/distances_simd_avx512.cpp'),'-o',str(a.out/'sosia_fixed')]
        if a.sanitize:command.extend(['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])
        subprocess.run(command,stdout=log,stderr=log,check=True)
    proof=dict(status='BUILT_NOT_PARITY_ACCEPTED',upstream=pin,archive_sha256=receipt['archive_sha256'],patch_sha256=digest(patch),adapter_sha256=digest(HERE/'sosia_fixed.cpp'),binary_sha256=digest(a.out/'sosia_fixed'),command=command,compiler=subprocess.check_output(['g++','--version'],text=True),source_sha256={f.relative_to(a.out/'source').as_posix():digest(f) for f in sorted((a.out/'source/src').iterdir()) if f.is_file()})
    (a.out/'build_receipt.json').write_text(json.dumps(proof,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in proof.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':main()
