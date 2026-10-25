import argparse
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile

from oracle import audit
from space_inventory import audit_sosia_inventory
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tests'))
from smoke_test import write_csr


def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--reference-binary',type=Path);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='sosia-parity-') as temporary:
        root=Path(temporary);rng=random.Random(1031)
        row=lambda:{j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),16))}
        write_csr(root/'base.csr',[row() for _ in range(160)],32)
        write_csr(root/'queries.csr',[row() for _ in range(12)],32)
        selected=[9,2,11,0];(root/'ids.txt').write_text(''.join(str(q)+'\n' for q in selected))
        out=root/'run';out.mkdir()
        command=[str(a.binary.resolve()),str(root/'base.csr'),str(root/'queries.csr'),str(root/'ids.txt'),str(out),'20261007','4','16','160','50','3']
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        (out/'stdout.log').write_text(result.stdout);(out/'stderr.log').write_text(result.stderr)
        proof=audit(root/'base.csr',root/'queries.csr',out/'retrieved.csv',out/'queries.csv',selected,50,3)
        proof['space_inventory']=audit_sosia_inventory(out,root/'base.csr',4,16)
        if a.reference_binary:
            reference=root/'reference';reference.mkdir();reference_command=command.copy();reference_command[0]=str(a.reference_binary.resolve());reference_command[4]=str(reference)
            subprocess.run(reference_command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
            if (reference/'retrieved.csv').read_bytes()!=(out/'retrieved.csv').read_bytes():raise ValueError('Space introspection changed ranked outputs from previous adapter')
            proof['previous_adapter_ranked_outputs_equal']=True
        again=root/'again';again.mkdir();command[4]=str(again)
        subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
        if (again/'retrieved.csv').read_bytes()!=(out/'retrieved.csv').read_bytes():raise ValueError('Same seed is nondeterministic')
        proof['same_seed_ranked_outputs_equal']=True;proof['scope']='SOSIA fixed-source small-fixture correctness only; no performance/IS/paper-parity acceptance'
        bad=root/'invalid_ids.txt';bad.write_text('12\n');badout=root/'bad';badout.mkdir();command[3]=str(bad);command[4]=str(badout)
        if subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode==0:raise ValueError('Out-of-range query ID was accepted')
        overflow=root/'overflow';overflow.mkdir();overflow_command=command.copy();overflow_command[3]=str(root/'ids.txt');overflow_command[4]=str(overflow);overflow_command[6]=str(2**31-1)
        if subprocess.run(overflow_command,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode==0:raise ValueError('Overflowing SOSIA expansion domain accepted')
        zero=root/'zero';zero.mkdir();write_csr(zero/'base.csr',[{0:1e-30} for _ in range(159)]+[{0:1}],3);write_csr(zero/'queries.csr',[{0:1}],3);(zero/'ids.txt').write_text('0\n');(zero/'run').mkdir();zero_command=command.copy();zero_command[1:5]=[str(zero/'base.csr'),str(zero/'queries.csr'),str(zero/'ids.txt'),str(zero/'run')];zero_command[-2]='1'
        subprocess.run(zero_command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True);zero_proof=audit(zero/'base.csr',zero/'queries.csr',zero/'run/retrieved.csv',zero/'run/queries.csv',[0],1,3)
        if zero_proof['Recall']!=1:raise ValueError('Partial empty expanded sets changed exact top-1')
        audit_sosia_inventory(zero/'run',zero/'base.csr',4,16)
        zero_space=json.loads((zero/'run/index_space.json').read_text())
        if zero_space['counts']['expanded_empty_rows']!=159 or zero_space['counts']['expanded_slots_size']!=4:raise ValueError('Partial empty expanded-row inventory differs')
        zero_query=root/'zero-query';zero_query.mkdir();write_csr(zero_query/'queries.csr',[{0:0,2:0}],3);zero_query_command=zero_command.copy();zero_query_command[2]=str(zero_query/'queries.csr');zero_query_command[4]=str(zero_query)
        rejected=subprocess.run(zero_query_command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if rejected.returncode==0 or 'does not support a zero query' not in rejected.stderr:raise ValueError('Unsupported zero query not safely rejected')
        ties=root/'ties';ties.mkdir();write_csr(ties/'base.csr',[{0:1,2:1} for _ in range(160)],3);write_csr(ties/'queries.csr',[{0:1,2:1}],3);(ties/'ids.txt').write_text('0\n');(ties/'run').mkdir();tie_command=command.copy();tie_command[1:5]=[str(ties/'base.csr'),str(ties/'queries.csr'),str(ties/'ids.txt'),str(ties/'run')]
        subprocess.run(tie_command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True);tie_proof=audit(ties/'base.csr',ties/'queries.csr',ties/'run/retrieved.csv',ties/'run/queries.csv',[0],50,3)
        if tie_proof['Recall']!=1:raise ValueError('Positive-query original-ID tie rule failed')
        audit_sosia_inventory(ties/'run',ties/'base.csr',4,16);tie_space=json.loads((ties/'run/index_space.json').read_text())
        if tie_space['counts']['bucket_slots_capacity']<=tie_space['counts']['bucket_slots_size']:raise ValueError('Fixture did not exercise unused bucket capacity')
        original=(out/'index_space.json').read_bytes();changed=json.loads(original);changed['components_bytes']['data_hash_slots']+=4;(out/'index_space.json').write_text(json.dumps(changed))
        try:
            try:audit_sosia_inventory(out,root/'base.csr',4,16)
            except ValueError:pass
            else:raise ValueError('Corrupt component inventory accepted')
        finally:(out/'index_space.json').write_bytes(original)
        proof['positive_query_ties_and_unused_capacity_checked']=True;proof['partial_empty_expansion_space_checked']=True;proof['zero_query_rejected_without_fallback']=True;proof['corrupt_space_inventory_rejected']=True;proof['overflow_domain_rejected']=True
        if a.out:
            artifacts=a.out.parent/'fixture_artifacts';artifacts.mkdir(exist_ok=False)
            for name in ('base.csr','queries.csr','ids.txt'):shutil.copyfile(root/name,artifacts/name)
            shutil.copytree(out,artifacts/'run')
            shutil.copytree(zero,artifacts/'zero_expansion')
            shutil.copytree(ties,artifacts/'positive_ties')
            with a.out.open('x') as f:json.dump(proof,f,indent=2)
        print(json.dumps({k:v for k,v in proof.items() if k not in ('input_sha256','QPS')},indent=2))


if __name__=='__main__':main()
