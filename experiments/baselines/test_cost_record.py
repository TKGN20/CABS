import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from cost_record import audit_cost_record
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from protocol import save,load

ROOT=Path(__file__).resolve().parents[2]


def reject(call):
    try:call()
    except ValueError:return
    raise AssertionError('Invalid cost record was accepted')


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cabs-binary',type=Path,required=True);parser.add_argument('--out',type=Path);args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cost-record-') as temporary:
        root=Path(temporary);records={}
        for method,fixture,parameters in [('SOSIA','sosia_space_release',dict(ell=4,m=16)),('SINDI','sindi_space',{'lambda':10000,'native_reordering':True}),('SEISMIC','seismic_space',{})]:
            folder=ROOT/'experiments/delivery/fixtures'/fixture
            record=audit_cost_record(method,folder/'run',folder/'base.csr',parameters)
            assert record['IS_bytes']==record['index_structure_bytes']+record['original_vector_bytes'] and record['IT_seconds']>0
            if method!='SOSIA':assert record['serialized_bytes']!=record['IS_bytes']
            records[method]=record
        fixture=ROOT/'experiments/delivery/fixtures/seismic';cabs=root/'cabs';cabs.mkdir()
        (root/'calibration_ids.txt').write_text('3\n4\n');shutil.copyfile(fixture/'ids.txt',root/'ids.txt')
        command=[str(args.cabs_binary.resolve()),str(fixture/'base.csr'),str(fixture/'queries.csr'),str(root/'calibration_ids.txt'),str(root/'ids.txt'),str(cabs),'1031','100','50','3','24','0.2','1','2']
        subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
        records['CABS']=audit_cost_record('CABS',cabs,fixture/'base.csr',{})
        assert records['CABS']['IS_bytes']>records['CABS']['legacy_IS_bytes']
        if args.out:
            artifacts=args.out.parent/'fixture_artifacts';artifacts.mkdir(exist_ok=False)
            for name in ('base.csr','queries.csr'):shutil.copyfile(fixture/name,artifacts/name)
            for name in ('calibration_ids.txt','ids.txt'):shutil.copyfile(root/name,artifacts/name)
            shutil.copytree(cabs,artifacts/'run')
        damaged=root/'damaged';shutil.copytree(cabs,damaged)
        value=load(damaged/'index_space.json');value['counts']['minhash_postings_capacity']+=1;(damaged/'index_space.json').write_text(json.dumps(value))
        reject(lambda:audit_cost_record('CABS',damaged,fixture/'base.csr',{}))
        reject(lambda:audit_cost_record('SINDI',cabs,fixture/'base.csr',{'lambda':10000,'native_reordering':True}))
        reject(lambda:audit_cost_record('CABS',cabs,fixture/'base.csr',{'ell':24}))
        (cabs/'index_space.json').unlink()
        try:audit_cost_record('CABS',cabs,fixture/'base.csr',{})
        except FileNotFoundError:pass
        else:raise AssertionError('Missing detailed inventory accepted')
        if args.out:save(args.out,records)
        print(json.dumps(dict(status='PASS',methods=list(records),checks=['all four component schemas','fresh CABS execution','native API/serialized bytes distinct','tampered capacities and wrong method/config rejected','missing inventory rejected']),indent=2))


if __name__=='__main__':main()
