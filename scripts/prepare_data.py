#!/usr/bin/env python3

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(8*1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()

def inspect(path):
    path = Path(path)
    with path.open('rb') as f:
        header = f.read(24)
    if len(header) != 24:
        raise ValueError(f'Truncated CSR: {path}')
    n,d,nnz = struct.unpack('<QQQ', header)
    size = path.stat().st_size
    if n < 1 or d < 1 or d > 2147483647 or size != 24+8*(n+1)+8*nnz:
        raise ValueError(f'Invalid CSR header/size: {path}')
    return dict(rows=n, dimensions=d, nnz=nnz, bytes=size, sha256=sha256(path))

def verify(path, expected):
    actual = inspect(path)
    for field in ['rows','dimensions','nnz','bytes','sha256']:
        if expected.get(field) is not None and expected[field] != actual[field]:
            raise ValueError(f'{path}: {field} mismatch: {actual[field]} != {expected[field]}')
    return actual

def prepare(scale, directory, offline=False):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    specs = json.loads((ROOT/'configs/datasets.json').read_text())['files']
    names = ['base_1M.csr' if scale == '1M' else 'base_small.csr', 'queries.dev.csr']
    evidence = {}
    for name in names:
        spec = specs[name]
        path = directory/name
        if not path.exists():
            if offline:
                raise FileNotFoundError(f'Missing {path}; download with scripts/prepare_data.py first')
            print(f'DOWNLOAD {spec["url"]}', flush=True)
            temporary = directory/(name+'.part')
            request = urllib.request.Request(spec['url'], headers={'User-Agent':'CABS-reproducibility'})

            with urllib.request.urlopen(request, timeout=120) as response:
                with gzip.GzipFile(fileobj=response) as stream, temporary.open('wb') as output:
                    shutil.copyfileobj(stream, output, 1024*1024)
            verify(temporary, spec)
            temporary.replace(path)
        actual = verify(path, spec)
        evidence[name] = dict(**actual, url=spec['url'])
        print(f'VERIFIED {name} {actual["sha256"]}', flush=True)
    (directory/f'data_manifest_{scale}.json').write_text(json.dumps(evidence,indent=2)+'\n')
    return directory/names[0], directory/names[1]

def main():
    p = argparse.ArgumentParser(description='Download and verify public SPLADE CSR files.')
    p.add_argument('--scale',choices=['0.1M','1M'],default='1M')
    p.add_argument('--directory',type=Path,default=ROOT/'datasets/SPLADE')
    p.add_argument('--offline',action='store_true',help='Verify supplied CSR files without downloading')
    args = p.parse_args()
    prepare(args.scale,args.directory,args.offline)

if __name__ == '__main__':
    main()
