#!/usr/bin/env python3
"""Independently recompute Recall@50/QPS and audit a completed CABS run."""
import argparse
import csv
import json
import math
import hashlib
import gzip
from collections import defaultdict
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_csv(path):
    path=Path(path)
    compressed=path.with_name(path.name+'.gz')
    stream=path.open(newline='',encoding='utf-8') if path.exists() else gzip.open(compressed,'rt',newline='',encoding='utf-8')
    with stream:
        return list(csv.DictReader(stream))


def audit(directory):
    directory = Path(directory)
    published_manifest=directory/'artifact_manifest.json'
    if published_manifest.exists():
        hashes=json.loads(published_manifest.read_text())['sha256']
        for name,expected in hashes.items():
            require(Path(name).name==name, 'Invalid artifact member name')
            actual=hashlib.sha256((directory/name).read_bytes()).hexdigest()
            require(actual==expected, 'Published artifact checksum mismatch: '+name)
    summary = json.loads((directory/'summary.json').read_text())
    k, n = summary['k'], summary['n']
    split = read_csv(directory/'query_split.csv')
    calibration = [int(r['query_id']) for r in split if r['role']=='calibration']
    evaluation = [int(r['query_id']) for r in split if r['role']=='evaluation']
    require(len(split)==len(calibration)+len(evaluation), 'Unknown query role')
    require(len(set(calibration+evaluation))==len(split), 'Duplicate IDs or calibration/test leakage')
    require(len(calibration)==summary['calibration_queries'], 'Calibration count mismatch')
    require(len(evaluation)==summary['evaluation_queries'], 'Evaluation count mismatch')
    all_queries=set(calibration+evaluation)
    evaluation_ids=set(evaluation)
    truth = defaultdict(list)
    for row in read_csv(directory/'ground_truth.csv'):
        qid, rank, identifier = (int(row[f]) for f in ('query_id','rank','identifier'))
        score = float(row['score'])
        require(qid in all_queries, 'Unexpected GT query ID')
        require(rank==len(truth[qid])+1 and 0<=identifier<n and math.isfinite(score), 'Invalid GT entry')
        truth[qid].append((identifier,score))
    require(set(truth)==all_queries, 'Missing GT query')
    for values in truth.values():
        require(len(values)>=k and len({x[0] for x in values})==len(values), 'Incomplete/duplicate GT')
        require(values==sorted(values,key=lambda v:(-v[1],v[0])), 'GT order is invalid')
    retrieved=defaultdict(list)
    for row in read_csv(directory/'retrieved.csv'):
        require(int(row['k'])==k, 'This entry evaluates one top-k only')
        qid, rank, identifier = (int(row[f]) for f in ('query_id','rank','identifier'))
        score=float(row['score'])
        require(qid in evaluation_ids and rank==len(retrieved[qid])+1, 'Invalid retrieval query/rank')
        require(0<=identifier<n and math.isfinite(score), 'Invalid result ID/score')
        retrieved[qid].append((identifier,score))
    require(set(retrieved)==set(evaluation), 'Missing retrieval query')
    recalls={}
    for qid,values in retrieved.items():
        require(len(values)==k and len({x[0] for x in values})==k, 'Incomplete/duplicate result')
        require(values==sorted(values,key=lambda v:(-v[1],v[0])), 'Result score/ID order invalid')
        recalls[qid]=len({v[0] for v in values}&{v[0] for v in truth[qid][:k]})/k
    records=read_csv(directory/'queries.csv')
    require(len(records)==len(evaluation)*summary['repeats'], 'Incorrect query/repeat count')
    seen=set(); elapsed=0.0; recall_sum=0.0; repeats=defaultdict(list)
    for row in records:
        qid,repeat=int(row['query_id']),int(row['repeat'])
        require(qid in recalls and 0<=repeat<summary['repeats'], 'Unexpected measurement')
        require((repeat,qid) not in seen, 'Duplicate timing record')
        seen.add((repeat,qid))
        latency=float(row['seconds']); recall=float(row['recall'])
        require(int(row['k'])==k and math.isfinite(latency) and latency>0, 'Invalid latency/k')
        require(math.isclose(recall,recalls[qid],abs_tol=1e-12), 'Recall differs from ranked IDs/GT')
        require(int(row['pool_peak'])<=summary['B'] and k<=int(row['verified'])<=n, 'Invalid query counters')
        elapsed+=latency; recall_sum+=recall; repeats[repeat].append(latency)
    recall=recall_sum/len(records); qps=len(records)/elapsed
    require(math.isclose(recall,summary['recall'],abs_tol=1e-12), 'Summary Recall mismatch')
    require(math.isclose(qps,summary['qps'],rel_tol=1e-12), 'Summary QPS mismatch')
    automatic=json.loads((directory/'automatic_configuration.json').read_text())
    require(automatic['K']==summary['K'] and automatic['B']==summary['B'], 'Resolved configuration mismatch')
    require(len(automatic['clusters'])==summary['K'], 'Missing cluster configuration')
    require(all(c['m']>=1 for c in automatic['clusters']), 'Invalid hash count')
    memory=json.loads((directory/'index_space.json').read_text())
    require(sum(memory['components_bytes'].values())==memory['index_structure_bytes']==summary['index_structure_bytes'], 'IS component sum mismatch')
    require(memory['index_plus_vectors_bytes']==memory['index_structure_bytes']+summary['original_vector_bytes'], 'Vector/index accounting mismatch')
    stages=automatic['stage_seconds']
    construction=summary['build_seconds_including_calibration']+summary['calibration_ground_truth_seconds']+stages['df_statistics']+stages['dataset_loading_validation']
    require(math.isfinite(construction) and construction>0 and math.isclose(construction,summary['construction_seconds'],rel_tol=1e-12), 'IT accounting mismatch')
    metrics={'status':'measured','dataset':summary['dataset'],'documents':n,'k':k,
             'Recall@50':recall,'QPS':qps,'IT_seconds':construction,
             'IS_bytes':memory['index_plus_vectors_bytes'],'IS_GB':memory['index_plus_vectors_bytes']/1e9,
             'IS_definition':'retained index payload plus required original CSR vector storage; excludes allocator/node overhead and query scratch',
             'index_structure_bytes':memory['index_structure_bytes'],
             'index_structure_definition':memory['definition'],'original_vector_bytes':summary['original_vector_bytes'],
             'index_plus_vectors_bytes':memory['index_plus_vectors_bytes'],
             'calibration_queries':len(calibration),'evaluation_queries':len(evaluation),
             'query_threads':1,'repeats':summary['repeats'],'measured_queries':len(records),
             'repeat_QPS':{str(r):len(t)/sum(t) for r,t in repeats.items()}}
    (directory/'metrics.json').write_text(json.dumps(metrics,indent=2,allow_nan=False)+'\n')
    report={'status':'passed','checks':['disjoint query sets','exact-GT recall recomputation',
             'ranked unique in-range IDs','complete unique query/repeat records',
             'QPS recomputed from all query times','resolved configuration consistency',
             'IT accounting','IS component sum'],'query_measurements':len(records),
             'evaluation_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (directory/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
    return metrics


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    print(json.dumps(audit(parser.parse_args().directory),indent=2))
