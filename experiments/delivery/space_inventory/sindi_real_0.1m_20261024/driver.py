import argparse
import json
import os
from pathlib import Path
import resource
import subprocess
import sys

root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root/'experiments/baselines'))
sys.path.insert(0,str(root/'experiments'))
from audit_run import audit_run
from space_inventory import audit_sindi_inventory,audit_seismic_inventory
from protocol import save,sha

p=argparse.ArgumentParser();p.add_argument('--method',choices=['SINDI','SEISMIC'],required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(exist_ok=False)
prior=root/'build-mission/local-real-smoke-20261024'
data=root/'datasets/SPLADE/base_small.csr';queries=root/'datasets/SPLADE/queries.dev.csr'
environment=dict(os.environ,OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',RAYON_NUM_THREADS='1',CABS_BENCH_INDEX_SEED='20261017')
for name in ('CABS_SINDI_SENTINEL_BYTE','CABS_SINDI_SPACE_ROUNDTRIP','CABS_SEISMIC_SPACE_ROUNDTRIP'):environment.pop(name,None)
if a.method=='SINDI':
    build=root/'build-mission/sindi-space-adapter-v1';binary=build/'sindi_fixed'
    command=[str(binary),str(data),str(queries),str(prior/'ids.txt'),str(out),'10000','1','1','1000','50','3','candidate-api-v1']
else:
    build=root/'build-mission/seismic-space-native-v2';binary=build/'target/release/cabs_fixed_queries'
    command=[str(binary),str(data),str(queries),str(prior/'ids.txt'),str(out),'20261017','50','3','1000','0.1','1','20','0.7']
state=dict(status='running',method=a.method,command=command,binary_sha256=sha(binary),build_receipt_sha256=sha(build/'build_receipt.json'),driver_sha256=sha(Path(__file__)),scope='One fixed local component inventory and unchanged-ranking control; not native default, parameter tuning, common IS or fair server performance')
def limits():resource.setrlimit(resource.RLIMIT_AS,(8*1024**3,8*1024**3))
try:
    with (out/'run.log').open('w') as log:subprocess.run(['/usr/bin/time','-v','-o',str(out/'resource_usage.log'),*command],env=environment,stdout=log,stderr=log,check=True,timeout=600,preexec_fn=limits)
    scores=audit_run(data,queries,prior/'plan.json',prior/'selection-gt',out,a.method,3);save(out/'independent_audit.json',scores)
    inventory=audit_sindi_inventory(out,data,10000,True) if a.method=='SINDI' else audit_seismic_inventory(out,data)
    save(out/'space_audit.json',inventory)
    equal=(out/'retrieved.csv').read_bytes()==(prior/a.method/'retrieved.csv').read_bytes()
    state.update(status='LOCAL_REAL_SPACE_INVENTORY_CHECKED',Recall=scores['Recall'],inventory=inventory,prior_ranked_outputs_equal=equal)
    if not equal:raise ValueError('Ranked results changed; investigate before acceptance')
except BaseException as error:state.update(status='FAILED_DIAGNOSTIC',error=str(error));raise
finally:save(out/'receipt.json',state)
print(json.dumps(state,indent=2))
