#!/usr/bin/env python3

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

from prepare_data import ROOT, prepare, sha256
from evaluate import audit


def save(path,value):
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def capture(command):
    try:
        return subprocess.check_output(command,cwd=ROOT,text=True,stderr=subprocess.DEVNULL).strip()
    except (OSError,subprocess.CalledProcessError):
        return None


def main():
    parser=argparse.ArgumentParser(description='Run one static CABS dataset configuration.')
    parser.add_argument('dataset',choices=['splade-0.1m','splade-1m'])
    parser.add_argument('--data-directory',type=Path,default=ROOT/'datasets/SPLADE')
    parser.add_argument('--offline',action='store_true')
    parser.add_argument('--out',type=Path,help='A new directory; existing runs are never overwritten')
    parser.add_argument('--eval-queries',type=int,help='Explicit subset verification; not a full-query run')
    parser.add_argument('--max-index-gib',type=float,help='Resource guard; never caps/skips an algorithm configuration')
    args=parser.parse_args()
    config_file=ROOT/'configs'/('splade_0.1m.json' if args.dataset=='splade-0.1m' else 'splade_1m.json')
    config=json.loads(config_file.read_text())
    if config['query_threads']!=1 or not config['auto_configuration']:
        parser.error('Public configuration must use automatic calibration and single-threaded search')
    docs,queries=prepare(config['scale'],args.data_directory,args.offline)
    import struct
    with queries.open('rb') as stream:
        query_n=struct.unpack('<Q',stream.read(8))[0]
    evaluation=args.eval_queries if args.eval_queries is not None else query_n-config['calibration_queries']
    if not 0<evaluation<=query_n-config['calibration_queries']:
        parser.error('Invalid evaluation subset size')
    guard=args.max_index_gib if args.max_index_gib is not None else config['max_index_gib']
    if not guard>0 or not guard<float('inf'):
        parser.error('Memory guard must be finite and positive')
    started=dt.datetime.now(dt.timezone.utc)
    output=(args.out or ROOT/'results'/args.dataset/started.strftime('%Y%m%dT%H%M%S%fZ')).resolve()
    output.mkdir(parents=True,exist_ok=False)
    config.update(evaluation_queries=evaluation,max_index_gib=guard,
                  full_query_run=evaluation==query_n-config['calibration_queries'])
    save(output/'config.json',config)
    source_files=sorted((ROOT/'include').rglob('*.hpp'))+sorted((ROOT/'src').rglob('*.cpp'))
    sources={str(p.relative_to(ROOT)):sha256(p) for p in source_files}
    pipeline_files=sorted((ROOT/'scripts').glob('*.py'))+sorted((ROOT/'scripts').glob('*.sh'))+sorted((ROOT/'configs').glob('*.json'))+[ROOT/'CMakeLists.txt']
    manifest={'status':'running','started_utc':started.isoformat(),
              'dataset':config['dataset'],'config_file':str(config_file.relative_to(ROOT)),
              'source_sha256':sources,'source_hash':hashlib.sha256(json.dumps(sources,sort_keys=True).encode()).hexdigest(),
              'pipeline_sha256':{str(p.relative_to(ROOT)):sha256(p) for p in pipeline_files},
              'git_commit':capture(['git','rev-parse','HEAD']),
              'git_worktree_status':capture(['git','status','--porcelain']),
              'platform':platform.platform(),'cpu':capture(['lscpu']),
              'memory':Path('/proc/meminfo').read_text() if Path('/proc/meminfo').exists() else None,
              'python':sys.version,'compiler':capture(['c++','--version']),
              'data':{'sha256':sha256(docs),'bytes':docs.stat().st_size},
              'queries':{'sha256':sha256(queries),'bytes':queries.stat().st_size},
              'query_threads':1,'warmup_queries':min(evaluation,config['warmup_queries']),
              'timing_repeats':config['timing_repeats'],'timing':'steady monotonic OpenMP wall clock; complete search; pooled query count/summed time'}
    save(output/'manifest.json',manifest)
    clock=time.monotonic()
    try:
        with (output/'build.log').open('w') as log:
            for command in (['cmake','-S',str(ROOT),'-B',str(ROOT/'build'),'-DCMAKE_BUILD_TYPE=Release','-DCABS_SANITIZE=OFF'],
                            ['cmake','--build',str(ROOT/'build'),'--clean-first','-j',str(config['build_threads'])]):
                subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        binary=ROOT/'build/cabs'
        manifest['binary_sha256']=sha256(binary)
        flag_file=ROOT/'build/CMakeFiles/cabs.dir/flags.make'
        if flag_file.exists():
            (output/'build_flags.txt').write_text(flag_file.read_text())
            manifest['build_flags_sha256']=sha256(output/'build_flags.txt')
        if any(sha256(ROOT/name)!=value for name,value in sources.items()):
            raise RuntimeError('Core source changed during compilation; start a fresh run')
        command=[str(binary),'run','--data',str(docs),'--queries',str(queries),'--out',str(output),
                 '--ell',str(config['ell']),'--delta-h',str(config['delta_h']),
                 '--k',str(config['k']),'--kmax',str(config['k_max']),
                 '--calibration-queries',str(config['calibration_queries']),'--eval-queries',str(evaluation),
                 '--seed',str(config['seed']),'--threads',str(config['build_threads']),
                 '--iterations',str(config['lloyd_iterations']),'--repeats',str(config['timing_repeats']),
                 '--max-index-gib',str(guard),'--warmup-queries',str(config['warmup_queries']),
                 '--benchmark-exact','0','--diagnostic-queries','0','--equivalence-queries','0']
        manifest['command']=command
        save(output/'manifest.json',manifest)
        environment=dict(os.environ,OMP_NUM_THREADS=str(config['build_threads']))
        with (output/'run.log').open('w') as log:
            log.write(json.dumps({'command':command,'platform':manifest['platform'],'cpu':manifest['cpu']})+'\n')
            log.flush()
            measured=['/usr/bin/time','-v','-o',str(output/'resource_usage.log'),*command]
            subprocess.run(measured,cwd=ROOT,env=environment,stdout=log,stderr=log,check=True)
        if any(sha256(ROOT/name)!=value for name,value in sources.items()):
            raise RuntimeError('Core source changed during measurement; run is not accepted')
        if any(sha256(ROOT/name)!=value for name,value in manifest['pipeline_sha256'].items()):
            raise RuntimeError('Pipeline or configuration changed during measurement; run is not accepted')
        if sha256(binary)!=manifest['binary_sha256']:
            raise RuntimeError('Executable changed during measurement; run is not accepted')
        if sha256(docs)!=manifest['data']['sha256'] or sha256(queries)!=manifest['queries']['sha256']:
            raise RuntimeError('Input data changed during measurement; run is not accepted')
        metrics=audit(output)
        automatic=json.loads((output/'automatic_configuration.json').read_text())
        resolved={'external':config,'automatic':automatic,'source_hash':manifest['source_hash'],
                  'data_sha256':manifest['data']['sha256'],'query_sha256':manifest['queries']['sha256']}
        save(output/'resolved_config.json',resolved)
        manifest.update(status='completed',finished_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                        pipeline_wall_seconds=time.monotonic()-clock)
        save(output/'manifest.json',manifest)
        save(output/'complete.json',{p.name:sha256(p) for p in output.iterdir() if p.is_file() and p.name!='complete.json'})
        print(json.dumps({'directory':str(output),'metrics':metrics},indent=2),flush=True)
    except BaseException as error:
        manifest.update(status='failed',error=str(error),finished_utc=dt.datetime.now(dt.timezone.utc).isoformat())
        save(output/'manifest.json',manifest)
        raise


if __name__=='__main__':
    main()
