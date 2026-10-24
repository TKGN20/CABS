import csv
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import struct


def main():
    root=Path(__file__).resolve().parent/'receipts'
    registry=json.loads((root/'sha256.json').read_text(encoding='utf-8'))
    expected={p.name for p in root.glob('*.json') if p.name!='sha256.json'}
    if set(registry)!=expected:raise ValueError('Missing or unregistered receipt')
    for name,wanted in registry.items():
        if Path(name).name!=name or not re.fullmatch('[0-9a-f]{64}',wanted):raise ValueError('Invalid receipt manifest entry')
        actual=hashlib.sha256((root/name).read_bytes()).hexdigest()
        if actual!=wanted:raise ValueError('Receipt changed: '+name)
    fixtures=json.loads((root/'fixture_artifacts_20261023.json').read_text(encoding='utf-8'))
    repository=root.parents[2];sys.path.insert(0,str(repository/'experiments/baselines'))
    from oracle import audit,CSR,score
    for method,record in fixtures['methods'].items():
        folder=repository/record['path']
        if not folder.resolve().is_relative_to(repository.resolve()):raise ValueError('Unsafe fixture path')
        for name,wanted in record['sha256'].items():
            if not (folder/name).resolve().is_relative_to(folder.resolve()):raise ValueError('Unsafe artifact path')
            if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=wanted:raise ValueError('Fixture changed: '+name)
        actual=audit(folder/'base.csr',folder/'queries.csr',folder/'run/retrieved.csv',folder/'run/queries.csv',[int(v) for v in (folder/'ids.txt').read_text().split()],50,3)
        if actual['Recall']!=record['Recall']:raise ValueError('Fixture recall differs: '+method)
        if 'candidate_pool_expected' in record:
            docs=CSR(folder/'base.csr');queries=CSR(folder/'queries.csr')
            try:
                selected=[int(v) for v in (folder/'ids.txt').read_text().split()]
                with (folder/'run/candidate_counts.csv').open() as f:counts=list(csv.DictReader(f))
                with (folder/'run/native_candidates.csv').open() as f:candidates=list(csv.DictReader(f))
                with (folder/'run/native_reference.csv').open() as f:references=list(csv.DictReader(f))
                if [int(c['query_id']) for c in counts]!=selected:raise ValueError('Candidate count queries differ')
                if set(int(r['query_id']) for r in candidates)!=set(selected):raise ValueError('Candidate queries differ')
                for count in counts:
                    qid=int(count['query_id']);hits=[r for r in candidates if int(r['query_id'])==qid];ids=[int(r['identifier']) for r in hits]
                    if len(ids)!=record['candidate_pool_expected'] or len(set(ids))!=len(ids) or int(count['exposed_slots'])!=len(ids) or int(count['unique_candidates'])!=len(ids) or int(count['unwritten_slots'])!=0 or int(count['duplicate_slots'])!=0:raise ValueError('Complete candidate pool differs')
                    if not set(int(r['identifier']) for r in references if int(r['query_id'])==qid).issubset(ids):raise ValueError('Native top-k absent from candidate pool')
                    for hit in hits:
                        did=int(hit['identifier'])
                        if did<0 or did>=docs.n or not math.isclose(float(hit['native_float_score']),score(docs.row(did),dict(queries.row(qid))),rel_tol=2e-6,abs_tol=2e-6):raise ValueError('Native score differs from original input')
            finally:docs.close();queries.close()
    invalid_gt=0;valid_gt=0
    for record in fixtures.get('native_smokes',{}).values():
        folder=repository/record['path']
        if not folder.resolve().is_relative_to(repository.resolve()):raise ValueError('Unsafe native artifact path')
        for name,wanted in record['sha256'].items():
            if not (folder/name).resolve().is_relative_to(folder.resolve()):raise ValueError('Unsafe native artifact entry')
            if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=wanted:raise ValueError('Native artifact changed: '+name)
        if 'ground_truth_valid' in record:
            inputs=record['gt_inputs'];base=repository/inputs['base'];queries=repository/inputs['queries']
            if any(not p.resolve().is_relative_to(repository.resolve()) for p in (base,queries)):raise ValueError('Unsafe GT input path')
            docs=CSR(base);qs=CSR(queries)
            try:
                k=inputs['k'];query_ids=inputs['query_ids'];truth=[]
                for qid in query_ids:truth.append(sorted(((did,score(docs.row(did),dict(qs.row(qid)))) for did in range(docs.n)),key=lambda h:(-h[1],h[0]))[:k])
                expected=struct.pack('<II',len(query_ids),k)+b''.join(struct.pack('<'+'i'*k,*[did for did,_ in hits]) for hits in truth)+b''.join(struct.pack('<'+'f'*k,*[value for _,value in hits]) for hits in truth)
                actual=(folder/'gt.bin').read_bytes()==expected
                if actual!=record['ground_truth_valid']:raise ValueError('Native GT classification differs from independent scoring')
                if actual:
                    valid_gt+=1
                    with (folder/'exact_reference/ground_truth.csv').open() as f:rows=list(csv.DictReader(f))
                    observed=[(int(r['query_id']),int(r['rank']),int(r['identifier']),float(r['score'])) for r in rows]
                    wanted=[(qid,rank+1,did,value) for qid,hits in zip(query_ids,truth) for rank,(did,value) in enumerate(hits)]
                    if observed!=wanted:raise ValueError('Saved independent GT CSV differs')
                else:invalid_gt+=1
            finally:docs.close();qs.close()
    for record in fixtures.get('diagnostics',{}).values():
        folder=repository/record['path']
        if not folder.resolve().is_relative_to(repository.resolve()):raise ValueError('Unsafe diagnostic path')
        for name,wanted in record['sha256'].items():
            if not (folder/name).resolve().is_relative_to(folder.resolve()):raise ValueError('Unsafe diagnostic entry')
            if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=wanted:raise ValueError('Diagnostic changed: '+name)
        if record['status']=='EXPECTED_FAILURE_NATIVE_UNDERFILLED':
            with (folder/'candidate_counts.csv').open() as f:counts=list(csv.DictReader(f))
            with (folder/'native_candidates.csv').open() as f:candidates=list(csv.DictReader(f))
            with (folder/'native_reference.csv').open() as f:references=list(csv.DictReader(f))
            if not any(int(c['unique_candidates'])<50 for c in counts):raise ValueError('Underfill diagnostic no longer contains underfill')
            for count in counts:
                qid=int(count['query_id']);hits=[int(r['identifier']) for r in candidates if int(r['query_id'])==qid];returned=[int(r['identifier']) for r in references if int(r['query_id'])==qid]
                if len(hits)!=int(count['exposed_slots']) or len(set(hits))!=int(count['unique_candidates']) or not set(returned).issubset(hits):raise ValueError('Diagnostic candidate accounting differs')
    print(json.dumps(dict(status='PASS',receipts=len(registry),fixtures_recomputed=len(fixtures['methods']),native_smoke_bundles_hash_checked=len(fixtures.get('native_smokes',{})),native_GT_bundles_validated=valid_gt,known_invalid_GT_diagnostics_preserved=invalid_gt,failed_diagnostics_preserved=len(fixtures.get('diagnostics',{})),scope='Saved integrity and small-fixture/GT recomputation only; native returned-ID acceptance separate; not independent performance reproduction')))


if __name__=='__main__':main()
