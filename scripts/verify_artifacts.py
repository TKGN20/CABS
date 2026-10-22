#!/usr/bin/env python3

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess

from evaluate import audit, read_csv, require
from prepare_data import ROOT
from audit_calibration import summarize, match_recorded_counters
from calibration_pilot import common_metrics
from approval_analysis import routing_summary, frozen_budget_analysis


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_sources(directory):
    attestation=directory/'source_attestation.json'
    provenance=directory/'provenance.json'
    if attestation.exists():
        proof=json.loads(attestation.read_text())
        commit=proof.get('source_snapshot_commit',proof.get('known_detached_snapshot'))
    elif provenance.exists():
        proof=json.loads(provenance.read_text())
        commit=proof.get('published_source_commit',proof.get('source_snapshot_commit'))
    else:
        return 0
    hashes=proof.get('published_source_sha256',{})
    if not hashes:
        return 0
    require(commit is not None and re.fullmatch('[0-9a-f]{40}',commit),'Missing canonical source commit')
    for name,value in hashes.items():
        path=Path(name)
        require(not path.is_absolute() and '..' not in path.parts and ':' not in name,'Invalid source path')
        try:
            blob=subprocess.check_output(['git','-c','safe.directory='+str(ROOT),'show',commit+':'+name],cwd=ROOT,stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as error:
            raise ValueError('Source history unavailable; use a full Git clone and git fetch --unshallow when applicable') from error
        require(digest(blob)==value,'Canonical source checksum: '+name)
        if 'line_ending_variants' in proof:
            lf=blob.replace(b'\r\n',b'\n')
            require(proof['measured_source_sha256'][name] in {digest(blob),digest(lf),digest(lf.replace(b'\n',b'\r\n'))},'Measured source differs beyond line endings: '+name)
    return len(hashes)


def verify_approval_evidence(recorded_queries,complete_directory):
    engineering=ROOT/'results/engineering'
    pilot=engineering/'calibration_pilot_20261022'
    comparison=json.loads((pilot/'comparison.json').read_text())
    manifest=json.loads((pilot/'manifest.json').read_text())
    require(comparison['status']==manifest['status']=='completed','Incomplete pilot')
    require(len(comparison['cases'])==4,'Incomplete pilot matrix')
    common=comparison['common_validation_query_ids'];pool=comparison['calibration_pool_query_ids']
    require(len(common)==64 and len(set(common))==64 and len(set(pool))==8,'Pilot query scope')
    require([[r['calibration_queries'],r['k_max']] for r in comparison['cases']]==manifest['case_order'],'Pilot case order')
    for case in comparison['cases']:
        nc=case['calibration_queries'];kmax=case['k_max']
        directory=pilot/('nc'+str(nc)+'_kmax'+str(kmax))
        metrics=audit(directory,write_outputs=False)
        actual=common_metrics(directory,common,pool)
        require(actual==json.loads((directory/'common_metrics.json').read_text()),'Pilot common metrics mismatch')
        split=read_csv(directory/'query_split.csv')
        calibration=[int(r['query_id']) for r in split if r['role']=='calibration']
        evaluation=[int(r['query_id']) for r in split if r['role']=='evaluation']
        require(calibration==pool[:nc] and evaluation==pool[nc:]+common,'Pilot split mismatch')
        automatic=json.loads((directory/'automatic_configuration.json').read_text())
        expected=dict(calibration_queries=nc,k_max=kmax,K=automatic['K'],B=automatic['B'],
                      min_m=min(r['m'] for r in automatic['clusters']),max_m=max(r['m'] for r in automatic['clusters']),
                      IT_seconds=metrics['IT_seconds'],IS_GB=metrics['IS_GB'])
        expected.update({k:actual[k] for k in ('Recall@50','QPS','mean_verified','mean_probed')})
        require(case==expected,'Pilot comparison mismatch')
        provenance=json.loads((directory/'manifest.json').read_text())
        require(provenance['status']=='completed' and provenance['source_commit']==manifest['git_commit'] and provenance['binary_sha256']==manifest['binary_sha256'],'Pilot provenance mismatch')
    routing=engineering/'routing_bounds_1m_20261022'
    bounds=routing_summary(routing)
    require(bounds==json.loads((routing/'analysis.json').read_text()),'Routing analysis mismatch')
    require(bounds['queries']==16 and bounds['exact_cluster_maximum_checks']==32,'Routing diagnostic scope')
    summarize(routing,16)
    actual={};expected={}
    for row in read_csv(routing/'full_results.csv'):
        actual.setdefault(int(row['query_id']),[]).append((int(row['identifier']),float(row['score'])))
    for row in read_csv(complete_directory/'retrieved.csv'):
        qid=int(row['query_id'])
        if qid in actual:expected.setdefault(qid,[]).append((int(row['identifier']),float(row['score'])))
    require(actual==expected and len(actual)==16,'Routing replay results mismatch')
    for row in read_csv(routing/'full_queries.csv'):
        if row['split']=='evaluation':match_recorded_counters(row,recorded_queries[int(row['query_id'])])
    frozen=frozen_budget_analysis(engineering/'selected_profile_1m_20261022',complete_directory)
    saved=read_csv(engineering/'frozen_B_20261022/frozen_B_subsets.csv')
    normalized=[{k:v if k=='query_ids' else int(v) for k,v in r.items()} for r in saved]
    require(normalized==frozen['rows'] and len(normalized)==30,'Frozen budget recomputation mismatch')
    return dict(pilot_cases=4,common_queries_per_case=64,common_timing_records_per_case=192,
                routing=bounds,frozen_budget_combinations=len(normalized))


def verify(source_check=False):
    checked=0;sources=0
    for manifest in sorted((ROOT/'results').rglob('artifact_manifest.json')):
        directory=manifest.parent
        for name,value in json.loads(manifest.read_text())['sha256'].items():
            require(Path(name).name==name,'Invalid artifact path')
            require(digest((directory/name).read_bytes())==value,'Artifact checksum: '+str(directory/name))
            checked+=1
        if source_check:
            sources+=check_sources(directory)
    full={}
    for name in ('measured/splade-0.1m','measured/splade-1m','engineering/cache_20261022','engineering/review_1m_20261022'):
        full[name]=audit(ROOT/'results'/name,write_outputs=False)
    old=ROOT/'results/measured/splade-1m';new=ROOT/'results/engineering/review_1m_20261022'
    for name in ('ground_truth.csv','retrieved.csv'):
        require(gzip.decompress((old/(name+'.gz')).read_bytes())==gzip.decompress((new/(name+'.gz')).read_bytes()),'Complete 1M raw record mismatch: '+name)
    require((old/'query_split.csv').read_bytes()==(new/'query_split.csv').read_bytes(),'Complete 1M split mismatch')
    before=read_csv(old/'queries.csv');after=read_csv(new/'queries.csv')
    require([{k:v for k,v in r.items() if k!='seconds'} for r in before]==[{k:v for k,v in r.items() if k!='seconds'} for r in after],'Complete 1M non-timing mismatch')
    diagnostics={}
    for name,count in [('calibration_audit_small_20261022',8),('selected_profile_1m_20261022',16)]:
        observation=summarize(ROOT/'results/engineering'/name,count)
        diagnostics[name]={'candidates':len(observation['candidates'])}
    selected=ROOT/'results/engineering/selected_profile_1m_20261022'
    actual={};expected={}
    for row in read_csv(selected/'full_results.csv'):
        actual.setdefault(int(row['query_id']),[]).append((int(row['identifier']),float(row['score'])))
    for row in read_csv(new/'retrieved.csv'):
        qid=int(row['query_id'])
        if qid in actual:expected.setdefault(qid,[]).append((int(row['identifier']),float(row['score'])))
    require(actual==expected and len(actual)==16,'Selected final-index results mismatch')
    recorded={int(r['query_id']):r for r in after if int(r['repeat'])==0}
    for row in read_csv(selected/'full_queries.csv'):
        if row['split']=='evaluation':match_recorded_counters(row,recorded[int(row['query_id'])])
    approval=verify_approval_evidence(recorded,new)
    return {'status':'PASS','artifact_members_checked':checked,'canonical_source_files_checked':sources,
            'full_runs':{name:{k:v for k,v in metrics.items() if k in ('Recall@50','QPS','IS_bytes','measured_queries')} for name,metrics in full.items()},
            'complete_1m_non_timing_equivalence':True,'diagnostics':diagnostics,'selected_final_index_query_matches':len(actual),
            'approval_evidence':approval,
            'scope':'read-only saved-record and optional historical-source verification; no new retrieval run or independent whole-corpus GT computation'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description='Verify public CABS evidence without data downloads or a local delivery ZIP.')
    parser.add_argument('--source-check',action='store_true',help='Check canonical source bytes using complete local Git history')
    print(json.dumps(verify(parser.parse_args().source_check),indent=2))
