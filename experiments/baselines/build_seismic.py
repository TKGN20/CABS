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
    p=argparse.ArgumentParser();p.add_argument('--upstreams',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--adapter',action='store_true');p.add_argument('--space-inventory',action='store_true');a=p.parse_args()
    if a.space_inventory and not a.adapter:raise ValueError('Space introspection requires the registered plain adapter')
    pin=json.loads((HERE/'sources.json').read_text())['methods']['SEISMIC'];name='SEISMIC-'+pin['commit'];source=a.upstreams/name
    receipt=json.loads((a.upstreams/(name+'.receipt.json')).read_text())
    if receipt['commit']!=pin['commit'] or receipt['repository']!=pin['repository']:raise ValueError('Wrong SEISMIC source receipt')
    for name,value in receipt['source_sha256'].items():
        if digest(source/name)!=value:raise ValueError('Upstream source changed: '+name)
    tool=json.loads((a.toolchain/'toolchain_receipt.json').read_text())
    a.out.mkdir(parents=True,exist_ok=False);target=a.out/'source';shutil.copytree(source,target)
    adaptations=[]
    if a.adapter:
        path=target/'src/inverted_index.rs';before=path.read_text()
        begin=before.index('    #[allow(clippy::too_many_arguments)]\n    #[must_use]\n    pub fn search<')
        end=before.index('    /// Convert the `InvertedIndexBase`',begin)
        method=before[begin:end]
        method=method.replace('pub fn search<','pub fn search_with_candidates<',1).replace(') -> Vec<ScoredVectorDotProduct>',') -> (Vec<ScoredVectorDotProduct>, Vec<usize>)',1)
        method=method.replace('        heap.into_sorted_vec()','        let candidates = visited.into_iter().collect();\n        let ranked = heap.into_sorted_vec()',1)
        ending='            .collect()\n    }'
        if method.count(ending)!=1:raise ValueError('Unrecognized native search body')
        method=method.replace(ending,'            .collect();\n        (ranked, candidates)\n    }',1)
        after=before[:end]+method+before[end:];path.write_text(after)
        adaptations.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/src/inverted_index.rs',tofile='b/src/inverted_index.rs'))
        path=target/'src/utils.rs';before=path.read_text();after=before
        for original in ('let seed = 1142;','let seed = 42;','let seed = 42;'):
            replacement='let seed = std::env::var("CABS_BENCH_INDEX_SEED").map(|v| v.parse::<u64>().expect("Invalid benchmark index seed")).unwrap_or('+original[len('let seed = '):-1]+');'
            if original not in after:raise ValueError('Missing native seed site')
            after=after.replace(original,replacement,1)
        path.write_text(after)
        adaptations.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/src/utils.rs',tofile='b/src/utils.rs'))
        shutil.copyfile(HERE/'seismic_fixed.rs',target/'src/bin/cabs_fixed_queries.rs')
        (a.out/'adaptation.patch').write_text(''.join(adaptations))
    space=None
    if a.space_inventory:
        space=json.loads((HERE/'seismic_space.json').read_text())
        if space['upstream_commit']!=pin['commit'] or space['required_adaptation_sha256']!=digest(a.out/'adaptation.patch') or space['patch_sha256']!=digest(HERE/'seismic_space.patch'):raise ValueError('Unregistered SEISMIC space patch/adaptation')
        for name,hashes in space['files'].items():
            if digest(target/name)!=hashes['before_sha256']:raise ValueError('Space patch preimage changed: '+name)
        subprocess.run(['patch','-p1','--batch','--forward','-i',str(HERE/'seismic_space.patch')],cwd=target,check=True)
        for name,hashes in space['files'].items():
            if digest(target/name)!=hashes['after_sha256']:raise ValueError('Space patch postimage changed: '+name)
    env=dict(os.environ,**tool['environment']);env['PATH']=str((a.toolchain/'cargo/bin').resolve())+os.pathsep+env.get('PATH','')
    env.update(RUSTUP_TOOLCHAIN='nightly-'+tool['nightly_date'],CARGO_TARGET_DIR=str((a.out/'target').resolve()),CARGO_NET_GIT_FETCH_WITH_CLI='true',GIT_TERMINAL_PROMPT='0',GIT_CONFIG_COUNT='1',GIT_CONFIG_KEY_0='credential.helper',GIT_CONFIG_VALUE_0='')
    if a.adapter:env['RUSTFLAGS']=env.get('RUSTFLAGS','')+' --check-cfg=cfg(cabs_space_inventory)'
    if space:env['RUSTFLAGS']+=' --cfg cabs_space_inventory'
    cargo=str((a.toolchain/'cargo/bin/cargo').absolute());lock=HERE/'locks'/('seismic-'+pin['commit']+'.lock')
    state=dict(status='running',upstream=pin,archive_sha256=receipt['archive_sha256'],rustc=tool['rustc'],toolchain_date=tool['nightly_date'],dependency_lock='registered' if lock.exists() else 'initial_resolution_pending_registration')
    if a.adapter:state.update(adapter_sha256=digest(HERE/'seismic_fixed.rs'),adaptation_sha256=digest(a.out/'adaptation.patch'),rustflags=env['RUSTFLAGS'])
    if space:state.update(space_inventory=space,rustflags=env['RUSTFLAGS'])
    (a.out/'build_receipt.json').write_text(json.dumps(state,indent=2),encoding='utf-8')
    binaries=('cabs_fixed_queries',) if a.adapter else ('build_inverted_index','perf_inverted_index')
    command=[cargo,'build','--manifest-path',str((target/'Cargo.toml').resolve()),'--locked','--release','--no-default-features']
    for binary in binaries:command+=['--bin',binary]
    command+=['-j2']
    try:
        with (a.out/'build.log').open('w') as log:
            if lock.exists():shutil.copyfile(lock,target/'Cargo.lock')
            else:subprocess.run([cargo,'generate-lockfile','--manifest-path',str((target/'Cargo.toml').resolve())],env=env,stdout=log,stderr=log,check=True)
            state['command']=command;subprocess.run(command,env=env,stdout=log,stderr=log,check=True)
        state.update(status='BUILT_NOT_PARITY_ACCEPTED',lock_sha256=digest(target/'Cargo.lock'),binary_sha256={name:digest(a.out/'target/release'/name) for name in binaries})
    except BaseException as error:
        state.update(status='failed',error=str(error));raise
    finally:(a.out/'build_receipt.json').write_text(json.dumps(state,indent=2),encoding='utf-8')
    print(json.dumps(state,indent=2))


if __name__=='__main__':main()
