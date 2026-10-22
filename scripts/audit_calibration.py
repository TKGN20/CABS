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
from evaluate import audit, read_csv


def match_recorded_counters(observed,recorded):
    for key in ('verified','touched','probed','bucket_accesses','entries','pool_peak'):
        original_key='matched_entries' if key=='entries' else key
        if int(observed[key])!=int(recorded[original_key]):
            raise ValueError('Selected final-index replay counter mismatch: '+key)


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
    parser.add_argument('--selected-from',type=Path,help='Reconstruct the recorded selected final index and verify ranked results/counters before accepting its profile')
    args=parser.parse_args()
    config=json.loads((ROOT/'configs'/('splade_0.1m.json' if args.dataset=='splade-0.1m' else 'splade_1m.json')).read_text())
    expected={'ell':24,'delta_h':.2,'k':50,'k_max':100,'calibration_queries':4,
              'seed':20261007,'build_threads':4,'lloyd_iterations':10,'max_index_gib':9}
    if any(config[key]!=value for key,value in expected.items()):
        parser.error('Audit binary uses the 2026-10-22 public protocol; configuration changed, update the audit explicitly')
    if args.eval_queries<1:
        parser.error('Evaluation subset must be positive')
    docs,queries=prepare(config['scale'],args.data_directory,args.offline)
    recorded=None
    if args.selected_from:
        directory=args.selected_from.resolve();audit(directory)
        original=json.loads((directory/'manifest.json').read_text())
        original_config=json.loads((directory/'config.json').read_text())
        if original['status']!='completed' or original['data']['sha256']!=sha256(docs) or original['queries']['sha256']!=sha256(queries):
            parser.error('Selected record must be completed on identical inputs')
        if any(original_config.get(key)!=value for key,value in expected.items()) or original_config.get('retain_sketch_cache') is not False:
            parser.error('Recorded protocol differs from the supported final-index replay')
        if any(sha256(ROOT/name)!=value for name,value in original['source_sha256'].items()):
            parser.error('Production source differs from the completed record')
        recorded={'directory':str(directory),'automatic':json.loads((directory/'automatic_configuration.json').read_text()),
                  'manifest_sha256':sha256(directory/'manifest.json'),'source_commit':original['git_commit']}
    output=args.out.resolve();output.mkdir(parents=True,exist_ok=False)
    files=sorted((ROOT/'include').rglob('*.hpp'))+sorted((ROOT/'scripts').glob('*.py'))+sorted((ROOT/'configs').glob('*.json'))+[ROOT/'benchmarks/calibration_audit.cpp',ROOT/'CMakeLists.txt']
    sources={str(p.relative_to(ROOT)):sha256(p) for p in files}
    manifest={'status':'running','started_utc':dt.datetime.now(dt.timezone.utc).isoformat(),
              'git_commit':capture(['git','rev-parse','HEAD']),'git_worktree_status':capture(['git','status','--porcelain']),
              'source_sha256':sources,'config':config,'evaluation_subset':args.eval_queries,
              'data_sha256':sha256(docs),'queries_sha256':sha256(queries),
              'cpu':capture(['lscpu']),'compiler':capture(['c++','--version'])}
    if recorded:
        manifest['selected_record']=recorded
    save(output/'manifest.json',manifest)
    try:
        build=ROOT/'build-audit'
        with (output/'build.log').open('w') as log:
            subprocess.run(['cmake','-S',str(ROOT),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DCABS_SANITIZE=OFF'],stdout=log,stderr=log,check=True)
            subprocess.run(['cmake','--build',str(build),'--target','cabs_calibration_audit','-j4'],stdout=log,stderr=log,check=True)
        binary=build/'cabs_calibration_audit';manifest['binary_sha256']=sha256(binary)
        command=[str(binary),str(docs),str(queries),str(output),str(args.eval_queries)]
        if recorded:
            command.extend([str(recorded['automatic']['K']),str(recorded['automatic']['B'])])
        manifest['command']=command;save(output/'manifest.json',manifest)
        with (output/'run.log').open('w') as log:
            subprocess.run(command,env=dict(os.environ,OMP_NUM_THREADS='4'),stdout=log,stderr=log,check=True)
        if any(sha256(ROOT/name)!=value for name,value in sources.items()) or sha256(binary)!=manifest['binary_sha256']:
            raise ValueError('Audit source or binary changed during measurement')
        result=summarize(output,args.eval_queries)
        if recorded:
            observed={}
            for row in read_csv(output/'full_results.csv'):
                observed.setdefault(int(row['query_id']),[]).append((int(row['identifier']),float(row['score'])))
            expected_hits={}
            for row in read_csv(args.selected_from/'retrieved.csv'):
                qid=int(row['query_id'])
                if qid in observed:
                    expected_hits.setdefault(qid,[]).append((int(row['identifier']),float(row['score'])))
            if observed!=expected_hits:
                raise ValueError('Selected final-index replay differs from recorded ranked results')
            original_queries={int(row['query_id']):row for row in read_csv(args.selected_from/'queries.csv') if int(row['repeat'])==0}
            checked=0
            for row in read_csv(output/'full_queries.csv'):
                if row['split']=='evaluation':
                    reference=original_queries[int(row['query_id'])]
                    match_recorded_counters(row,reference)
                    checked+=1
            result.update(scope='reconstructed recorded selected final index; B recalibrated; fresh final seed domain; cache released; subset only',
                          selected_record_results_equal=True,selected_record_queries=len(observed),selected_record_counter_checks=checked,
                          selected_record_source_commit=recorded['source_commit'])
        save(output/'observation.json',result)
        manifest.update(status='completed',finished_utc=dt.datetime.now(dt.timezone.utc).isoformat())
        save(output/'manifest.json',manifest)
        save(output/'complete.json',{p.name:sha256(p) for p in output.iterdir() if p.is_file() and p.name!='complete.json'})
        print(json.dumps(result,indent=2))
    except BaseException as error:
        manifest.update(status='failed',error=str(error));save(output/'manifest.json',manifest)
        raise


if __name__=='__main__':
    main()
