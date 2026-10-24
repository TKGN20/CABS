import json
from pathlib import Path
import struct


def require(condition,message):
    if not condition:raise ValueError(message)


def audit_sosia_inventory_header(directory,n,d,nnz,original_file_bytes,ell,m):
    root=Path(directory);inventory=json.loads((root/'index_space.json').read_text());summary=json.loads((root/'summary.json').read_text())
    require(inventory['schema']==1 and inventory['method']=='SOSIA' and inventory['status']=='REPORTED_NOT_COMMON_VALIDATED','Unsupported inventory schema/status')
    counts=inventory['counts'];abi=inventory['abi_bytes'];components=inventory['components_bytes']
    require(all(type(value) is int and value>=0 for group in (counts,abi,components) for value in group.values()),'Invalid inventory byte/count value')
    require(abi['int']==4 and abi['float']==4 and abi['pointer']==struct.calcsize('P') and abi['Res']==8,'Unexpected native CSR/ABI types')
    require(abi['vector_int']>=3*abi['pointer'] and abi['vector_vector_int']==abi['vector_int'],'Unexpected vector header ABI')
    require((counts['documents'],counts['dimensions'],counts['nnz'],counts['ell'],counts['hash_functions'])==(n,d,nnz,ell,m),'Inventory inputs/configuration differ')
    domain=d*ell+1
    require(counts['hash_domain_slots']==domain and counts['bucket_tables']==m and counts['buckets']==m*domain,'Hash domain/table inventory differs')
    require(counts['bucket_table_capacity']>=m and counts['bucket_vector_capacity']>=m*domain and counts['sorted_cardinality_capacity']>=n,'Invalid container capacity')
    require(counts['bucket_slots_size']==n*m and counts['bucket_slots_capacity']>=n*m,'Postings missing or multiply counted')
    require(0<=counts['expanded_slots_size']<=nnz*ell and counts['expanded_slots_capacity']>=counts['expanded_slots_size'],'Invalid expanded-slot count/capacity')
    require(0<=counts['expanded_empty_rows']<=n,'Invalid empty expanded-row count')
    expected={
        'index_object_headers':abi['Sosia'],
        'sorted_cardinalities':counts['sorted_cardinality_capacity']*abi['Res'],
        'bucket_table_headers':counts['bucket_table_capacity']*abi['vector_vector_int'],
        'bucket_vector_headers':counts['bucket_vector_capacity']*abi['vector_int'],
        'bucket_slot_capacity':counts['bucket_slots_capacity']*abi['int'],
        'encoding_object_headers':abi['mips2set'],
        'expanded_vector_headers':n*abi['vector_int'],
        'expanded_slot_capacity':counts['expanded_slots_capacity']*abi['int'],
        'hash_permutation_pointer_array':m*abi['pointer'],
        'hash_permutation_slots':m*domain*abi['int'],
        'data_hash_pointer_array':n*abi['pointer'],
        'data_hash_slots':n*m*abi['int'],
        'preprocessing_object_headers':abi['Preprocess'],
        'preprocessing_square_norms':n*abi['float'],
    }
    require(set(components)==set(expected)|{'index_labels_payload','preprocessing_labels_payload'},'Missing or unexpected retained component')
    require(all(components[name]==value for name,value in expected.items()),'Retained component does not match analytic count/ABI')
    original=24+8*(n+1)+8*nnz;structure=sum(components.values())
    require(original==original_file_bytes==inventory['original_vector_bytes'],'Original CSR storage differs or is counted twice')
    require(structure==inventory['index_structure_bytes'] and original+structure==inventory['index_plus_vectors_bytes']==summary['IS_bytes'],'IS sum differs')
    require(summary['IS_status']==inventory['status'],'Summary IS status differs')
    return dict(status='SOSIA_COMPONENT_ACCOUNTING_PASS',index_structure_bytes=structure,original_vector_bytes=original,index_plus_vectors_bytes=original+structure,
                scope='Analytic component/count/ABI and sum checks only; common four-method IS and performance acceptance pending')


def audit_sosia_inventory(directory,base,ell,m):
    with Path(base).open('rb') as f:n,d,nnz=struct.unpack('<QQQ',f.read(24))
    return audit_sosia_inventory_header(directory,n,d,nnz,Path(base).stat().st_size,ell,m)


def audit_sindi_inventory_header(directory,n,d,nnz,original_file_bytes,lambda_,native_reordering,expected_postings=None):
    root=Path(directory);inventory=json.loads((root/'index_space.json').read_text());summary=json.loads((root/'summary.json').read_text())
    require(inventory['schema']==1 and inventory['method']=='SINDI' and inventory['status']=='REPORTED_NOT_COMMON_VALIDATED','Unsupported SINDI inventory schema/status')
    counts=inventory['counts'];abi=inventory['abi_bytes'];components=inventory['components_bytes']
    require(all(type(v) is int and v>=0 for group in (counts,abi,components) for v in group.values()),'Invalid SINDI count/byte value')
    require(abi['uint32']==abi['float']==4 and abi['uint64']==8 and abi['pointer']==struct.calcsize('P'),'Unexpected native SINDI ABI')
    require(abi['InvertedList']>=4+3*abi['pointer'] and abi['SparseVector']>=4+2*abi['pointer'] and abi['Sindi']>0 and abi['mutex']>0 and abi['vector_SparseVector']==3*abi['pointer'],'Invalid native object headers')
    require((counts['documents'],counts['dimensions'],counts['nnz'],counts['lambda'])==(n,d,nnz,lambda_),'SINDI input/configuration differs')
    require(counts['windows']==(n+lambda_-1)//lambda_ and counts['mutex_capacity']>=d,'Invalid native windows/retained mutex capacity')
    require(0<counts['nonempty_lists']<=min(d,counts['postings']) and counts['postings']<=nnz,'Invalid native posting count')
    if expected_postings is not None:require(counts['postings']==expected_postings,'Native postings differ from independent expectation')
    require(type(inventory['native_reordering']) is bool and inventory['native_reordering']==native_reordering==summary['native_reranking'],'Native forward-copy policy differs')
    require(counts['forward_nnz']==(nnz if native_reordering else 0),'Native forward vectors missing or multiply counted')
    require(counts['original_offset_capacity']>=n+1 and counts['original_coordinate_capacity']>=nnz and counts['original_value_capacity']>=nnz and counts['adapter_row_view_capacity']>=n,'Invalid original/view capacity')
    expected={
        'native_index_object_headers':abi['Sindi'],
        'native_inverted_list_headers':d*abi['InvertedList'],
        'native_posting_ids':counts['postings']*4,
        'native_posting_values':counts['postings']*4,
        'native_window_offsets':counts['nonempty_lists']*(counts['windows']+1)*4,
        'native_forward_row_headers':n*abi['SparseVector'] if native_reordering else 0,
        'native_forward_ids':counts['forward_nnz']*4,
        'native_forward_values':counts['forward_nnz']*4,
        'native_retained_mutex_capacity':counts['mutex_capacity']*abi['mutex'],
        'adapter_row_view_headers':counts['adapter_row_view_capacity']*abi['SparseVector'],
        'adapter_row_view_container_header':abi['vector_SparseVector'],
        'original_capacity_slack':8*(counts['original_offset_capacity']-n-1)+4*(counts['original_coordinate_capacity']-nnz)+4*(counts['original_value_capacity']-nnz),
    }
    require(components==expected,'SINDI retained components differ from analytic count/ABI')
    native=sum(value for name,value in expected.items() if name.startswith('native_'))
    require(native==inventory['native_payload_bytes'],'Native SINDI payload sum differs')
    original=24+8*(n+1)+8*nnz;structure=sum(expected.values())
    require(original==original_file_bytes==inventory['original_vector_bytes'],'Original CSR bytes differ or are counted twice')
    require(structure==inventory['index_structure_bytes'] and original+structure==inventory['index_plus_vectors_bytes']==summary['IS_bytes'],'SINDI IS sum differs')
    require(summary['IS_status']==inventory['status'],'Summary SINDI IS status differs')
    return dict(status='SINDI_COMPONENT_ACCOUNTING_PASS',index_structure_bytes=structure,original_vector_bytes=original,index_plus_vectors_bytes=original+structure,
                native_forward_copy_bytes=expected['native_forward_row_headers']+expected['native_forward_ids']+expected['native_forward_values'],
                scope='Analytic component/count/ABI checks only; common IS and server performance acceptance pending')


def audit_sindi_inventory(directory,base,lambda_,native_reordering,expected_postings=None):
    with Path(base).open('rb') as f:n,d,nnz=struct.unpack('<QQQ',f.read(24))
    return audit_sindi_inventory_header(directory,n,d,nnz,Path(base).stat().st_size,lambda_,native_reordering,expected_postings)


def audit_sindi_serialization(directory,base):
    from oracle import CSR
    root=Path(directory);inventory=json.loads((root/'index_space.json').read_text());roundtrip=json.loads((root/'roundtrip.json').read_text());counts=inventory['counts']
    data=(root/'native_index.bin').read_bytes();cursor=0
    def take(format_,length=1):
        nonlocal cursor
        size=struct.calcsize('<'+format_)*length
        require(cursor+size<=len(data),'Truncated native serialized data')
        result=struct.unpack_from('<'+format_*length,data,cursor);cursor+=size;return result
    n,d,lambda_,windows=take('I',4)
    require((n,d,lambda_,windows)==tuple(counts[key] for key in ('documents','dimensions','lambda','windows')),'Serialized header differs from runtime inventory')
    docs=CSR(base);postings=nonempty=forward=0
    try:
        for term in range(d):
            length=take('I')[0]
            if not length:continue
            postings+=length;nonempty+=1
            ids=take('I',length);values=take('f',length);offsets=take('I',windows+1)
            require(list(ids)==sorted(set(ids)) and ids[-1]<n,'Invalid serialized posting IDs')
            require(offsets[0]==0 and offsets[-1]==length and list(offsets)==sorted(offsets),'Invalid serialized window offsets')
            for did,value in zip(ids,values):require(dict(docs.row(did)).get(term)==value,'Serialized posting differs from original vector')
            for window,boundary in enumerate(offsets):require(boundary==sum(did<window*lambda_ for did in ids),'Serialized window boundary differs')
        if inventory['native_reordering']:
            for did in range(n):
                length=take('I')[0];forward+=length
                ids=take('I',length);values=take('f',length)
                require(list(zip(ids,values))==list(docs.row(did)),'Serialized native forward copy differs from original CSR')
    finally:docs.close()
    require(cursor==len(data),'Unexpected native serialized tail')
    require((postings,nonempty,forward)==tuple(counts[key] for key in ('postings','nonempty_lists','forward_nnz')),'Serialized allocation counts differ from runtime inventory')
    before=roundtrip['before'];after=roundtrip['after']
    require(roundtrip['ranked_and_native_outputs_equal'] is True,'Native serialize/reload result check missing')
    require(before['counts']=={k:counts[k] for k in before['counts']},'Roundtrip preimage counts differ')
    expected_counts=dict(before['counts'],mutex_capacity=0)
    expected_components=dict(before['components_bytes'],native_retained_mutex_capacity=0)
    require(after['counts']==expected_counts and after['components_bytes']==expected_components,'Reloaded components differ beyond released build mutexes')
    require(after['native_payload_bytes']==sum(expected_components.values()) and after['abi_bytes']==before['abi_bytes'],'Reloaded native payload/ABI differs')
    expected_size=16+4*d+8*postings+4*nonempty*(windows+1)+(4*n+8*forward if inventory['native_reordering'] else 0)
    require(len(data)==expected_size,'Serialized byte formula differs')
    return dict(status='SINDI_SERIALIZATION_CROSSCHECK_PASS',serialized_bytes=len(data),postings=postings,nonempty_lists=nonempty,forward_nnz=forward,
                runtime_payload_includes_retained_build_mutexes=True,ranked_and_native_outputs_equal=True)


def audit_seismic_inventory_header(directory,n,d,nnz,original_file_bytes,expected_postings=None):
    root=Path(directory);inventory=json.loads((root/'index_space.json').read_text());summary=json.loads((root/'summary.json').read_text())
    require(inventory['schema']==1 and inventory['method']=='SEISMIC' and inventory['status']=='REPORTED_NOT_COMMON_VALIDATED','Unsupported SEISMIC inventory schema/status')
    counts=inventory['counts'];abi=inventory['abi_bytes'];components=inventory['components_bytes']
    require(all(type(v) is int and v>=0 for group in (counts,abi,components) for v in group.values()),'Invalid SEISMIC count/byte value')
    require(abi['usize']==struct.calcsize('P')==8 and abi['component']==abi['float']==4 and abi['PackedPostingBlock']==8,'Unexpected SEISMIC plain-storage ABI')
    require(abi['forward_store']==7*abi['usize'] and abi['index']>abi['forward_store'] and abi['PostingList']>0,'Unaudited forward store/object headers')
    require((counts['documents'],counts['dimensions'],counts['nnz'],counts['posting_lists'])==(n,d,nnz,d),'SEISMIC original input dimensions differ')
    require(inventory['knn_present'] is False and counts['configuration_path_capacity']==0,'Optional kNN/path is outside the current plain adapter')
    require(0<counts['packed_postings']<=nnz and counts['sparse_summary_lists']+counts['dense_summary_lists']==d,'Invalid native posting/summary-list count')
    if expected_postings is not None:require(counts['packed_postings']==expected_postings,'SEISMIC posting count differs from independent expectation')
    require(counts['summary_minimums']==counts['summary_quants']==counts['summaries'],'Quantized summary scalar counts differ')
    require(counts['summaries']<=counts['block_offsets']<=counts['summaries']+d and counts['summary_values']>=counts['summaries'],'Summary/block offsets differ')
    require(counts['adapter_offset_map_capacity']>=n+1,'Retained original-ID offset map is missing')
    forward=8*(n+1)+8*nnz
    expected={
        'native_index_object_headers':abi['index'],
        'forward_store_heap':forward,
        'posting_list_object_array':d*abi['PostingList'],
        'packed_postings':counts['packed_postings']*8,
        'block_offsets':counts['block_offsets']*8,
        'summary_component_ids':counts['summary_components']*4,
        'summary_offsets_heap':counts['summary_offsets_heap_bytes'],
        'summary_ids_heap':counts['summary_ids_heap_bytes'],
        'summary_values':counts['summary_values'],
        'summary_minimums':counts['summary_minimums']*4,
        'summary_quants':counts['summary_quants']*4,
        'knn_heap':0,'configuration_path_capacity':0,
        'adapter_offset_map_header':3*abi['usize'],
        'adapter_offset_map_capacity':counts['adapter_offset_map_capacity']*8,
    }
    require(components==expected,'SEISMIC components differ from retained count/ABI')
    total=sum(expected.values());native=total-expected['adapter_offset_map_header']-expected['adapter_offset_map_capacity']
    require(native==inventory['native_payload_bytes'] and inventory['native_space_usage_bytes']>0,'SEISMIC native payload sum differs')
    require(original_file_bytes==24+forward==inventory['original_CSR_reference_bytes'],'Reference CSR bytes differ')
    require(inventory['original_vector_bytes']==forward and inventory['index_structure_bytes']==total-forward and inventory['index_plus_vectors_bytes']==total==summary['IS_bytes'],'SEISMIC forward store missing, duplicated or IS sum differs')
    require(summary['IS_status']==inventory['status'],'SEISMIC summary IS status differs')
    return dict(status='SEISMIC_COMPONENT_ACCOUNTING_PASS',index_structure_bytes=total-forward,original_vector_bytes=forward,index_plus_vectors_bytes=total,
                native_space_usage_bytes=inventory['native_space_usage_bytes'],
                scope='Counts/ABI/sums and pinned compressed-storage MemSize capacity traversal; common IS and server performance acceptance pending')


def audit_seismic_inventory(directory,base,expected_postings=None):
    with Path(base).open('rb') as f:n,d,nnz=struct.unpack('<QQQ',f.read(24))
    return audit_seismic_inventory_header(directory,n,d,nnz,Path(base).stat().st_size,expected_postings)


def audit_seismic_roundtrip(directory):
    root=Path(directory);proof=json.loads((root/'roundtrip.json').read_text());inventory=json.loads((root/'index_space.json').read_text())
    require(proof['all_index_fields_equal'] is True and proof['native_candidates_and_exact_outputs_equal'] is True,'SEISMIC roundtrip equality check missing')
    require(proof['serialized_bytes']==(root/'native_index.bin').stat().st_size>0,'SEISMIC serialized size differs')
    before=proof['before'];after=proof['after']
    require(before['native_payload_bytes']==inventory['native_payload_bytes'],'SEISMIC roundtrip preimage differs')
    capacity_counts={'summary_offsets_heap_bytes','summary_ids_heap_bytes'}
    require({k:v for k,v in before['counts'].items() if k not in capacity_counts}=={k:v for k,v in after['counts'].items() if k not in capacity_counts},'Reload changed logical counts')
    capacity_components={'summary_offsets_heap','summary_ids_heap'}
    require({k:v for k,v in before['components_bytes'].items() if k not in capacity_components}=={k:v for k,v in after['components_bytes'].items() if k not in capacity_components},'Reload changed fixed retained payload')
    require(after['native_payload_bytes']==sum(after['components_bytes'].values()) and after['abi_bytes']==before['abi_bytes'],'Reloaded SEISMIC component sum/ABI differs')
    return dict(status='SEISMIC_SERIALIZATION_CROSSCHECK_PASS',serialized_bytes=proof['serialized_bytes'],all_index_fields_equal=True,native_candidates_and_exact_outputs_equal=True,
                runtime_capacity_before=before['native_payload_bytes'],runtime_capacity_after=after['native_payload_bytes'])
import argparse
import hashlib


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--method',choices=['SOSIA','SINDI','SEISMIC'],default='SOSIA');p.add_argument('--directory',type=Path,required=True);p.add_argument('--base',type=Path,required=True);p.add_argument('--ell',type=int);p.add_argument('--m',type=int);p.add_argument('--lambda',dest='lambda_',type=int);p.add_argument('--no-native-reordering',action='store_true');p.add_argument('--roundtrip',action='store_true');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if a.method=='SOSIA':
        require(a.ell is not None and a.ell>0 and a.m is not None and a.m>0,'SOSIA requires positive ell/m');result=audit_sosia_inventory(a.directory,a.base,a.ell,a.m)
    elif a.method=='SINDI':
        require(a.lambda_ is not None and a.lambda_>0,'SINDI requires positive lambda');result=audit_sindi_inventory(a.directory,a.base,a.lambda_,not a.no_native_reordering)
    else:result=audit_seismic_inventory(a.directory,a.base)
    if a.roundtrip:
        require(a.method!='SOSIA','SOSIA roundtrip is not supported')
        result['roundtrip']=audit_sindi_serialization(a.directory,a.base) if a.method=='SINDI' else audit_seismic_roundtrip(a.directory)
    result['artifacts_sha256']={name:hashlib.sha256((a.directory/name).read_bytes()).hexdigest() for name in ('index_space.json','summary.json')}
    with a.out.open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps(result,indent=2))
