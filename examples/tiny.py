#!/usr/bin/env python3
"""Run CABS on reproducible synthetic vectors without downloading any dataset."""
import argparse
import csv
import hashlib
import json
import platform
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from evaluate import audit


def write_csr(path,rows,dimension):
    offsets=[0];coordinates=[];values=[];decoded=[]
    for row in rows:
        actual={}
        for coordinate,value in sorted(row.items()):
            value=struct.unpack('<f',struct.pack('<f',value))[0]
            coordinates.append(coordinate);values.append(value);actual[coordinate]=value
        offsets.append(len(coordinates));decoded.append(actual)
    path.write_bytes(struct.pack('<QQQ',len(rows),dimension,len(values))
        +struct.pack('<'+'Q'*len(offsets),*offsets)
        +struct.pack('<'+'i'*len(coordinates),*coordinates)
        +struct.pack('<'+'f'*len(values),*values))
    return decoded


def exact_score(document,query):
    total=0.0
    for coordinate,value in document.items():total+=value*query.get(coordinate,0.0)
    return total


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/cabs')
    parser.add_argument('--out',type=Path,default=ROOT/'results/tiny')
    parser.add_argument('--maintenance',action='store_true',help='Also exercise insert/delete/update/refresh through the CLI')
    args=parser.parse_args();binary=args.binary.resolve();out=args.out.resolve()
    if not binary.is_file():parser.error('Build CABS first, or pass --binary /path/to/cabs')
    out.mkdir(parents=True,exist_ok=False)
    rng=random.Random(84691)
    def row():return {j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),8))}
    documents=write_csr(out/'base.csr',[row() for _ in range(120)],32)
    queries=write_csr(out/'queries.csr',[row() for _ in range(16)],32)
    result=out/'run';result.mkdir()
    command=[str(binary),'dynamic' if args.maintenance else 'run',
        '--data',str(out/'base.csr'),'--queries',str(out/'queries.csr'),'--out',str(result),
        '--ell','24','--delta-h','0.2','--k','50','--kmax','100',
        '--calibration-queries','4','--eval-queries','12','--seed','20261007',
        '--threads','2','--iterations','10','--repeats','2','--warmup-queries','5',
        '--keep-sketch-cache','0','--benchmark-exact','0','--diagnostic-queries','0','--equivalence-queries','0']
    if args.maintenance:command+=['--dynamic-operations','2']
    manifest=dict(data_kind='generated synthetic example',seed=84691,command=command,
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),python=sys.version,platform=platform.platform(),
        input_sha256={name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in ('base.csr','queries.csr')},
        scope='Functional example only; synthetic timing is not a dataset benchmark or paper result',status='running')
    (out/'example.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    try:
        with (out/'run.log').open('w') as log:subprocess.run(command,stdout=log,stderr=log,check=True)
        metrics=audit(result);truth={}
        with (result/'ground_truth.csv').open() as stream:
            for item in csv.DictReader(stream):truth.setdefault(int(item['query_id']),[]).append((int(item['identifier']),float(item['score'])))
        for qid,observed in truth.items():
            expected=sorted(((i,exact_score(document,queries[qid])) for i,document in enumerate(documents)),key=lambda h:(-h[1],h[0]))[:len(observed)]
            if observed!=expected:raise ValueError('Ground truth differs from independent original-vector scoring')
        with (result/'retrieved.csv').open() as stream:
            for item in csv.DictReader(stream):
                if float(item['score'])!=exact_score(documents[int(item['identifier'])],queries[int(item['query_id'])]):raise ValueError('Returned original-vector score differs')
        if args.maintenance:
            with (result/'dynamic.csv').open() as stream:operations=list(csv.DictReader(stream))
            expected={'insert_in_range','insert_out_of_range','update_fresh_id','delete_main_and_delta','refresh'}
            if {item['operation'] for item in operations}!=expected or any(int(item['active'])!=120 for item in operations if item['operation']=='refresh'):raise ValueError('Maintenance checkpoints are incomplete')
        manifest.update(status='PASS',independent_GT_queries=len(truth),ranked_scores_checked=True,
            maintenance_checked=args.maintenance,automatic_configuration=json.loads((result/'automatic_configuration.json').read_text()),
            metrics=metrics)
    except BaseException as error:manifest.update(status='FAIL',error=str(error));raise
    finally:(out/'example.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status='PASS',output=str(out),independent_GT_queries=len(truth),maintenance_checked=args.maintenance,
        scope=manifest['scope']),indent=2))


if __name__=='__main__':main()
