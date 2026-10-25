import argparse
import json
import math
from pathlib import Path
import re
import sys

from space_inventory import audit_cabs_inventory,audit_sosia_inventory,audit_sindi_inventory,audit_seismic_inventory
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from protocol import load,require,save,sha


def audit_cost_record(method,directory,base,parameters):
    require(method in ('CABS','SOSIA','SINDI','SEISMIC'),'Unknown cost-record method')
    root=Path(directory);base=Path(base)
    files=['summary.json','index_space.json']+[name for name in ('native_index.bin','resource_usage.log') if (root/name).is_file()]
    before={name:sha(root/name) for name in files};base_hash=sha(base)
    inventory=load(root/'index_space.json');summary=load(root/'summary.json')
    require(summary.get('method')==method and summary.get('IS_status')=='REPORTED_NOT_COMMON_VALIDATED','Method/status differs from detailed component record')
    if method=='CABS':
        require(parameters=={},'CABS automatic configuration comes from its bound record')
        checked=audit_cabs_inventory(root,base);time_key='IT_seconds';storage='Original mmap CSR, including its actual 24-byte file header'
    elif method=='SOSIA':
        require(set(parameters)=={'ell','m'} and all(type(v) is int and v>0 for v in parameters.values()),'SOSIA cost record needs positive integer ell/m')
        checked=audit_sosia_inventory(root,base,parameters['ell'],parameters['m']);time_key='build_seconds_including_loading';storage='Original CSR arrays with one logical CSR-header allowance under the disclosed baseline policy'
    elif method=='SINDI':
        require(set(parameters)=={'lambda','native_reordering'} and type(parameters['lambda']) is int and 10000<=parameters['lambda']<=100000 and type(parameters['native_reordering']) is bool,'Invalid SINDI cost parameters')
        checked=audit_sindi_inventory(root,base,parameters['lambda'],parameters['native_reordering']);time_key='build_seconds_including_loading';storage='Adapter original CSR plus native forward copies separately counted in structure bytes'
    else:
        require(parameters=={},'Only the currently audited plain SEISMIC adapter is supported')
        checked=audit_seismic_inventory(root,base);time_key='build_seconds_including_loading';storage='Full native plain u32/f32 forward heap; its original-vector payload is counted once'
    it=summary[time_key]
    require(type(it) in (float,int) and math.isfinite(it) and it>0,'Invalid construction time')
    require(summary['search_threads']==1,'Cost record is outside the registered single-search-thread contract')
    total=checked['index_plus_vectors_bytes'];structure=checked['index_structure_bytes'];vectors=checked['original_vector_bytes']
    require(total==structure+vectors,'Common cost sum differs')
    result=dict(schema=1,status='COST_COMPONENT_RECORD_AUDIT_ONLY',method=method,
        IT_seconds=it,IT_source_field=time_key,IS_bytes=total,index_structure_bytes=structure,original_vector_bytes=vectors,
        original_CSR_reference_bytes=base.stat().st_size,original_storage_definition=storage,
        components_status=checked['status'],parameters=parameters,
        binding=dict(data_sha256=base_hash,artifacts_sha256=before,sources_sha256={p.name:sha(p) for p in (Path(__file__),Path(__file__).with_name('space_inventory.py'))}),
        timing_scope='Saved driver construction time; query evaluation and external GT utility excluded; complete fairness/thread/warmup acceptance remains separate',
        scope='Unified record shape for independently checked component formulas and reported IT; not matched-recall, RSS-as-IS, parameter-selection or server-performance acceptance')
    if 'native_space_usage_bytes' in inventory:result['native_space_usage_bytes']=inventory['native_space_usage_bytes']
    if 'legacy_index_plus_vectors_bytes' in inventory:result['legacy_IS_bytes']=inventory['legacy_index_plus_vectors_bytes']
    if 'native_index.bin' in files:result['serialized_bytes']=(root/'native_index.bin').stat().st_size
    if 'resource_usage.log' in files:
        report=(root/'resource_usage.log').read_text();matches=re.findall(r'^\s*Maximum resident set size \(kbytes\): (\d+)\s*$',report,re.M)
        exits=re.findall(r'^\s*Exit status: (\d+)\s*$',report,re.M)
        require(len(matches)==1 and int(matches[0])>0 and exits==['0'],'Missing/invalid process RSS or unsuccessful resource record');result['peak_RSS_native_report']=dict(value=int(matches[0]),unit='kbytes as reported by GNU time; process peak, not retained index space')
    require(sha(base)==base_hash and before=={name:sha(root/name) for name in files},'Cost inputs changed during audit')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--method',choices=['CABS','SOSIA','SINDI','SEISMIC'],required=True)
    for name in ('directory','base','parameters','out'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();value=audit_cost_record(args.method,args.directory,args.base,load(args.parameters));save(args.out,value)
    print(json.dumps({name:value[name] for name in ('status','method','IT_seconds','IS_bytes','scope')},indent=2))
