import json
import os
from pathlib import Path
import platform
import random
import resource
import subprocess
import sys
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'experiments/baselines'))
sys.path.insert(0,str(ROOT/'experiments'))
sys.path.insert(0,str(ROOT/'scripts'))
from protocol import plan,checked_plan,save,sha
from prepare_gt import generate
from audit_run import audit_run
from verify_artifacts import check_approval_baseline

out=ROOT/'build-mission/local-real-smoke-20261024'
out.mkdir(exist_ok=False)
data=ROOT/'datasets/SPLADE/base_small.csr';queries=ROOT/'datasets/SPLADE/queries.dev.csr'
query_plan=out/'plan.json';plan(queries,query_plan,20261024,128,4,4);split=checked_plan(query_plan,queries)
calibration=random.Random(20261007).sample(split['reservoir'],split['calibration_count'])
(out/'ids.txt').write_text(''.join(str(q)+'\n' for q in split['selection']))
(out/'calibration_ids.txt').write_text(''.join(str(q)+'\n' for q in calibration))
generate(SimpleNamespace(data=data,queries=queries,plan=query_plan,binary=ROOT/'build-experiment/independent_exact_gt',out=out/'selection-gt',role='selection',k=50,calibration_seed=20261007,lock=None))
binding=dict(data_sha256=sha(data),queries_sha256=sha(queries),plan_sha256=sha(query_plan),script_sha256=sha(Path(__file__)))
manifest=dict(status='running',scope='Local real-data single-configuration smoke only; four selection queries; no parameter selection, matched-recall, IS or server-performance acceptance',host=platform.platform(),commit=subprocess.check_output(['git','-c','safe.directory='+str(ROOT),'rev-parse','HEAD'],cwd=ROOT,text=True).strip(),binding=binding,freeze=check_approval_baseline(source_check=True),methods={},selection_query_ids=split['selection'],calibration_query_ids=calibration,resource_policy=dict(timeout_seconds_per_method=600,address_space_limit_bytes=8*1024**3,search_threads=1,repeats=3))
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
env=dict(os.environ,OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',RAYON_NUM_THREADS='1',CABS_BENCH_INDEX_SEED='20261017')
def limits():resource.setrlimit(resource.RLIMIT_AS,(8*1024**3,8*1024**3))
for method,binary,receipt,parameters in [
    ('CABS','build-experiment/cabs_fixed_queries',None,['20261017','100','50','3','24','0.2','1','10']),
    ('SOSIA','build-mission/sosia-release-v3/sosia_fixed','build-mission/sosia-release-v3/build_receipt.json',['20261017','4','16','1000','50','3']),
    ('SINDI','build-mission/sindi-repaired-adapter-v2/sindi_fixed','build-mission/sindi-repaired-adapter-v2/build_receipt.json',['10000','1','1','1000','50','3','candidate-api-v1']),
    ('SEISMIC','build-mission/seismic-adapter-v1/target/release/cabs_fixed_queries','build-mission/seismic-adapter-v1/build_receipt.json',['20261017','50','3','1000','0.1','1','20','0.7']),
]:
    folder=out/method;folder.mkdir();executable=ROOT/binary
    prefix=[str(executable),str(data),str(queries)]
    if method=='CABS':prefix.append(str(out/'calibration_ids.txt'))
    command=prefix+[str(out/'ids.txt'),str(folder)]+parameters
    state=dict(status='running',command=command,binary_sha256=sha(executable),build_receipt_sha256=sha(ROOT/receipt) if receipt else None)
    if method=='SINDI':state['native_library_sha256']=sha(ROOT/'build-mission/sindi-repaired-native-v1/build/src/libvsag.so')
    manifest['methods'][method]=state;(out/'manifest.json').write_text(json.dumps(manifest,indent=2));print(method+' START',flush=True)
    try:
        with (folder/'run.log').open('w') as log:subprocess.run(['/usr/bin/time','-v','-o',str(folder/'resource_usage.log'),*command],env=env,stdout=log,stderr=log,timeout=600,preexec_fn=limits,check=True)
        result=audit_run(data,queries,query_plan,out/'selection-gt',folder,method,3);save(folder/'independent_audit.json',result)
        if sha(executable)!=state['binary_sha256'] or (receipt and sha(ROOT/receipt)!=state['build_receipt_sha256']):raise ValueError('Binary or receipt changed during smoke')
        if method=='SINDI' and sha(ROOT/'build-mission/sindi-repaired-native-v1/build/src/libvsag.so')!=state['native_library_sha256']:raise ValueError('Native SINDI library changed during smoke')
        state.update(status='SMOKE_AUDITED',Recall=result['Recall'],audit_sha256=sha(folder/'independent_audit.json'))
    except BaseException as error:state.update(status='FAILED',error=str(error))
    print(method+' '+json.dumps(state),flush=True);(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
if binding!=dict(data_sha256=sha(data),queries_sha256=sha(queries),plan_sha256=sha(query_plan),script_sha256=sha(Path(__file__))):raise ValueError('Smoke inputs or driver changed')
manifest['status']='completed_with_failures' if any(record['status']=='FAILED' for record in manifest['methods'].values()) else 'completed_smoke_only'
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
save(out/'artifacts.json',{p.relative_to(out).as_posix():sha(p) for p in sorted(out.rglob('*')) if p.is_file()})
print(manifest['status'],flush=True)
