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
    return len(hashes)


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
    return {'status':'PASS','artifact_members_checked':checked,'canonical_source_files_checked':sources,
            'full_runs':{name:{k:v for k,v in metrics.items() if k in ('Recall@50','QPS','IS_bytes','measured_queries')} for name,metrics in full.items()},
            'complete_1m_non_timing_equivalence':True,'diagnostics':diagnostics,'selected_final_index_query_matches':len(actual),
            'scope':'read-only saved-record and optional historical-source verification; no new retrieval run or independent whole-corpus GT computation'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description='Verify public CABS evidence without data downloads or a local delivery ZIP.')
    parser.add_argument('--source-check',action='store_true',help='Check canonical source bytes using complete local Git history')
    print(json.dumps(verify(parser.parse_args().source_check),indent=2))
