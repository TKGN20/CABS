import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.request


def download(url,path):
    if path.exists():raise FileExistsError(path)
    request=urllib.request.Request(url,headers={'User-Agent':'CABS-reproducibility'})
    with urllib.request.urlopen(request,timeout=120) as source,path.open('xb') as out:
        for block in iter(lambda:source.read(1024*1024),b''):out.write(block)


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--nightly-date');p.add_argument('--resolve-only',action='store_true');a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    if a.resolve_only:
        manifest=a.out/'channel-rust-nightly.toml';download('https://static.rust-lang.org/dist/channel-rust-nightly.toml',manifest)
        match=re.search(r'^date = "([0-9-]+)"',manifest.read_text(),re.M)
        if not match:raise ValueError('Official nightly manifest has no date')
        proof=dict(nightly_date=match.group(1),manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),scope='Resolved available nightly only; not installed or benchmarked')
    else:
        if not a.nightly_date or not re.fullmatch('[0-9]{4}-[0-9]{2}-[0-9]{2}',a.nightly_date):raise ValueError('Pin --nightly-date explicitly')
        executable=a.out/'rustup-init';checksum=a.out/'rustup-init.sha256'
        base='https://static.rust-lang.org/rustup/dist/x86_64-unknown-linux-gnu/rustup-init'
        download(base,executable);download(base+'.sha256',checksum)
        actual=hashlib.sha256(executable.read_bytes()).hexdigest()
        if actual!=checksum.read_text().split()[0]:raise ValueError('Rustup installer checksum mismatch')
        executable.chmod(0o755)
        env=dict(os.environ,RUSTUP_HOME=str((a.out/'rustup').resolve()),CARGO_HOME=str((a.out/'cargo').resolve()))
        command=[str(executable.resolve()),'-y','--no-modify-path','--profile','minimal','--default-toolchain','nightly-'+a.nightly_date]
        with (a.out/'bootstrap.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=log,check=True)
        rustc=a.out/'cargo/bin/rustc'
        proof=dict(nightly_date=a.nightly_date,installer_sha256=actual,command=command,environment={k:env[k] for k in ('RUSTUP_HOME','CARGO_HOME')},rustc=subprocess.check_output([str(rustc.absolute()),'--version'],env=env,text=True).strip(),scope='Isolated project-local toolchain; no shell profile or global toolchain changed')
    (a.out/'toolchain_receipt.json').write_text(json.dumps(proof,indent=2),encoding='utf-8');print(json.dumps(proof,indent=2))


if __name__=='__main__':main()
