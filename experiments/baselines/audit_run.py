import argparse
import csv
import json
import math
from pathlib import Path
import sys

from oracle import CSR,score
from prepare_gt import checked_gt,evaluation_lock,producer_sources
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from protocol import checked_plan,load,require,save,sha


def audit_run(data,queries,plan,ground_truth,results,method,repeats,lock=None):
    require(method in ('CABS','SOSIA','SINDI','SEISMIC') and type(repeats) is int and repeats>0,'Invalid method or timing repeats')
    query_plan=checked_plan(plan,queries);gt,truth_rows=checked_gt(ground_truth)
    require(gt['role'] in ('selection','evaluation'),'Calibration queries cannot be benchmark queries')
    require(gt['query_ids']==query_plan[gt['role']],'GT IDs differ from the query plan role')
    binding=gt['binding']
    require(binding['data_sha256']==sha(data) and binding['queries_sha256']==sha(queries) and binding['plan_sha256']==sha(plan),'GT inputs differ from benchmark inputs')
    require(binding['producer_sources']==producer_sources(),'GT producer source differs from audited source')
    if gt['role']=='evaluation':
        require(gt['lock_sha256']==evaluation_lock(lock,plan,data,queries,gt['k']),'Evaluation GT lock differs')
        require(load(lock)['selected']['binding'].get('method',method)==method,'Evaluation lock belongs to another method')
    else:require(lock is None,'Selection audit cannot consume evaluation lock')
    folder=Path(results);summary=load(folder/'summary.json')
    input_hashes={name:sha(path) for name,path in [('data',data),('queries',queries),('plan',plan),('GT_complete',Path(ground_truth)/'complete.json')]}
    names=['summary.json','retrieved.csv','queries.csv']
    if (folder/'index_space.json').is_file():names.append('index_space.json')
    require(summary.get('IS_status')!='REPORTED_NOT_COMMON_VALIDATED' or 'index_space.json' in names,'Reported component status requires its bound inventory')
    result_hashes={name:sha(folder/name) for name in names}
    require(summary.get('method',method)==method and summary.get('search_threads',1)==1,'Method or search-thread policy differs')
    if method!='CABS':require(summary.get('exact_binary64_reranking_in_timer') is True,'Baseline exact scoring is not declared inside query timing')
    expected_ids=gt['query_ids'];k=gt['k'];truth={qid:[] for qid in expected_ids};observed={qid:[] for qid in expected_ids}
    for row in truth_rows:truth[int(row['query_id'])].append(int(row['identifier']))
    docs=CSR(data);qs=CSR(queries)
    try:
        require(docs.d==qs.d and docs.n==gt['documents'],'GT collection dimensions differ')
        target_queries={qid:dict(qs.row(qid)) for qid in expected_ids}
        with (folder/'retrieved.csv').open(newline='') as f:
            for row in csv.DictReader(f):
                qid=int(row['query_id']);did=int(row['identifier']);rank=int(row['rank']);value=float(row['score'])
                require(qid in observed,'Unexpected original query ID')
                require(rank==len(observed[qid])+1 and rank<=k and math.isfinite(value),'Invalid result rank/score')
                require(value==score(docs.row(did),target_queries[qid]),'Result score differs from original binary64 scoring')
                observed[qid].append((did,value))
        recalls={}
        for qid,hits in observed.items():
            require(len(hits)==k and len({did for did,_ in hits})==k,'Missing query, duplicate result or underfilled top-k')
            require(hits==sorted(hits,key=lambda h:(-h[1],h[0])),'Result ranking/tie rule differs')
            recalls[qid]=len(set(truth[qid]).intersection(did for did,_ in hits))/k
        with (folder/'queries.csv').open(newline='') as f:timings=list(csv.DictReader(f))
        expected={(repeat,qid) for repeat in range(repeats) for qid in expected_ids}
        require(len(timings)==len(expected) and {(int(row['repeat']),int(row['query_id'])) for row in timings}==expected,'Missing/duplicate query timing or different original IDs')
        elapsed=0.0
        for row in timings:
            latency=float(row['seconds']);require(math.isfinite(latency) and latency>0,'Invalid measured latency');elapsed+=latency
            if 'recall' in row:require(float(row['recall'])==recalls[int(row['query_id'])],'Reported query recall differs from independent GT')
        recall=sum(recalls.values())/len(recalls);qps=len(timings)/elapsed
        for key,value in [('recall',recall),('qps',qps)]:
            if key in summary:require(math.isclose(float(summary[key]),value,rel_tol=1e-12,abs_tol=1e-12),'Native summary differs from audited '+key)
    finally:docs.close();qs.close()
    require(input_hashes=={name:sha(path) for name,path in [('data',data),('queries',queries),('plan',plan),('GT_complete',Path(ground_truth)/'complete.json')]} and result_hashes=={name:sha(folder/name) for name in result_hashes},'Inputs or records changed during audit')
    checked_gt(ground_truth)
    if gt['role']=='evaluation':require(gt['lock_sha256']==evaluation_lock(lock,plan,data,queries,k),'Evaluation lock changed during audit')
    sources={p.name:sha(p) for p in (Path(__file__),Path(__file__).with_name('oracle.py'),Path(__file__).with_name('prepare_gt.py'))}
    return dict(status='MEASURED_RECORD_AUDIT_ONLY',method=method,role=gt['role'],query_ids=expected_ids,k=k,repeats=repeats,Recall=recall,QPS=qps,measured_records=len(timings),query_recalls=recalls,
                binding=dict(data_sha256=sha(data),queries_sha256=sha(queries),plan_sha256=sha(plan),GT_complete_sha256=sha(Path(ground_truth)/'complete.json'),result_artifacts=result_hashes,audit_sources=sources),
                IS_status=summary.get('IS_status','NOT_COMMON_BASELINE_VALIDATED'),scope='Original-ID, exact-score and timing audit against separately generated role-bound GT; not source/binary provenance, parameter-selection, IS, fair-performance or server acceptance')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('data','queries','plan','ground-truth','results','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--method',choices=['CABS','SOSIA','SINDI','SEISMIC'],required=True);p.add_argument('--repeats',type=int,default=3);p.add_argument('--lock',type=Path)
    a=p.parse_args();result=audit_run(a.data,a.queries,a.plan,a.ground_truth,a.results,a.method,a.repeats,a.lock);save(a.out,result)
    print(json.dumps({key:result[key] for key in ('status','method','role','Recall','QPS','scope')}))
