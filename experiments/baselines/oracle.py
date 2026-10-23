import argparse
import csv
import hashlib
import json
import math
import mmap
from pathlib import Path
import struct


def require(value,message):
    if not value:raise ValueError(message)


class CSR:
    def __init__(self,path):
        self.file=Path(path).open('rb')
        try:
            self.data=mmap.mmap(self.file.fileno(),0,access=mmap.ACCESS_READ)
            require(len(self.data)>=32,'Truncated CSR')
            self.n,self.d,self.nnz=struct.unpack_from('<QQQ',self.data)
            require(self.d>0 and len(self.data)==24+8*(self.n+1)+8*self.nnz,'CSR header/size mismatch')
            self.ix=24+8*(self.n+1);self.val=self.ix+4*self.nnz
            pointers=struct.unpack_from('<'+'Q'*(self.n+1),self.data,24)
            require(pointers[0]==0 and pointers[-1]==self.nnz and all(a<=b for a,b in zip(pointers,pointers[1:])),'Invalid CSR pointers')
        except BaseException:
            if hasattr(self,'data'):self.data.close()
            self.file.close();raise

    def row(self,identifier):
        require(type(identifier) is int and 0<=identifier<self.n,'Original row ID out of range')
        a,b=struct.unpack_from('<QQ',self.data,24+8*identifier);count=b-a
        indices=struct.unpack_from('<'+'i'*count,self.data,self.ix+4*a)
        values=struct.unpack_from('<'+'f'*count,self.data,self.val+4*a)
        require(all(0<=i<self.d for i in indices) and all(a<b for a,b in zip(indices,indices[1:])),'Unsorted/duplicate/out-of-range coordinates')
        require(all(math.isfinite(v) and v>=0 for v in values),'Invalid original sparse values')
        return list(zip(indices,values))

    def close(self):
        self.data.close();self.file.close()


def score(document,query):
    value=0.0
    for coordinate,weight in document:
        if coordinate in query:value+=weight*query[coordinate]
    return value


def audit(base,queries,result_file,timing_file,query_ids,k,repeats):
    require(k>0 and repeats>0 and query_ids and len(set(query_ids))==len(query_ids),'Invalid requested scope')
    docs=CSR(base);qs=CSR(queries)
    try:
        require(docs.d==qs.d and k<=docs.n,'Dimension/k mismatch')
        targets={qid:dict(qs.row(qid)) for qid in query_ids};observed={qid:[] for qid in query_ids}
        with Path(result_file).open(newline='') as f:
            for row in csv.DictReader(f):
                qid=int(row['query_id']);identifier=int(row['identifier']);rank=int(row['rank']);reported=float(row['score'])
                require(qid in observed,'Unexpected query ID')
                require(math.isfinite(reported) and rank==len(observed[qid])+1 and rank<=k,'Invalid rank/score or truncated result group')
                require(reported==score(docs.row(identifier),targets[qid]),'Score differs from original binary64 inner product')
                observed[qid].append((identifier,reported))
        for hits in observed.values():
            require(len(hits)==k and len({h[0] for h in hits})==k,'Underfilled, missing query or duplicate result ID')
            require(hits==sorted(hits,key=lambda h:(-h[1],h[0])),'Ranking/tie order differs')
        require(docs.n*len(query_ids)<=1000000,'Fixture oracle scope too large; independent full GT requires separately authorized scalable tool')
        truth={qid:[] for qid in query_ids}
        for identifier in range(docs.n):
            document=docs.row(identifier)
            for qid,query in targets.items():truth[qid].append((identifier,score(document,query)))
        recalls={}
        for qid in query_ids:
            wanted={h[0] for h in sorted(truth[qid],key=lambda h:(-h[1],h[0]))[:k]}
            recalls[qid]=len(wanted.intersection(h[0] for h in observed[qid]))/k
        with Path(timing_file).open(newline='') as f:rows=list(csv.DictReader(f))
        expected={(r,qid) for r in range(repeats) for qid in query_ids}
        actual={(int(r['repeat']),int(r['query_id'])) for r in rows}
        require(len(rows)==len(actual) and actual==expected,'Missing/duplicate timing or unexpected query ID')
        require(all(math.isfinite(float(r['seconds'])) and float(r['seconds'])>0 for r in rows),'Invalid measured query time')
        for row in rows:
            if 'recall' in row:require(float(row['recall'])==recalls[int(row['query_id'])],'Reported recall differs from independent full GT')
        elapsed=sum(float(r['seconds']) for r in rows)
        return dict(status='PASS',queries=len(query_ids),k=k,repeats=repeats,Recall=sum(recalls.values())/len(recalls),QPS=len(rows)/elapsed,
                    scope='Independent small-fixture exact-score/GT and raw timing audit; no performance claim',
                    input_sha256={str(p):hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in (base,queries,result_file,timing_file)})
    finally:
        docs.close();qs.close()


if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('base','queries','results','timings','query-ids','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--k',type=int,default=50);p.add_argument('--repeats',type=int,default=3);a=p.parse_args()
    result=audit(a.base,a.queries,a.results,a.timings,[int(v) for v in a.query_ids.read_text().split()],a.k,a.repeats)
    with a.out.open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps(result,indent=2))
