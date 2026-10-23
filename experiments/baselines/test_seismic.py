import argparse
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile

from oracle import audit
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tests'))
from smoke_test import write_csr


def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--out',type=Path);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='seismic-parity-') as temporary:
        root=Path(temporary);rng=random.Random(1031)
        row=lambda:{j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),16))}
        write_csr(root/'base.csr',[row() for _ in range(160)],32)
        write_csr(root/'queries.csr',[row() for _ in range(12)],32)
        selected=[9,2,11,0];(root/'ids.txt').write_text(''.join(str(q)+'\n' for q in selected))
        out=root/'run';out.mkdir()
        env=dict(os.environ,RAYON_NUM_THREADS='1',CABS_BENCH_INDEX_SEED='20261007')
        command=[str(a.binary.resolve()),str(root/'base.csr'),str(root/'queries.csr'),str(root/'ids.txt'),str(out),'20261007','50','3','160','0.2','1','32','0.01']
        result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        (out/'stdout.log').write_text(result.stdout);(out/'stderr.log').write_text(result.stderr)
        proof=audit(root/'base.csr',root/'queries.csr',out/'retrieved.csv',out/'queries.csv',selected,50,3)
        again=root/'again';again.mkdir();command[4]=str(again)
        subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
        if (again/'retrieved.csv').read_bytes()!=(out/'retrieved.csv').read_bytes():raise ValueError('Same seed is nondeterministic')
        proof.update(same_seed_ranked_outputs_equal=True,native_candidate_rankings_checked=True,scope='SEISMIC fixed-source small-fixture correctness only; no performance/IS/paper-parity acceptance')
        bad=root/'invalid_ids.txt';bad.write_text('12\n');badout=root/'bad';badout.mkdir();command[3]=str(bad);command[4]=str(badout)
        if subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode==0:raise ValueError('Out-of-range query ID was accepted')
        command[3]=str(root/'ids.txt');env['CABS_BENCH_INDEX_SEED']='17'
        if subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode==0:raise ValueError('Mismatched index seed was accepted')
        if a.out:
            artifacts=a.out.parent/'fixture_artifacts';artifacts.mkdir(exist_ok=False)
            for name in ('base.csr','queries.csr','ids.txt'):shutil.copyfile(root/name,artifacts/name)
            shutil.copytree(out,artifacts/'run')
            with a.out.open('x') as f:json.dump(proof,f,indent=2)
        print(json.dumps({k:v for k,v in proof.items() if k not in ('input_sha256','QPS')},indent=2))


if __name__=='__main__':main()
