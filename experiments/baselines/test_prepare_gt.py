import argparse
from pathlib import Path
import random
from types import SimpleNamespace
import sys
import tempfile

from prepare_gt import generate,checked_gt
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from protocol import plan,save,sha
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tests'))
from smoke_test import write_csr


def reject(call):
    try:call()
    except (ValueError,FileExistsError):return
    raise AssertionError('Invalid GT request accepted')


def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='isolated-gt-') as temporary:
        root=Path(temporary);rng=random.Random(1031);row=lambda:{j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),16))}
        write_csr(root/'base.csr',[row() for _ in range(160)],32);write_csr(root/'queries.csr',[row() for _ in range(12)],32);plan(root/'queries.csr',root/'plan.json',20261023,3,4,2)
        args=SimpleNamespace(data=root/'base.csr',queries=root/'queries.csr',plan=root/'plan.json',binary=a.binary,out=root/'selection',role='selection',k=5,calibration_seed=7,lock=None)
        generate(args);selection,_=checked_gt(args.out)
        args.out=root/'calibration';args.role='calibration';generate(args);calibration,_=checked_gt(args.out)
        args.out=root/'evaluation-rejected';args.role='evaluation';reject(lambda:generate(args));assert not args.out.exists()
        binding={name:selection['binding'][name] for name in ('data_sha256','queries_sha256','plan_sha256')};binding['config']={'k':5}
        lock=dict(selected={'binding':binding},scope='Synthetic lock fixture only; not a real parameter selection');save(root/'lock.json',lock);Path(str(root/'lock.json')+'.sha256').write_text(sha(root/'lock.json'))
        args.lock=root/'lock.json';args.out=root/'evaluation';generate(args);evaluation,_=checked_gt(args.out)
        assert set(selection['query_ids']).isdisjoint(evaluation['query_ids']) and set(calibration['query_ids']).isdisjoint(selection['query_ids']+evaluation['query_ids'])
        reject(lambda:generate(args));args.out=root/'wrong-k';args.k=6;reject(lambda:generate(args));assert not args.out.exists()
        args.k=5;(root/'evaluation/ground_truth.csv').write_bytes((root/'evaluation/ground_truth.csv').read_bytes()+b'changed');reject(lambda:checked_gt(root/'evaluation'))
        (root/'lock.json').write_bytes((root/'lock.json').read_bytes()+b' ');args.out=root/'changed-lock';reject(lambda:generate(args));assert not args.out.exists()
        print('PASS: exact-GT generation enforces role IDs, calibration seed, final validation lock, locked k, artifact integrity and no overwrite')


if __name__=='__main__':main()
