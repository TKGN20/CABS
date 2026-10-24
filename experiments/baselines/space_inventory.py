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
import argparse
import hashlib


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);p.add_argument('--base',type=Path,required=True);p.add_argument('--ell',type=int,required=True);p.add_argument('--m',type=int,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    result=audit_sosia_inventory(a.directory,a.base,a.ell,a.m)
    result['artifacts_sha256']={name:hashlib.sha256((a.directory/name).read_bytes()).hexdigest() for name in ('index_space.json','summary.json')}
    with a.out.open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps(result,indent=2))
