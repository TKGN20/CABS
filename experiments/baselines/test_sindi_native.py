import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'experiments/baselines'))
from oracle import CSR,score

p=argparse.ArgumentParser();p.add_argument('--native',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--reference-binary',type=Path);a=p.parse_args()
native=a.native.resolve()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
fixture=root/'experiments/delivery/fixtures/seismic'
docs=CSR(fixture/'base.csr');queries=CSR(fixture/'queries.csr');k=50
truth=[]
for qid in range(queries.n):truth.append(sorted([(i,score(docs.row(i),dict(queries.row(qid)))) for i in range(docs.n)],key=lambda h:(-h[1],h[0]))[:k])
with (out/'gt.bin').open('wb') as f:
    f.write(struct.pack('<II',queries.n,k))
    for hits in truth:f.write(struct.pack('<'+'i'*k,*[i for i,_ in hits]))
    for hits in truth:f.write(struct.pack('<'+'f'*k,*[s for _,s in hits]))
docs.close();queries.close()
crosscheck=dict(status='NOT_RUN')
if a.reference_binary:
    reference=out/'exact_reference';reference.mkdir();(reference/'ids.txt').write_text(''.join(str(q)+'\n' for q in range(len(truth))))
    subprocess.run([str(a.reference_binary.resolve()),str(fixture/'base.csr'),str(fixture/'queries.csr'),str(reference/'ids.txt'),str(k),str(reference)],check=True)
    with (reference/'ground_truth.csv').open() as f:rows=list(csv.DictReader(f))
    expected=[(qid,rank+1,did,value) for qid,hits in enumerate(truth) for rank,(did,value) in enumerate(hits)]
    actual=[(int(r['query_id']),int(r['rank']),int(r['identifier']),float(r['score'])) for r in rows]
    if actual!=expected:raise ValueError('Native GT generator differs from independent C++ reference')
    crosscheck=dict(status='PASS',reference_binary_sha256=hashlib.sha256(a.reference_binary.read_bytes()).hexdigest(),rows=len(rows),scope='Exact GT values/IDs only; native returned IDs still require separate candidate audit')
env=dict(os.environ,OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1')
binroot=native/'build/sparse/scripts'
commands=[['sindi_index_build',str(fixture/'base.csr'),'10000','1','1',str(out/'index.bin')],['sindi_index_search',str(out/'index.bin'),str(fixture/'queries.csr'),str(out/'gt.bin'),'1','160','50','1']]
results=[]
for command in commands:
    command=[str(binroot/command[0])]+command[1:]
    run=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    tag=Path(command[0]).name
    (out/(tag+'.stdout.log')).write_bytes(run.stdout);(out/(tag+'.stderr.log')).write_bytes(run.stderr)
    results.append(dict(command=command,returncode=run.returncode))
    if run.returncode:break
receipt=dict(status='NATIVE_SMOKE_ONLY' if len(results)==2 and all(r['returncode']==0 for r in results) else 'FAILED',scope='Native CLI build/serialize/reload/search smoke on public synthetic fixture; printed QPS/recall not accepted as four-method evidence; no original-ID candidate adapter yet',ground_truth_crosscheck=crosscheck,commands=results,source_build_receipt_sha256=hashlib.sha256((native/'build_receipt.json').read_bytes()).hexdigest(),binary_sha256={name:hashlib.sha256((binroot/name).read_bytes()).hexdigest() for name in ('sindi_index_build','sindi_index_search')},library_sha256=hashlib.sha256((native/'build/src/libvsag.so').read_bytes()).hexdigest(),avx512f_host_flag='avx512f' in Path('/proc/cpuinfo').read_text(),artifact_sha256={p.relative_to(out).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()})
(out/'receipt.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt,indent=2))
if receipt['status']=='FAILED':sys.exit(1)
