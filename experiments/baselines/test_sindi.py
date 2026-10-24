import argparse
import csv
import json
import math
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile

from oracle import audit,CSR,score
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tests'))
from smoke_test import write_csr


def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--out',type=Path);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='sindi-parity-') as temporary:
        root=Path(temporary);rng=random.Random(1031)
        row=lambda:{j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),16))}
        write_csr(root/'base.csr',[row() for _ in range(160)],32);write_csr(root/'queries.csr',[row() for _ in range(12)],32)
        selected=[9,2,11,0];(root/'ids.txt').write_text(''.join(str(q)+'\n' for q in selected));out=root/'run';out.mkdir()
        env=dict(os.environ,OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1');env.pop('CABS_SINDI_SENTINEL_BYTE',None)
        command=[str(a.binary.resolve()),str(root/'base.csr'),str(root/'queries.csr'),str(root/'ids.txt'),str(out),'10000','1','1','160','50','3','candidate-api-v1']
        result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        (out/'stdout.log').write_text(result.stdout);(out/'stderr.log').write_text(result.stderr)
        proof=audit(root/'base.csr',root/'queries.csr',out/'retrieved.csv',out/'queries.csv',selected,50,3)
        docs=CSR(root/'base.csr');queries=CSR(root/'queries.csr')
        try:
            candidates={qid:[] for qid in selected}
            with (out/'native_candidates.csv').open() as f:
                for hit in csv.DictReader(f):
                    qid=int(hit['query_id']);did=int(hit['identifier']);value=float(hit['native_float_score']);exact=score(docs.row(did),dict(queries.row(qid)))
                    if not math.isclose(value,exact,rel_tol=2e-6,abs_tol=2e-6):raise ValueError('Native float score differs from original vectors')
                    candidates[qid].append(did)
            with (out/'retrieved.csv').open() as f:ranked=list(csv.DictReader(f))
            for qid in selected:
                expected=sorted(((did,score(docs.row(did),dict(queries.row(qid)))) for did in set(candidates[qid])),key=lambda h:(-h[1],h[0]))[:50]
                actual=[(int(h['identifier']),float(h['score'])) for h in ranked if int(h['query_id'])==qid]
                if actual!=expected:raise ValueError('Exact top-k does not match complete exposed gamma pool')
            with (out/'candidate_counts.csv').open() as f:
                counts=list(csv.DictReader(f))
            for count in counts:
                if int(count['exposed_slots'])+int(count['unwritten_slots'])!=160 or int(count['exposed_slots'])-int(count['unique_candidates'])!=int(count['duplicate_slots']):raise ValueError('Invalid candidate count accounting')
                if int(count['unique_candidates'])!=160 or int(count['unwritten_slots'])!=0 or int(count['duplicate_slots'])!=0:raise ValueError('Unpruned full-budget SINDI did not expose all positive candidates')
            if proof['Recall']!=1:raise ValueError('Unpruned full-budget SINDI differs from exact GT')
        finally:docs.close();queries.close()
        for tag,pattern in [('again','255'),('other-pattern','170')]:
            again=root/tag;again.mkdir();command[4]=str(again);env['CABS_SINDI_SENTINEL_BYTE']=pattern
            subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
            for name in ('retrieved.csv','native_candidates.csv','native_reference.csv','candidate_counts.csv'):
                if (again/name).read_bytes()!=(out/name).read_bytes():raise ValueError('Native results depend on repeat or unwritten-memory pattern: '+name)
        proof.update(same_build_ranked_outputs_equal=True,two_sentinel_patterns_equal=True,complete_gamma_pool_reranking_checked=True,candidate_counts=counts,scope='SINDI public-API small-fixture correctness only; no performance/IS/paper-parity acceptance')
        ties=root/'ties';ties.mkdir();write_csr(ties/'base.csr',[{0:1,2:1} for _ in range(160)],3);write_csr(ties/'queries.csr',[{0:1,2:1}],3);(ties/'ids.txt').write_text('0\n');(ties/'run').mkdir();tie_command=command.copy();tie_command[1:5]=[str(ties/'base.csr'),str(ties/'queries.csr'),str(ties/'ids.txt'),str(ties/'run')]
        subprocess.run(tie_command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True);tie_proof=audit(ties/'base.csr',ties/'queries.csr',ties/'run/retrieved.csv',ties/'run/queries.csv',[0],50,3)
        if tie_proof['Recall']!=1:raise ValueError('Common original-ID tie rule failed')
        proof['original_id_tie_rule_checked']=True
        no_reorder=root/'no-reorder';no_reorder.mkdir();no_reorder_command=command.copy();no_reorder_command[4]=str(no_reorder);no_reorder_command[-1]='candidate-no-reorder-diagnostic-v1'
        subprocess.run(no_reorder_command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True);no_reorder_proof=audit(root/'base.csr',root/'queries.csr',no_reorder/'retrieved.csv',no_reorder/'queries.csv',selected,50,3)
        if no_reorder_proof['Recall']!=1:raise ValueError('Non-reranking branch gamma>k/padded-window fixture differs from GT')
        proof['non_reranking_gamma_gt_k_and_padded_window_checked']=True
        bad=root/'invalid_ids.txt';bad.write_text('12\n');badout=root/'bad';badout.mkdir();command[3]=str(bad);command[4]=str(badout)
        if subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode==0:raise ValueError('Out-of-range original query ID accepted')
        write_csr(root/'disjoint-base.csr',[{0:1,2:1} for _ in range(160)],3);write_csr(root/'disjoint-query.csr',[{1:1}],3);(root/'one-id.txt').write_text('0\n');under=root/'underfilled';under.mkdir();command[1:5]=[str(root/'disjoint-base.csr'),str(root/'disjoint-query.csr'),str(root/'one-id.txt'),str(under)]
        result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if result.returncode==0 or 'underfilled unique candidate set' not in result.stderr:raise ValueError('Underfilled native candidates were not explicitly rejected: '+result.stderr)
        proof['underfill_rejected_without_exact_fallback']=True
        write_csr(root/'trailing-base.csr',[{0:1,2:1} for _ in range(160)],4);write_csr(root/'trailing-query.csr',[{3:1}],4);trailing=root/'trailing';trailing.mkdir();command[1:5]=[str(root/'trailing-base.csr'),str(root/'trailing-query.csr'),str(root/'one-id.txt'),str(trailing)]
        result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if result.returncode==0 or 'underfilled unique candidate set' not in result.stderr:raise ValueError('Unseen declared trailing coordinate was not safely handled: '+result.stderr)
        proof['declared_trailing_dimension_checked']=True
        if a.out:
            artifacts=a.out.parent/'fixture_artifacts';artifacts.mkdir(exist_ok=False)
            for name in ('base.csr','queries.csr','ids.txt'):shutil.copyfile(root/name,artifacts/name)
            shutil.copytree(out,artifacts/'run')
            shutil.copytree(ties,a.out.parent/'tie_fixture_artifacts')
            no_reorder_artifacts=a.out.parent/'no_reorder_fixture_artifacts';no_reorder_artifacts.mkdir(exist_ok=False)
            for name in ('base.csr','queries.csr','ids.txt'):shutil.copyfile(root/name,no_reorder_artifacts/name)
            shutil.copytree(no_reorder,no_reorder_artifacts/'run')
            with a.out.open('x') as f:json.dump(proof,f,indent=2)
        print(json.dumps({k:v for k,v in proof.items() if k not in ('input_sha256','QPS','candidate_counts')},indent=2))


if __name__=='__main__':main()
