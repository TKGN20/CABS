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
        actual=audit(folder/'base.csr',folder/'queries.csr',folder/'run/retrieved.csv',folder/'run/queries.csv',[int(v) for v in (folder/'ids.txt').read_text().split()],record.get('k',50),record.get('repeats',3))
        if actual['Recall']!=record['Recall']:raise ValueError('Fixture recall differs: '+method)
        if 'sosia_space' in record:
            from space_inventory import audit_sosia_inventory
            config=record['sosia_space'];audit_sosia_inventory(folder/'run',folder/'base.csr',config['ell'],config['m'])
            counts=json.loads((folder/'run/index_space.json').read_text())['counts']
            if 'expected_empty_expansions' in record and counts['expanded_empty_rows']!=record['expected_empty_expansions']:raise ValueError('Empty expanded row fixture differs')
            if record.get('require_unused_bucket_capacity') and counts['bucket_slots_capacity']<=counts['bucket_slots_size']:raise ValueError('Unused capacity fixture differs')
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
        elif record['status']=='EXPECTED_FAILURE_ZERO_QUERY_NATIVE_BOUNDARY':
            result=json.loads((folder/'receipt.json').read_text());build=json.loads((folder/'build_receipt.json').read_text())
            if result['returncode']==0 or 'AddressSanitizer: heap-buffer-overflow' not in (folder/'stderr.log').read_text(encoding='utf-8') or hashlib.sha256((folder/'compiled_driver.cpp').read_bytes()).hexdigest()!=build['adapter_sha256']:raise ValueError('SOSIA zero-query diagnostic differs')
    for record in list(fixtures.get('integrity_bundles',{}).values())+list(fixtures.get('real_smokes',{}).values())+list(fixtures.get('space_inventories',{}).values()):
        folder=repository/record['path']
        if not folder.resolve().is_relative_to(repository.resolve()):raise ValueError('Unsafe evidence bundle path')
        if {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()}!=set(record['sha256']):raise ValueError('Evidence bundle inventory changed')
        for name,wanted in record['sha256'].items():
            if not (folder/name).resolve().is_relative_to(folder.resolve()) or hashlib.sha256((folder/name).read_bytes()).hexdigest()!=wanted:raise ValueError('Evidence bundle changed: '+name)
    for record in fixtures.get('space_inventories',{}).values():
        from space_inventory import audit_sosia_inventory_header
        folder=repository/record['path'];preflight=json.loads((root/'preflight_20261023.json').read_text());header=next(item for item in preflight['inputs'] if item['file']==record['preflight_input'])
        observed=audit_sosia_inventory_header(folder,header['n'],header['d'],header['nnz'],header['bytes'],record['ell'],record['m'])
        saved=json.loads((folder/'current_space_audit.json').read_text());audited=json.loads((folder/'independent_audit.json').read_text())
        if observed!=saved or audited['binding']['data_sha256']!=header['sha256']:raise ValueError('Saved real component accounting differs from input receipt')
    real_methods=0
    for record in fixtures.get('real_smokes',{}).values():
        from prepare_gt import checked_gt
        folder=repository/record['path'];manifest=json.loads((folder/'manifest.json').read_text());gt,rows=checked_gt(folder/'selection-gt');ids=manifest['selection_query_ids'];k=gt['k']
        if manifest['status']!='completed_smoke_only' or gt['role']!='selection' or gt['query_ids']!=ids or set(ids).intersection(manifest['calibration_query_ids']):raise ValueError('Real smoke query scope differs')
        truth={qid:{int(r['identifier']) for r in rows if int(r['query_id'])==qid} for qid in ids}
        methods={name:folder/name for name in manifest['methods']};methods['SINDI_original_control']=folder/'SINDI_original_control'
        for method,directory in methods.items():
            with (directory/'retrieved.csv').open() as f:hits=list(csv.DictReader(f))
            if [(int(r['query_id']),int(r['rank'])) for r in hits]!=[(qid,rank) for qid in ids for rank in range(1,k+1)]:raise ValueError('Real smoke result query IDs/ranks differ')
            recalls={}
            for qid in ids:
                ranked=[(int(r['identifier']),float(r['score'])) for r in hits if int(r['query_id'])==qid]
                if len(set(did for did,_ in ranked))!=k or any(did<0 or did>=gt['documents'] or not math.isfinite(value) or value<0 for did,value in ranked) or ranked!=sorted(ranked,key=lambda h:(-h[1],h[0])):raise ValueError('Real smoke ranked result invalid')
                recalls[qid]=len(truth[qid].intersection(did for did,_ in ranked))/k
            with (directory/'queries.csv').open() as f:timings=list(csv.DictReader(f))
            expected={(repeat,qid) for repeat in range(3) for qid in ids}
            if len(timings)!=len(expected) or {(int(r['repeat']),int(r['query_id'])) for r in timings}!=expected or any(not math.isfinite(float(r['seconds'])) or float(r['seconds'])<=0 for r in timings):raise ValueError('Real smoke timing scope differs')
            audit_record=json.loads((directory/'independent_audit.json').read_text());recall=sum(recalls.values())/len(ids);elapsed=0.0
            for row in timings:elapsed+=float(row['seconds'])
            if not math.isclose(recall,audit_record['Recall'],rel_tol=1e-12,abs_tol=1e-12) or not math.isclose(len(timings)/elapsed,audit_record['QPS'],rel_tol=1e-12):raise ValueError('Real smoke saved audit differs from raw GT/timing')
            for name,wanted in audit_record['binding']['result_artifacts'].items():
                if Path(name).name!=name or hashlib.sha256((directory/name).read_bytes()).hexdigest()!=wanted:raise ValueError('Real smoke audited record hash differs')
            if method in manifest['methods'] and (manifest['methods'][method]['status']!='SMOKE_AUDITED' or not math.isclose(recall,manifest['methods'][method]['Recall'],abs_tol=1e-12)):raise ValueError('Real smoke manifest differs')
            real_methods+=1
    print(json.dumps(dict(status='PASS',receipts=len(registry),fixtures_recomputed=len(fixtures['methods']),native_smoke_bundles_hash_checked=len(fixtures.get('native_smokes',{})),native_GT_bundles_validated=valid_gt,known_invalid_GT_diagnostics_preserved=invalid_gt,failed_diagnostics_preserved=len(fixtures.get('diagnostics',{})),real_smoke_method_records_recomputed=real_methods,real_component_inventories_recomputed=len(fixtures.get('space_inventories',{})),scope='Saved integrity, synthetic exact-score/GT and real-smoke saved GT/timing/component arithmetic; real original-vector scores require dataset audit; not independent performance reproduction')))


if __name__=='__main__':main()
