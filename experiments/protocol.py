import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def save(path,value):
    with Path(path).open('x',encoding='utf-8',newline='\n') as f:json.dump(value,f,indent=2,allow_nan=False)


def require(condition,message):
    if not condition:raise ValueError(message)


def code_hashes():
    paths=list((ROOT/'include').rglob('*.hpp'))+list((ROOT/'experiments').glob('*.hpp'))+[Path(__file__),ROOT/'experiments/fixed_queries.cpp']
    return {p.relative_to(ROOT).as_posix():sha(p) for p in sorted(paths)}


def plan(queries,out,seed,reservoir,selection,nc):
    with Path(queries).open('rb') as f:n,d,nnz=struct.unpack('<QQQ',f.read(24))
    require(0<nc<=reservoir and selection>0 and reservoir+selection<n,'Invalid split sizes')
    ids=list(range(n));random.Random(seed).shuffle(ids)
    save(out,dict(queries_sha256=sha(queries),query_count=n,split_seed=seed,calibration_count=nc,
                  split_algorithm='Python random.Random.shuffle; explicit IDs authoritative',
                  reservoir=ids[:reservoir],selection=ids[reservoir:reservoir+selection],evaluation=ids[reservoir+selection:]))


def checked_plan(path,queries):
    p=load(path);ids=p['reservoir']+p['selection']+p['evaluation']
    require(p['queries_sha256']==sha(queries),'Query input changed')
    require(len(ids)==p['query_count'] and set(ids)==set(range(p['query_count'])),'Split IDs overlap, missing or invalid')
    require(0<p['calibration_count']<=len(p['reservoir']) and p['selection'] and p['evaluation'],'Invalid split')
    return p


def checked_run(directory):
    d=Path(directory);receipt=load(d/'complete.json')
    for name,value in receipt.items():
        require(Path(name).name==name and sha(d/name)==value,'Run artifact changed: '+name)
    m=load(d/'manifest.json');s=load(d/'summary.json')
    require(m['status']=='completed','Incomplete run')
    with (d/'queries.csv').open() as f:rows=list(csv.DictReader(f))
    keys={(int(r['repeat']),int(r['query_id'])) for r in rows}
    expected={(repeat,q) for repeat in range(m['repeats']) for q in m['query_ids']}
    require(len(rows)==len(keys) and keys==expected,'Timed query IDs/repeats differ')
    elapsed=sum(float(r['seconds']) for r in rows)
    require(math.isfinite(elapsed) and elapsed>0,'Invalid timing')
    recall=sum(float(r['recall']) for r in rows)/len(rows)
    require(all(0<=float(r['recall'])<=1 and math.isfinite(float(r['seconds'])) and float(r['seconds'])>0 for r in rows),'Invalid query record')
    require(math.isclose(s['qps'],len(rows)/elapsed,rel_tol=1e-12) and math.isclose(s['recall'],recall,rel_tol=1e-12),'Summary differs from raw rows')
    return m,s


def run(args):
    require(0<=args.index_seed<2**64 and 0<=args.calibration_seed<2**64,'Seeds outside uint64 range')
    p=checked_plan(args.plan,args.queries)
    config=load(args.config)
    require(set(config)=={'ell','delta_h','kmax','k','threads','iterations'},'Unsupported/missing configuration key')
    require(all(type(config[k]) is int and config[k]>0 for k in ('ell','kmax','k','threads','iterations')),'Configuration requires positive integers')
    require(type(config['delta_h']) in (int,float) and math.isfinite(config['delta_h']) and 0<config['delta_h']<1 and config['k']<=config['kmax'],'Invalid delta_h or k range')
    datahash=sha(args.data);binaryhash=sha(args.binary);source=code_hashes()
    binding=dict(plan_sha256=sha(args.plan),data_sha256=datahash,queries_sha256=sha(args.queries),
                 binary_sha256=binaryhash,source_sha256=source,config=config,calibration_seed=args.calibration_seed)
    if args.role=='evaluation':
        require(args.lock is not None,'Evaluation requires a validation lock')
        lock=load(args.lock)
        require(lock['selected'] is not None,'Unmatched validation target')
        require(binding==lock['selected']['binding'],'Evaluation configuration/input/source differs from locked validation')
        require(sha(args.lock)==Path(str(args.lock)+'.sha256').read_text().strip(),'Lock changed')
    else:require(args.lock is None,'Selection cannot consume an evaluation lock')
    calibration=random.Random(args.calibration_seed).sample(p['reservoir'],p['calibration_count'])
    query_ids=p[args.role];out=args.out;out.mkdir(parents=True,exist_ok=False)
    for name,values in [('calibration_ids.txt',calibration),('measured_ids.txt',query_ids)]:
        (out/name).write_text(''.join(str(v)+'\n' for v in values),encoding='utf-8')
    m=dict(binding=binding,status='running',role=args.role,index_seed=args.index_seed,repeats=args.repeats,
           query_ids=query_ids,calibration_ids=calibration,lock_sha256=sha(args.lock) if args.lock else None)
    save(out/'manifest.json',m)
    command=[str(args.binary.resolve()),str(args.data.resolve()),str(args.queries.resolve()),str((out/'calibration_ids.txt').resolve()),str((out/'measured_ids.txt').resolve()),str(out.resolve()),str(args.index_seed),str(config['kmax']),str(config['k']),str(args.repeats),str(config['ell']),str(config['delta_h']),str(config['threads']),str(config['iterations'])]
    try:
        with (out/'run.log').open('w') as log:subprocess.run(command,stdout=log,stderr=log,check=True)
        require(sha(args.binary)==binaryhash and code_hashes()==source and sha(args.data)==datahash and sha(args.queries)==binding['queries_sha256'] and sha(args.plan)==binding['plan_sha256'],'Inputs/source changed during run')
        m.update(status='completed',command=command)
        (out/'manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8')
        save(out/'complete.json',{f.name:sha(f) for f in out.iterdir() if f.is_file()})
        checked_run(out)
    except BaseException:
        m['status']='failed';(out/'manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8');raise


def lock_runs(runs,out,target,tolerance):
    require(math.isfinite(target) and math.isfinite(tolerance) and 0<=target<=1 and 0<=tolerance<=1-target,'Invalid recall band')
    groups={};context=None
    for path in runs:
        m,s=checked_run(path);require(m['role']=='selection','Cannot select parameters from evaluation')
        b=m['binding'];current={k:v for k,v in b.items() if k!='config'}
        require(context is None or context==current,'Candidates use different inputs, seeds or code');context=current
        key=json.dumps(b['config'],sort_keys=True)
        group=groups.setdefault(key,dict(binding=b,records=[],index_seeds=[],validation_receipt_sha256=[]))
        require(m['index_seed'] not in group['index_seeds'],'Repeated selection build seed for a configuration')
        group['index_seeds'].append(m['index_seed']);group['records'].append(s);group['validation_receipt_sha256'].append(sha(path/'complete.json'))
    require(groups,'No validation records')
    candidates=[]
    for group in groups.values():
        records=group.pop('records');count=sum(r['queries']*r['repeats'] for r in records)
        group.update(recall=sum(r['recall'] for r in records)/len(records),qps=count/sum(r['queries']*r['repeats']/r['qps'] for r in records))
        candidates.append(group)
    eligible=[c for c in candidates if target<=c['recall']<=target+tolerance]
    eligible.sort(key=lambda c:(-c['qps'],json.dumps(c['binding']['config'],sort_keys=True)))
    value=dict(target=target,tolerance=tolerance,selected=eligible[0] if eligible else None,all_candidates=candidates)
    save(out,value);Path(str(out)+'.sha256').write_text(sha(out)+'\n',encoding='utf-8')
    return value


def main():
    parser=argparse.ArgumentParser();subs=parser.add_subparsers(dest='command',required=True)
    p=subs.add_parser('plan');p.add_argument('--queries',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--split-seed',type=int,default=20261023);p.add_argument('--reservoir',type=int,default=128);p.add_argument('--selection',type=int,default=1000);p.add_argument('--nc',type=int,default=4)
    p=subs.add_parser('run')
    for name in ('binary','data','queries','plan','config','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--role',choices=['selection','evaluation'],required=True);p.add_argument('--lock',type=Path)
    p.add_argument('--calibration-seed',type=int,default=20261007);p.add_argument('--index-seed',type=int,default=20261017);p.add_argument('--repeats',type=int,default=3)
    p=subs.add_parser('lock');p.add_argument('--runs',type=Path,nargs='+',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--target',type=float,required=True);p.add_argument('--tolerance',type=float,default=.002)
    p=subs.add_parser('report');p.add_argument('--lock',type=Path,required=True);p.add_argument('--runs',type=Path,nargs='+',required=True)
    a=parser.parse_args()
    if a.command=='plan':plan(a.queries,a.out,a.split_seed,a.reservoir,a.selection,a.nc)
    elif a.command=='run':require(a.repeats>0,'Invalid repeats');run(a)
    elif a.command=='lock':print(json.dumps(lock_runs(a.runs,a.out,a.target,a.tolerance),indent=2))
    else:
        lock=load(a.lock);require(sha(a.lock)==Path(str(a.lock)+'.sha256').read_text().strip(),'Lock changed');rows=[]
        require(lock['selected'] is not None,'Unmatched validation target')
        seeds=set()
        for path in a.runs:
            m,s=checked_run(path);require(m['role']=='evaluation' and m['binding']==lock['selected']['binding'] and m['lock_sha256']==sha(a.lock),'Unrelated evaluation run')
            require(m['index_seed'] not in seeds,'Duplicate independent-build seed');seeds.add(m['index_seed']);rows.append(s)
        recall=sum(r['recall'] for r in rows)/len(rows)
        print(json.dumps(dict(status='matched' if lock['target']<=recall<=lock['target']+lock['tolerance'] else 'unmatched',independent_builds=len(rows),minimum_three_builds_met=len(rows)>=3,mean_recall=recall,builds=rows),indent=2))


if __name__=='__main__':main()
