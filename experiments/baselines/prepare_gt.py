import argparse
import csv
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from protocol import checked_plan,load,require,save,sha

HERE=Path(__file__).resolve().parent


def producer_sources():
    paths=[Path(__file__),HERE/'exact_gt.cpp',HERE.parent/'protocol.py']
    return {p.relative_to(HERE.parents[1]).as_posix():sha(p) for p in paths}


def evaluation_lock(path,plan,data,queries,k):
    require(path is not None,'Evaluation GT requires a validation lock')
    lock=load(path);require(sha(path)==Path(str(path)+'.sha256').read_text().strip(),'Validation lock changed')
    require(lock.get('selected') is not None,'Unmatched validation target')
    binding=lock['selected']['binding']
    require(binding['plan_sha256']==sha(plan) and binding['data_sha256']==sha(data) and binding['queries_sha256']==sha(queries) and binding['config']['k']==k,'GT inputs or k differ from locked validation')
    return sha(path)


def checked_gt(directory):
    root=Path(directory);complete=load(root/'complete.json')
    required={'manifest.json','ground_truth.csv','measured_ids.txt','summary.json','producer.log'}
    require(set(complete)==required,'Incomplete/unexpected GT artifact set')
    for name,value in complete.items():require(Path(name).name==name and sha(root/name)==value,'GT artifact changed: '+name)
    manifest=load(root/'manifest.json');require(manifest['status']=='completed','Incomplete GT generation')
    with (root/'ground_truth.csv').open() as f:rows=list(csv.DictReader(f))
    expected=[(qid,rank) for qid in manifest['query_ids'] for rank in range(1,manifest['k']+1)]
    require([(int(r['query_id']),int(r['rank'])) for r in rows]==expected,'GT query IDs/ranks differ from explicit role')
    for start in range(0,len(rows),manifest['k']):
        hits=[(int(r['identifier']),float(r['score'])) for r in rows[start:start+manifest['k']]]
        require(len({did for did,_ in hits})==manifest['k'] and all(0<=did<manifest['documents'] and math.isfinite(value) and value>=0 for did,value in hits),'Invalid/duplicate GT identifiers or scores')
        require(hits==sorted(hits,key=lambda h:(-h[1],h[0])),'GT ordering/tie rule differs')
    require([int(v) for v in (root/'measured_ids.txt').read_text().split()]==manifest['query_ids'],'GT measured IDs differ')
    return manifest,rows


def generate(args):
    plan=checked_plan(args.plan,args.queries);require(0<=args.calibration_seed<2**64,'Calibration seed outside uint64')
    with args.data.open('rb') as f:documents,dimensions,nnz=struct.unpack('<QQQ',f.read(24))
    require(0<args.k<=documents,'Invalid k for collection')
    lock_hash=None
    if args.role=='evaluation':lock_hash=evaluation_lock(args.lock,args.plan,args.data,args.queries,args.k)
    else:require(args.lock is None,'Non-evaluation GT cannot consume evaluation lock')
    query_ids=random.Random(args.calibration_seed).sample(plan['reservoir'],plan['calibration_count']) if args.role=='calibration' else plan[args.role]
    binding=dict(plan_sha256=sha(args.plan),data_sha256=sha(args.data),queries_sha256=sha(args.queries),producer_binary_sha256=sha(args.binary),producer_sources=producer_sources())
    args.out.mkdir(parents=True,exist_ok=False);(args.out/'measured_ids.txt').write_text(''.join(str(q)+'\n' for q in query_ids))
    manifest=dict(status='running',role=args.role,query_ids=query_ids,k=args.k,documents=documents,binding=binding,calibration_seed=args.calibration_seed if args.role=='calibration' else None,lock_sha256=lock_hash,scope='Independent exact-GT generation; no retrieval-performance claim')
    save(args.out/'manifest.json',manifest)
    command=[str(args.binary.resolve()),str(args.data.resolve()),str(args.queries.resolve()),str((args.out/'measured_ids.txt').resolve()),str(args.k),str(args.out.resolve())]
    try:
        with (args.out/'producer.log').open('w') as log:subprocess.run(command,stdout=log,stderr=log,check=True)
        require(binding==dict(plan_sha256=sha(args.plan),data_sha256=sha(args.data),queries_sha256=sha(args.queries),producer_binary_sha256=sha(args.binary),producer_sources=producer_sources()),'GT inputs/source/binary changed during generation')
        if args.role=='evaluation':require(lock_hash==evaluation_lock(args.lock,args.plan,args.data,args.queries,args.k),'GT validation lock changed during generation')
        manifest.update(status='completed',command=command);(args.out/'manifest.json').write_text(json.dumps(manifest,indent=2))
        save(args.out/'complete.json',{name:sha(args.out/name) for name in ('manifest.json','ground_truth.csv','measured_ids.txt','summary.json','producer.log')});checked_gt(args.out)
    except BaseException:
        manifest['status']='failed';(args.out/'manifest.json').write_text(json.dumps(manifest,indent=2));raise
    print(json.dumps(dict(status='PASS',role=args.role,queries=len(query_ids),k=args.k,scope=manifest['scope'])))


if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('data','queries','plan','binary','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--role',choices=['calibration','selection','evaluation'],required=True);p.add_argument('--k',type=int,default=50);p.add_argument('--calibration-seed',type=int,default=20261007);p.add_argument('--lock',type=Path)
    generate(p.parse_args())
