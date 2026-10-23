import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE=Path(__file__).resolve().parent


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--upstreams',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--configure-only',action='store_true');a=p.parse_args()
    registry=json.loads((HERE/'sources.json').read_text());pin=registry['methods']['SINDI'];dep=registry['dependencies']['ThreadPool']
    name='SINDI-'+pin['commit'];source=a.upstreams/name;receipt=json.loads((a.upstreams/(name+'.receipt.json')).read_text())
    if receipt['commit']!=pin['commit'] or receipt['repository']!=pin['repository']:raise ValueError('Wrong SINDI source receipt')
    for file,value in receipt['source_sha256'].items():
        if digest(source/file)!=value:raise ValueError('Upstream source changed: '+file)
    archive=a.upstreams/('ThreadPool-'+dep['commit']+'.tar.gz')
    if digest(archive)!=dep['archive_sha256']:raise ValueError('ThreadPool pinned checksum mismatch')
    a.out.mkdir(parents=True,exist_ok=False);target=a.out/'source';target.mkdir();patch=[]
    for name in receipt['source_sha256']:
        destination=target/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/name,destination)
    for path in sorted(target.rglob('*')):
        if not path.is_file() or (path.suffix!='.cmake' and path.name!='CMakeLists.txt'):continue
        original=path.read_text();modified=original.replace('-Ofast','-O3')
        if path.relative_to(target).as_posix()=='extern/thread_pool/thread_pool.cmake':
            modified='include(FetchContent)\nFetchContent_Declare(thread_pool URL "'+str(archive.resolve())+'" URL_HASH SHA256='+dep['archive_sha256']+')\nFetchContent_MakeAvailable(thread_pool)\ninclude_directories(${thread_pool_SOURCE_DIR}/)\n'
        if modified!=original:
            patch.extend(difflib.unified_diff(original.splitlines(True),modified.splitlines(True),fromfile='a/'+path.relative_to(target).as_posix(),tofile='b/'+path.relative_to(target).as_posix()))
            path.write_text(modified)
    (a.out/'adaptation.patch').write_text(''.join(patch))
    command=['cmake','-S',str(target.resolve()),'-B',str((a.out/'build').resolve()),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_INSTALL_PREFIX='+str((a.out/'install').resolve()),'-DENABLE_INTEL_MKL=OFF','-DENABLE_TOOLS=OFF','-DENABLE_EXAMPLES=OFF','-DENABLE_TESTS=OFF','-DENABLE_WERROR=OFF','-DNUM_BUILDING_JOBS=2','-DCMAKE_CXX_FLAGS=-ffp-contract=off']
    env=dict(os.environ,GIT_CEILING_DIRECTORIES=str(a.out.resolve()),GIT_TERMINAL_PROMPT='0')
    state=dict(status='running',upstream=pin,thread_pool=dep,command=command,patch_sha256=digest(a.out/'adaptation.patch'))
    (a.out/'build_receipt.json').write_text(json.dumps(state,indent=2))
    try:
        with (a.out/'configure.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=log,check=True)
        if not a.configure_only:
            with (a.out/'build.log').open('w') as log:subprocess.run(['cmake','--build',str(a.out/'build'),'--parallel','2','--target','sindi_index_build','sindi_index_search'],env=env,stdout=log,stderr=log,check=True)
        state.update(status='CONFIGURED_NOT_BUILT' if a.configure_only else 'BUILT_NOT_PARITY_ACCEPTED',compile_commands_sha256=digest(a.out/'build/compile_commands.json'))
        if not a.configure_only:state.update(binary_sha256={name:digest(a.out/'build/sparse/scripts'/name) for name in ('sindi_index_build','sindi_index_search')},library_sha256=digest(a.out/'build/src/libvsag.so'))
    except BaseException as error:state.update(status='failed',error=str(error));raise
    finally:(a.out/'build_receipt.json').write_text(json.dumps(state,indent=2))
    print(json.dumps(state,indent=2))


if __name__=='__main__':main()
