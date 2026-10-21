#!/usr/bin/env python3
"""Exercise the production CSR/build/search path with independent exact scores."""
import argparse
import csv
import json
import random
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from evaluate import audit


def write_csr(path,rows,dimension):
    pointers=[0];indices=[];values=[];decoded=[]
    for row in rows:
        actual={}
        for coordinate,value in sorted(row.items()):
            value=struct.unpack('<f',struct.pack('<f',value))[0]
            indices.append(coordinate);values.append(value);actual[coordinate]=value
        pointers.append(len(indices));decoded.append(actual)
    with path.open('wb') as stream:
        stream.write(struct.pack('<QQQ',len(rows),dimension,len(indices)))
        stream.write(struct.pack('<'+'Q'*len(pointers),*pointers))
        stream.write(struct.pack('<'+'i'*len(indices),*indices))
        stream.write(struct.pack('<'+'f'*len(values),*values))
    return decoded


def check(binary):
    rng=random.Random(84691)
    with tempfile.TemporaryDirectory(prefix='cabs-smoke-') as temporary:
        directory=Path(temporary)
        make_row=lambda:{j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),8))}
        docs=write_csr(directory/'base.csr',[make_row() for _ in range(120)],32)
        queries=write_csr(directory/'queries.csr',[make_row() for _ in range(16)],32)
        result=directory/'result';result.mkdir()
        command=[str(binary),'run','--data',str(directory/'base.csr'),'--queries',str(directory/'queries.csr'),
                 '--out',str(result),'--ell','24','--delta-h','0.2','--k','50','--kmax','50',
                 '--calibration-queries','4','--eval-queries','12','--threads','2','--iterations','10',
                 '--repeats','2','--equivalence-queries','12','--benchmark-exact','0','--diagnostic-queries','0']
        completed=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if completed.returncode:
            raise RuntimeError(completed.stdout+completed.stderr)
        metrics=audit(result)
        truth={}
        with (result/'ground_truth.csv').open() as stream:
            for row in csv.DictReader(stream):
                truth.setdefault(int(row['query_id']),[]).append((int(row['identifier']),float(row['score'])))
        for qid,observed in truth.items():
            q=queries[qid]
            expected=sorted([(i,sum(value*q.get(j,0) for j,value in x.items())) for i,x in enumerate(docs)],key=lambda h:(-h[1],h[0]))[:50]
            if observed!=expected:
                raise ValueError('Production GT differs from independent Python sparse inner products')
        with (result/'retrieved.csv').open() as stream:
            for row in csv.DictReader(stream):
                q=queries[int(row['query_id'])];x=docs[int(row['identifier'])]
                expected=sum(value*q.get(j,0) for j,value in x.items())
                if expected!=float(row['score']):
                    raise ValueError('Returned score differs from independent original inner product')
        # Invalid binary inputs must fail explicitly rather than produce results.
        malformed=directory/'truncated.csr';malformed.write_bytes(b'bad csr')
        bad=subprocess.run([str(binary),'inspect','--data',str(malformed),'--queries',str(directory/'queries.csr')],capture_output=True)
        if bad.returncode==0:
            raise ValueError('Truncated CSR was accepted')
        print(json.dumps({'status':'passed','independent_GT_queries':len(truth),
                          'measured_queries':metrics['measured_queries'],'production_path':True}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/cabs')
    check(parser.parse_args().binary.resolve())
