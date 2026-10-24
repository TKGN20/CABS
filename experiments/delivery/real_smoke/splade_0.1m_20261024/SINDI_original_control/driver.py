import json
import os
from pathlib import Path
import resource
import subprocess
import sys
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'experiments/baselines'))
sys.path.insert(0,str(ROOT/'experiments'))
from audit_run import audit_run
from protocol import save,sha
smoke=ROOT/'build-mission/local-real-smoke-20261024'
if json.loads((smoke/'manifest.json').read_text())['status']=='running':raise ValueError('Run only after all four methods finish')
out=ROOT/'build-mission/sindi-original-real-control-20261024';out.mkdir(exist_ok=False)
binary=ROOT/'build-mission/sindi-unpatched-adapter-recorded-v4/sindi_fixed'
data=ROOT/'datasets/SPLADE/base_small.csr';queries=ROOT/'datasets/SPLADE/queries.dev.csr'
command=[str(binary),str(data),str(queries),str(smoke/'ids.txt'),str(out),'10000','1','1','1000','50','3','candidate-api-v1']
state=dict(scope='Original pinned SINDI native-control diagnostic on same four selection queries and fixed configuration; diagnostic allocator; no performance acceptance',command=command,binary_sha256=sha(binary),library_sha256=sha(ROOT/'build-mission/sindi-pinned-v2/build/src/libvsag.so'),driver_sha256=sha(Path(__file__)),parent_smoke_manifest_sha256=sha(smoke/'manifest.json'),status='running')
def limits():resource.setrlimit(resource.RLIMIT_AS,(8*1024**3,8*1024**3))
try:
    with (out/'run.log').open('w') as log:subprocess.run(['/usr/bin/time','-v','-o',str(out/'resource_usage.log'),*command],env=dict(os.environ,OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1'),stdout=log,stderr=log,timeout=600,preexec_fn=limits,check=True)
    audit=audit_run(data,queries,smoke/'plan.json',smoke/'selection-gt',out,'SINDI',3);save(out/'independent_audit.json',audit);state.update(status='SMOKE_AUDITED',Recall=audit['Recall'])
except BaseException as error:state.update(status='FAILED_DIAGNOSTIC',error=str(error))
finally:save(out/'receipt.json',state)
print(json.dumps(state,indent=2))
