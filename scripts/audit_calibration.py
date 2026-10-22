#!/usr/bin/env python3

import argparse
import csv
import datetime as dt
import json
import math
import os
from pathlib import Path
import subprocess

from prepare_data import ROOT, prepare, sha256
from reproduce import capture, save


def summarize(output, evaluation):
    with (output/'candidates_full_cost.csv').open() as stream:
        candidates=list(csv.DictReader(stream))
    with (output/'full_queries.csv').open() as stream:
        queries=list(csv.DictReader(stream))
    with (output/'pool_targets.csv').open() as stream:
        targets=list(csv.DictReader(stream))
    if not candidates:
        raise ValueError('No candidate measurements')
    observations=[]
    for candidate in candidates:
        k=int(candidate['K']);rows=[r for r in queries if int(r['K'])==k]
        if len(rows)!=2*(evaluation+4):
            raise ValueError('Incomplete candidate timing records')
        keys={(r['split'],int(r['repeat']),int(r['query_id'])) for r in rows}
        if len(keys)!=len(rows):
            raise ValueError('Duplicate candidate timings')
        for split,expected in [('calibration',4),('evaluation',evaluation)]:
            selected=[r for r in rows if r['split']==split]
            seconds=sum(float(r['seconds']) for r in selected)
            if len(selected)!=2*expected or not math.isfinite(seconds) or seconds<=0:
                raise ValueError('Invalid timing scope')
            if not math.isclose(seconds/len(selected),float(candidate['full_'+split+'_mean_seconds']),rel_tol=1e-11):
                raise ValueError('Candidate mean does not match raw records')
        positive=[r for r in targets if int(r['K'])==k and int(r['positive_overlap']) and int(r['touched'])]
        capacity=max([100]+[int(r['minimum_prefix_rank']) for r in positive])
        if capacity!=int(candidate['B']):
            raise ValueError('B does not match touched-positive minimum prefix ranks')
        worst=sorted(positive,key=lambda r:int(r['minimum_prefix_rank']),reverse=True)[:4]
        observations.append(dict(candidate,B_driver_targets=worst))
    return {
        'status':'audited',
        'selected_by_proxy_K':int(min(candidates,key=lambda r:float(r['proxy_calibration_seconds']))['K']),
        'lowest_full_calibration_mean_K':int(min(candidates,key=lambda r:float(r['full_calibration_mean_seconds']))['K']),
        'lowest_full_evaluation_mean_K':int(min(candidates,key=lambda r:float(r['full_evaluation_mean_seconds']))['K']),
        'candidates':observations,
        'scope':'temporary calibration indexes; all candidates; four calibration queries; disjoint evaluation subset; two timing repeats',
        'not_a_default_QPS_result':True,
        'not_matched_recall':True,
        'profile_timing_included_in_full_query_measurements':False,
        'profile_note':'instrumented timings have overhead; dot_only_nested overlaps verification/fallback and must not be added to them',
        'method_unchanged':True}


def main():
    parser=argparse.ArgumentParser(description='Diagnose the published K proxy objective without changing default construction.')
    parser.add_argument('dataset',choices=['splade-0.1m','splade-1m'])
    parser.add_argument('--data-directory',type=Path,default=ROOT/'datasets/SPLADE')
    parser.add_argument('--offline',action='store_true')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--eval-queries',type=int,default=8)
    args=parser.parse_args()
    config=json.loads((ROOT/'configs'/('splade_0.1m.json' if args.dataset=='splade-0.1m' else 'splade_1m.json')).read_text())
    expected={'ell':24,'delta_h':.2,'k':50,'k_max':100,'calibration_queries':4,
              'seed':20261007,'build_threads':4,'lloyd_iterations':10,'max_index_gib':9}
    if any(config[key]!=value for key,value in expected.items()):
        parser.error('Audit binary uses the 2026-10-22 public protocol; configuration changed, update the audit explicitly')
    if args.eval_queries<1:
        parser.error('Evaluation subset must be positive')
    docs,queries=prepare(config['scale'],args.data_directory,args.offline)
    output=args.out.resolve();output.mkdir(parents=True,exist_ok=False)
    files=sorted((ROOT/'include').rglob('*.hpp'))+[ROOT/'benchmarks/calibration_audit.cpp',Path(__file__),ROOT/'CMakeLists.txt']
    sources={str(p.relative_to(ROOT)):sha256(p) for p in files}
    manifest={'status':'running','started_utc':dt.datetime.now(dt.timezone.utc).isoformat(),
              'git_commit':capture(['git','rev-parse','HEAD']),'git_worktree_status':capture(['git','status','--porcelain']),
              'source_sha256':sources,'config':config,'evaluation_subset':args.eval_queries,
              'data_sha256':sha256(docs),'queries_sha256':sha256(queries),
              'cpu':capture(['lscpu']),'compiler':capture(['c++','--version'])}
    save(output/'manifest.json',manifest)
    try:
        build=ROOT/'build-audit'
        with (output/'build.log').open('w') as log:
            subprocess.run(['cmake','-S',str(ROOT),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DCABS_SANITIZE=OFF'],stdout=log,stderr=log,check=True)
            subprocess.run(['cmake','--build',str(build),'--target','cabs_calibration_audit','-j4'],stdout=log,stderr=log,check=True)
        binary=build/'cabs_calibration_audit';manifest['binary_sha256']=sha256(binary)
        command=[str(binary),str(docs),str(queries),str(output),str(args.eval_queries)]
        manifest['command']=command;save(output/'manifest.json',manifest)
        with (output/'run.log').open('w') as log:
            subprocess.run(command,env=dict(os.environ,OMP_NUM_THREADS='4'),stdout=log,stderr=log,check=True)
        if any(sha256(ROOT/name)!=value for name,value in sources.items()) or sha256(binary)!=manifest['binary_sha256']:
            raise ValueError('Audit source or binary changed during measurement')
        result=summarize(output,args.eval_queries);save(output/'observation.json',result)
        manifest.update(status='completed',finished_utc=dt.datetime.now(dt.timezone.utc).isoformat())
        save(output/'manifest.json',manifest)
        save(output/'complete.json',{p.name:sha256(p) for p in output.iterdir() if p.is_file() and p.name!='complete.json'})
        print(json.dumps(result,indent=2))
    except BaseException as error:
        manifest.update(status='failed',error=str(error));save(output/'manifest.json',manifest)
        raise


if __name__=='__main__':
    main()
