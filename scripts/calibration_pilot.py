#!/usr/bin/env python3

import argparse
import csv
import datetime as dt
import json
import math
import os
from pathlib import Path
import subprocess

from prepare_data import ROOT, prepare, sha256
from reproduce import capture, save
from evaluate import audit, read_csv, require


def common_metrics(directory,common_ids,calibration_ids):
    require(not set(common_ids)&set(calibration_ids),'Common queries overlap calibration pool')
    summary=json.loads((directory/'summary.json').read_text())
    rows=[r for r in read_csv(directory/'queries.csv') if int(r['query_id']) in common_ids]
    require(len(rows)==len(common_ids)*summary['repeats'],'Incomplete common query records')
    require(len({(int(r['query_id']),int(r['repeat'])) for r in rows})==len(rows),'Duplicate common record')
    elapsed=sum(float(r['seconds']) for r in rows)
    require(math.isfinite(elapsed) and elapsed>0,'Invalid common elapsed time')
    return {'query_ids':list(common_ids),'queries':len(common_ids),'timed_records':len(rows),
            'Recall@'+str(summary['k']):sum(float(r['recall']) for r in rows)/len(rows),
            'QPS':len(rows)/elapsed,'mean_verified':sum(int(r['verified']) for r in rows)/len(rows),
            'mean_probed':sum(int(r['probed']) for r in rows)/len(rows)}


def main():
    parser=argparse.ArgumentParser(description='Experimental 0.1M full-recalibration pilot; never changes public task defaults.')
    parser.add_argument('--data-directory',type=Path,default=ROOT/'datasets/SPLADE')
    parser.add_argument('--offline',action='store_true')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--common-queries',type=int,default=64)
    args=parser.parse_args()
    if not 1<=args.common_queries<=512:
        parser.error('Common-query pilot count must be in 1..512')
    docs,queries=prepare('0.1M',args.data_directory,args.offline)
    output=args.out.resolve();output.mkdir(parents=True,exist_ok=False)
    files=sorted((ROOT/'include').rglob('*.hpp'))+sorted((ROOT/'src').glob('*.cpp'))+sorted((ROOT/'scripts').glob('*.py'))+sorted((ROOT/'configs').glob('*.json'))+[ROOT/'CMakeLists.txt']
    hashes={str(p.relative_to(ROOT)):sha256(p) for p in files}
    cases=[(4,100),(8,50),(4,50),(8,100)]
    manifest={'status':'running','started_utc':dt.datetime.now(dt.timezone.utc).isoformat(),
              'git_commit':capture(['git','rev-parse','HEAD']),'git_worktree_status':capture(['git','status','--porcelain']),
              'source_sha256':hashes,'data_sha256':sha256(docs),'queries_sha256':sha256(queries),
              'case_order':cases,'common_queries':args.common_queries,'maximum_calibration_queries':8,
              'cpu':capture(['lscpu']),'compiler':capture(['c++','--version']),
              'scope':'experimental 0.1M; full automatic K/m/B per case; one fresh index each; three timing repeats; common validation IDs disjoint from all calibration IDs; not matched recall or new public defaults'}
    save(output/'manifest.json',manifest)
    try:
        build=ROOT/'build-pilot'
        with (output/'build.log').open('w') as log:
            subprocess.run(['cmake','-S',str(ROOT),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DCABS_SANITIZE=OFF'],stdout=log,stderr=log,check=True)
            subprocess.run(['cmake','--build',str(build),'--target','cabs','-j4'],stdout=log,stderr=log,check=True)
        binary=build/'cabs';manifest['binary_sha256']=sha256(binary);save(output/'manifest.json',manifest)
        common_ids=None;calibration_pool=None;comparisons=[]
        base=json.loads((ROOT/'configs/splade_0.1m.json').read_text())
        for nc,kmax in cases:
            case=output/('nc'+str(nc)+'_kmax'+str(kmax));case.mkdir()
            config=dict(base,calibration_queries=nc,k_max=kmax,evaluation_queries=args.common_queries+8-nc,
                        experimental=True,full_query_run=False,common_validation_queries=args.common_queries,warmup_queries=5+8-nc)
            save(case/'config.json',config)
            command=[str(binary),'run','--data',str(docs),'--queries',str(queries),'--out',str(case),
                     '--ell','24','--delta-h','0.2','--k','50','--kmax',str(kmax),'--calibration-queries',str(nc),
                     '--eval-queries',str(args.common_queries+8-nc),'--seed','20261007','--threads','4',
                     '--iterations','10','--repeats','3','--max-index-gib','9','--warmup-queries',str(5+8-nc),
                     '--keep-sketch-cache','0','--benchmark-exact','0','--diagnostic-queries','0','--equivalence-queries',str(8+8-nc)]
            save(case/'manifest.json',dict(status='running',command=command,source_commit=manifest['git_commit'],binary_sha256=manifest['binary_sha256']))
            with (case/'run.log').open('w') as log:
                subprocess.run(['/usr/bin/time','-v','-o',str(case/'resource_usage.log'),*command],env=dict(os.environ,OMP_NUM_THREADS='4'),stdout=log,stderr=log,check=True)
            metrics=audit(case)
            split=read_csv(case/'query_split.csv')
            cal=[int(r['query_id']) for r in split if r['role']=='calibration']
            evaluation=[int(r['query_id']) for r in split if r['role']=='evaluation']
            current_common=evaluation[8-nc:]
            current_pool=cal+evaluation[:8-nc]
            if common_ids is None:common_ids=current_common;calibration_pool=current_pool
            require(common_ids==current_common and calibration_pool==current_pool,'Common validation split changed')
            common=common_metrics(case,common_ids,calibration_pool);save(case/'common_metrics.json',common)
            automatic=json.loads((case/'automatic_configuration.json').read_text())
            comparison={'calibration_queries':nc,'k_max':kmax,'K':automatic['K'],'B':automatic['B'],
                        'min_m':min(c['m'] for c in automatic['clusters']),'max_m':max(c['m'] for c in automatic['clusters']),
                        'Recall@50':common['Recall@50'],'QPS':common['QPS'],'IT_seconds':metrics['IT_seconds'],
                        'IS_GB':metrics['IS_GB'],'mean_verified':common['mean_verified'],'mean_probed':common['mean_probed']}
            comparisons.append(comparison)
            save(case/'manifest.json',dict(status='completed',command=command,source_commit=manifest['git_commit'],binary_sha256=manifest['binary_sha256']))
            save(case/'complete.json',{p.name:sha256(p) for p in case.iterdir() if p.is_file() and p.name!='complete.json'})
            save(output/'comparison.json',{'status':'in_progress','scope':manifest['scope'],'cases':comparisons})
        require(all(sha256(ROOT/name)==value for name,value in hashes.items()),'Source changed during pilot')
        require(sha256(binary)==manifest['binary_sha256'] and sha256(docs)==manifest['data_sha256'] and sha256(queries)==manifest['queries_sha256'],'Binary or inputs changed during pilot')
        with (output/'comparison.csv').open('w',newline='') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(comparisons[0]));writer.writeheader();writer.writerows(comparisons)
        save(output/'comparison.json',{'status':'completed','scope':manifest['scope'],'cases':comparisons,
                                      'common_validation_query_ids':common_ids,'calibration_pool_query_ids':calibration_pool})
        manifest.update(status='completed',finished_utc=dt.datetime.now(dt.timezone.utc).isoformat());save(output/'manifest.json',manifest)
        save(output/'complete.json',{p.name:sha256(p) for p in output.iterdir() if p.is_file() and p.name!='complete.json'})
        print(json.dumps(comparisons,indent=2))
    except BaseException as error:
        manifest.update(status='failed',error=str(error));save(output/'manifest.json',manifest)
        raise


if __name__=='__main__':
    main()
