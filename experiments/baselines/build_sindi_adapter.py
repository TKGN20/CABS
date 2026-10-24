import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

HERE=Path(__file__).resolve().parent


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--native',type=Path,required=True);p.add_argument('--upstreams',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    native=a.native.resolve();pin=json.loads((HERE/'sources.json').read_text())['methods']['SINDI'];receipt=json.loads((native/'build_receipt.json').read_text())
    if receipt['upstream']!=pin or receipt['status']!='BUILT_NOT_PARITY_ACCEPTED':raise ValueError('Wrong or incomplete native build')
    upstream=json.loads((a.upstreams/('SINDI-'+pin['commit']+'.receipt.json')).read_text())
    repairs=receipt.get('correctness_repairs')
    if repairs and (repairs!=json.loads((HERE/'sindi_correctness.json').read_text()) or digest(HERE/'sindi_correctness.patch')!=repairs['patch_sha256']):raise ValueError('Unregistered native correctness repairs')
    for name,wanted in upstream['source_sha256'].items():
        if repairs and name in repairs['files']:wanted=repairs['files'][name]['after_sha256']
        if Path(name).suffix in ('.h','.hpp','.cpp','.inl') and digest(native/'source'/name)!=wanted:raise ValueError('Native algorithm source differs from pin: '+name)
    if digest(native/'adaptation.patch')!=receipt['patch_sha256'] or digest(native/'build/compile_commands.json')!=receipt['compile_commands_sha256']:raise ValueError('Native build recipe changed')
    a.out.mkdir(parents=True,exist_ok=False);binary=(a.out/'sindi_fixed').resolve();obj=(a.out/'sindi_fixed.o').resolve()
    driver=(a.out/'sindi_fixed.cpp').resolve();shutil.copyfile(HERE/'sindi_fixed.cpp',driver)
    commands=json.loads((native/'build/compile_commands.json').read_text());entry=next(e for e in commands if e['file'].endswith('/sparse/scripts/sindi_index_build.cpp'))
    compile_command=shlex.split(entry['command']);compile_command[compile_command.index('-o')+1]=str(obj);compile_command[compile_command.index('-c')+1]=str(driver);compile_command+=['-std=c++17']
    if repairs:compile_command+=['-DCABS_SINDI_REPAIRED_NATIVE=1']
    link_command=shlex.split((native/'build/sparse/scripts/CMakeFiles/sindi_index_build.dir/link.txt').read_text())
    link_command=[str(obj) if v.endswith('CMakeFiles/sindi_index_build.dir/sindi_index_build.cpp.o') else v for v in link_command];link_command[link_command.index('-o')+1]=str(binary)
    library=native/'build/src/libvsag.so';library_hash=digest(library)
    state=dict(status='running',upstream=pin,native_build_receipt_sha256=digest(native/'build_receipt.json'),native_library_sha256=library_hash,adapter_sha256=digest(HERE/'sindi_fixed.cpp'),compile_command=compile_command,link_command=link_command,scope='Public API gamma-candidate adapter; supplied native library is unchanged during adapter build; native repairs separately disclosed; IS/common protocol acceptance pending')
    state['native_correctness_repairs']=repairs
    try:
        with (a.out/'build.log').open('w') as log:
            subprocess.run(compile_command,cwd=entry['directory'],stdout=log,stderr=log,check=True)
            subprocess.run(link_command,cwd=native/'build/sparse/scripts',stdout=log,stderr=log,check=True)
        if digest(library)!=library_hash:raise ValueError('Native library changed during adapter build')
        state.update(status='BUILT_NOT_PARITY_ACCEPTED',binary_sha256=digest(binary))
    except BaseException as error:state.update(status='failed',error=str(error));raise
    finally:(a.out/'build_receipt.json').write_text(json.dumps(state,indent=2))
    print(json.dumps(state,indent=2))


if __name__=='__main__':main()
