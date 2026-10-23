import argparse
import csv
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
from types import SimpleNamespace

from protocol import plan,run,load,save,lock_runs,checked_run,checked_plan

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from smoke_test import write_csr


def reject(fn):
    try:fn()
    except (ValueError,FileExistsError):return
    raise AssertionError('Expected protocol rejection')


def check(binary):
    with tempfile.TemporaryDirectory(prefix='cabs-experiment-') as temp:
        d=Path(temp);rng=random.Random(531)
        row=lambda:{j:rng.uniform(.1,1) for j in sorted(rng.sample(range(12),4))}
        docs=write_csr(d/'data.csr',[row() for _ in range(40)],12)
        queries=write_csr(d/'query.csr',[row() for _ in range(24)],12)
        plan(d/'query.csr',d/'plan.json',21,6,8,2)
        p=checked_plan(d/'plan.json',d/'query.csr');assert len(p['evaluation'])==10
        config=dict(ell=4,delta_h=.2,kmax=5,k=5,threads=1,iterations=2);save(d/'config.json',config)
        def args(name,role='selection',index=31,cal=41,lock=None):return SimpleNamespace(binary=binary,data=d/'data.csr',queries=d/'query.csr',plan=d/'plan.json',config=d/'config.json',out=d/name,role=role,index_seed=index,calibration_seed=cal,repeats=2,lock=lock)
        reject(lambda:run(args('no-lock',role='evaluation')))
        run(args('validation'));run(args('other-index',index=32))
        m,s=checked_run(d/'validation');m2,_=checked_run(d/'other-index')
        assert m['calibration_ids']==m2['calibration_ids'] and m['query_ids']==p['selection']
        run(args('other-cal',cal=42));m3,_=checked_run(d/'other-cal');assert m3['calibration_ids']!=m['calibration_ids'] and m3['index_seed']==m['index_seed']
        lock_runs([d/'validation',d/'other-index'],d/'lock.json',0,1)
        run(args('final',role='evaluation',index=51,lock=d/'lock.json'))
        mf,_=checked_run(d/'final');assert mf['query_ids']==p['evaluation'] and mf['calibration_ids']==m['calibration_ids']
        assert set(mf['query_ids']).isdisjoint(m['query_ids']+m['calibration_ids'])
        reject(lambda:lock_runs([d/'final'],d/'bad-lock.json',0,1))
        reject(lambda:lock_runs([d/'validation',d/'other-cal'],d/'mixed-lock.json',0,1))
        lock_runs([d/'validation'],d/'unmatched.json',1 if s['recall']<1 else 0,0)
        reject(lambda:run(args('unmatched-final',role='evaluation',lock=d/'unmatched.json')))
        report=[sys.executable,str(Path(__file__).parent/'protocol.py'),'report','--lock',str(d/'lock.json'),'--runs',str(d/'final')]
        summary=json.loads(subprocess.check_output(report));assert summary['status']=='matched' and not summary['minimum_three_builds_met']
        assert subprocess.run(report+[str(d/'final')],stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode!=0
        config['ell']=8;(d/'config.json').write_text(json.dumps(config));reject(lambda:run(args('changed',role='evaluation',lock=d/'lock.json')))
        for name in ('validation','other-index','other-cal','final'):
            with (d/name/'ground_truth.csv').open() as f:truth=list(csv.DictReader(f))
            grouped={}
            for r in truth:grouped.setdefault(int(r['query_id']),[]).append((int(r['identifier']),float(r['score'])))
            for qid,hits in grouped.items():
                expected=sorted(((i,sum(v*queries[qid].get(j,0) for j,v in x.items())) for i,x in enumerate(docs)),key=lambda h:(-h[1],h[0]))[:5]
                assert hits==expected
            with (d/name/'retrieved.csv').open() as f:
                for r in csv.DictReader(f):
                    q=queries[int(r['query_id'])];x=docs[int(r['identifier'])];assert float(r['score'])==sum(v*q.get(j,0) for j,v in x.items())
        raw=d/'validation/queries.csv';raw.write_bytes(raw.read_bytes()+b'changed');reject(lambda:checked_run(d/'validation'))
        malformed=load(d/'plan.json');malformed['selection'][0]=malformed['evaluation'][0];save(d/'bad-plan.json',malformed);reject(lambda:checked_plan(d/'bad-plan.json',d/'query.csr'))
        reject(lambda:lock_runs([],d/'empty.json',0,1))
        print(json.dumps(dict(status='PASS',independent_python_GT=True,original_query_ids=True,seed_isolation=True,evaluation_lock_enforced=True,heldout_selection=True,modified_artifact_rejected=True)))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);check(p.parse_args().binary.resolve())
