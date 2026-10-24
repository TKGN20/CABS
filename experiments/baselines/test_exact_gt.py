import argparse
import csv
from pathlib import Path
import random
import subprocess
import sys
import tempfile

from oracle import CSR,score
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tests'))
from smoke_test import write_csr


def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='independent-gt-') as temporary:
        root=Path(temporary);rng=random.Random(1031)
        random_row=lambda:{j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),16))}
        cases=[('random',[random_row() for _ in range(160)],[random_row() for _ in range(12)],32,[9,2,11,0],50),('ties-zeros',[{0:1},{0:1},{1:2},{0:.5,1:1},{2:1},{},{}],[{0:1},{1:1},{2:1},{}, {0:0}],3,[4,2,0,3,1],7),('rounding',[{0:1e8,1:1,2:1},{0:1e8,1:2,2:1},{1:1e-20},{0:0}],[{0:1,1:1,2:1},{1:1e-20}],3,[1,0],4)]
        for name,documents,queries,d,ids,k in cases:
            folder=root/name;folder.mkdir();write_csr(folder/'base.csr',documents,d);write_csr(folder/'queries.csr',queries,d);(folder/'ids.txt').write_text(''.join(str(q)+'\n' for q in ids));out=folder/'out';out.mkdir()
            command=[str(a.binary.resolve()),str(folder/'base.csr'),str(folder/'queries.csr'),str(folder/'ids.txt'),str(k),str(out)];subprocess.run(command,check=True)
            docs=CSR(folder/'base.csr');qs=CSR(folder/'queries.csr')
            try:
                with (out/'ground_truth.csv').open() as f:observed=list(csv.DictReader(f))
                expected=[(qid,rank+1,did,value) for qid in ids for rank,(did,value) in enumerate(sorted(((did,score(docs.row(did),dict(qs.row(qid)))) for did in range(docs.n)),key=lambda h:(-h[1],h[0]))[:k])]
                actual=[(int(r['query_id']),int(r['rank']),int(r['identifier']),float(r['score'])) for r in observed]
                if actual!=expected:raise ValueError('Exact reference differs from independent Python scoring: '+name)
            finally:docs.close();qs.close()
            if subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode==0:raise ValueError('Existing GT was overwritten')
            bad=folder/'bad';bad.mkdir();command[-1]=str(bad);(folder/'ids.txt').write_text(str(ids[0])+'\n'+str(ids[0])+'\n')
            if subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode==0:raise ValueError('Duplicate query IDs accepted')
        print('PASS: independent exact GT matches Python for random vectors, ties, empty/zero rows, rounding and original query IDs; overwrite/duplicate guards pass')


if __name__=='__main__':main()
