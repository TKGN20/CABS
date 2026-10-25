import json
import os
from pathlib import Path
import resource
import subprocess
import sys
from types import SimpleNamespace

root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root/'experiments/baselines'));sys.path.insert(0,str(root/'experiments'))
from protocol import save,sha,code_hashes
from prepare_gt import generate
from audit_run import audit_run
from space_inventory import audit_cabs_inventory

out=root/'build-mission/cabs-space-real-0.1m-20261024';out.mkdir(exist_ok=False)
prior=root/'build-mission/local-real-smoke-20261024'
data=root/'datasets/SPLADE/base_small.csr';queries=root/'datasets/SPLADE/queries.dev.csr'
binary=root/'build-experiment/cabs_fixed_queries';gt=out/'selection-gt'
generate(SimpleNamespace(data=data,queries=queries,plan=prior/'plan.json',binary=root/'build-experiment/independent_exact_gt',out=gt,role='selection',k=50,calibration_seed=20261007,lock=None))
assert (gt/'ground_truth.csv').read_bytes()==(prior/'selection-gt/ground_truth.csv').read_bytes()
command=[str(binary),str(data),str(queries),str(prior/'calibration_ids.txt'),str(prior/'ids.txt'),str(out),'20261017','100','50','3','24','0.2','1','10']
state=dict(status='running',method='CABS',command=command,binary_sha256=sha(binary),source_sha256=code_hashes(),driver_sha256=sha(Path(__file__)),scope='One fixed local component/lifecycle control; no matched-recall or server-performance acceptance')
(out/'receipt.json').write_text(json.dumps(state,indent=2))
def limits():resource.setrlimit(resource.RLIMIT_AS,(8*1024**3,8*1024**3))
try:
    with (out/'run.log').open('w') as log:subprocess.run(['/usr/bin/time','-v','-o',str(out/'resource_usage.log'),*command],env=dict(os.environ,OMP_NUM_THREADS='1'),stdout=log,stderr=log,check=True,timeout=600,preexec_fn=limits)
    assert sha(binary)==state['binary_sha256'] and code_hashes()==state['source_sha256']
    score=audit_run(data,queries,prior/'plan.json',gt,out,'CABS',3);save(out/'independent_audit.json',score)
    inventory=audit_cabs_inventory(out,data);save(out/'space_audit.json',inventory)
    summary=json.loads((out/'summary.json').read_text())
    state.update(status='LOCAL_REAL_SPACE_INVENTORY_CHECKED',Recall=score['Recall'],K=summary['K'],B=summary['B'],inventory=inventory,prior_ranked_outputs_equal=(out/'retrieved.csv').read_bytes()==(prior/'CABS/retrieved.csv').read_bytes(),GT_matches_previous=True)
except BaseException as error:state.update(status='FAILED_DIAGNOSTIC',error=str(error));raise
finally:(out/'receipt.json').write_text(json.dumps(state,indent=2))
print(json.dumps(state,indent=2))
