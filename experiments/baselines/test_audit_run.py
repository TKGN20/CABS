import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace

from audit_run import audit_run
from prepare_gt import generate
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from protocol import save,sha

ROOT=Path(__file__).resolve().parents[2]


def reject(call):
    try:call()
    except ValueError:return
    raise AssertionError('Corrupt or unrelated benchmark record accepted')


def main():
    p=argparse.ArgumentParser();p.add_argument('--gt-binary',type=Path,required=True);p.add_argument('--cabs-binary',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='common-record-audit-') as temporary:
        root=Path(temporary);fixture=ROOT/'experiments/delivery/fixtures/seismic';data=fixture/'base.csr';queries=fixture/'queries.csr'
        plan=root/'plan.json';selected=[9,2,11,0];reservoir=[3,4,5,6,7,8];evaluation=[1,10]
        save(plan,dict(queries_sha256=sha(queries),query_count=12,split_seed=1031,calibration_count=2,reservoir=reservoir,selection=selected,evaluation=evaluation,scope='Synthetic explicit query-plan fixture'))
        gt=root/'selection-gt';generate(SimpleNamespace(data=data,queries=queries,plan=plan,binary=a.gt_binary,out=gt,role='selection',k=50,calibration_seed=1031,lock=None))
        reports=[]
        for method,folder in [('SOSIA','sosia'),('SEISMIC','seismic'),('SINDI','sindi_repaired')]:
            record=ROOT/'experiments/delivery/fixtures'/folder/'run';result=audit_run(data,queries,plan,gt,record,method,3)
            assert result['measured_records']==12 and result['query_ids']==selected
            reports.append((method,result['Recall']))
        cabs=root/'cabs';cabs.mkdir();(root/'calibration_ids.txt').write_text('3\n4\n');(root/'ids.txt').write_text(''.join(str(q)+'\n' for q in selected))
        command=[str(a.cabs_binary.resolve()),str(data),str(queries),str(root/'calibration_ids.txt'),str(root/'ids.txt'),str(cabs),'1031','100','50','3','24','0.2','1','2']
        subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
        result=audit_run(data,queries,plan,gt,cabs,'CABS',3);reports.append(('CABS',result['Recall']))
        corrupt=root/'corrupt';shutil.copytree(ROOT/'experiments/delivery/fixtures/sindi_repaired/run',corrupt)
        result_file=corrupt/'retrieved.csv'
        with result_file.open() as f:rows=list(csv.DictReader(f))
        rows[0]['score']=str(float(rows[0]['score'])+1)
        with result_file.open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
        reject(lambda:audit_run(data,queries,plan,gt,corrupt,'SINDI',3))
        shutil.copyfile(ROOT/'experiments/delivery/fixtures/sindi_repaired/run/retrieved.csv',result_file)
        (corrupt/'queries.csv').write_bytes((corrupt/'queries.csv').read_bytes().splitlines(keepends=True)[0]+b''.join((corrupt/'queries.csv').read_bytes().splitlines(keepends=True)[2:]))
        reject(lambda:audit_run(data,queries,plan,gt,corrupt,'SINDI',3))
        reject(lambda:audit_run(data,queries,plan,gt,cabs,'CABS',2))
        reject(lambda:audit_run(data,queries,plan,gt,cabs,'SEISMIC',3))
        evaluation_gt=root/'evaluation-gt';binding={name:sha(value) for name,value in [('plan_sha256',plan),('data_sha256',data),('queries_sha256',queries)]};binding.update(config={'k':50},method='SINDI');lock=root/'lock.json'
        save(lock,dict(selected={'binding':binding},scope='Synthetic lock only'));Path(str(lock)+'.sha256').write_text(sha(lock))
        generate(SimpleNamespace(data=data,queries=queries,plan=plan,binary=a.gt_binary,out=evaluation_gt,role='evaluation',k=50,calibration_seed=1031,lock=lock))
        reject(lambda:audit_run(data,queries,plan,evaluation_gt,cabs,'CABS',3,lock))
        reject(lambda:audit_run(data,queries,plan,evaluation_gt,cabs,'SINDI',3,None))
        print(json.dumps(dict(status='PASS',fixtures=reports,checks=['all four record formats with independent exact GT','fresh CABS execution','original IDs and exact scores','missing timing/repeat rejection','wrong method/lock rejection'],scope='Small synthetic record-audit contract only')))


if __name__=='__main__':main()
