import json
import os
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root/'experiments/baselines'));sys.path.insert(0,str(root/'experiments'))
from audit_run import audit_run
from space_inventory import audit_sosia_inventory
from protocol import save,sha
out=root/'build-mission/sosia-space-real-0.1m-20261024';out.mkdir(exist_ok=False)
binary=root/'build-mission/sosia-space-release-v2/sosia_fixed';prior=root/'build-mission/local-real-smoke-20261024'
data=root/'datasets/SPLADE/base_small.csr';queries=root/'datasets/SPLADE/queries.dev.csr'
command=[str(binary),str(data),str(queries),str(prior/'ids.txt'),str(out),'20261017','4','16','1000','50','3']
with (out/'run.log').open('w') as log:subprocess.run(['/usr/bin/time','-v','-o',str(out/'resource_usage.log'),*command],stdout=log,stderr=log,env=dict(os.environ,OMP_NUM_THREADS='1'),check=True,timeout=600)
scores=audit_run(data,queries,prior/'plan.json',prior/'selection-gt',out,'SOSIA',3);save(out/'independent_audit.json',scores)
inventory=audit_sosia_inventory(out,data,4,16);save(out/'space_audit.json',inventory)
assert (out/'retrieved.csv').read_bytes()==(prior/'SOSIA/retrieved.csv').read_bytes()
save(out/'receipt.json',dict(status='LOCAL_REAL_SPACE_INVENTORY_CHECKED',command=command,binary_sha256=sha(binary),build_receipt_sha256=sha(root/'build-mission/sosia-space-release-v2/build_receipt.json'),driver_sha256=sha(Path(__file__)),Recall=scores['Recall'],inventory=inventory,prior_ranked_outputs_equal=True,scope='Single local SOSIA component inventory; not common IS, native default or fair performance acceptance'))
print(json.dumps(inventory,indent=2))
