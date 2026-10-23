#!/usr/bin/env python3

import argparse
import csv
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile

from smoke_test import write_csr

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from audit_calibration import summarize, match_recorded_counters
from evaluate import audit, read_csv


def main(binary,core):
    rng=random.Random(84691)
    with tempfile.TemporaryDirectory(prefix='cabs-calibration-audit-') as temporary:
        directory=Path(temporary)
        def make_row():
            return {j:rng.uniform(.1,1) for j in sorted(rng.sample(range(32),8))}
        write_csr(directory/'base.csr',[make_row() for _ in range(120)],32)
        write_csr(directory/'queries.csr',[make_row() for _ in range(16)],32)
        result=directory/'result';result.mkdir()
        run=subprocess.run([str(binary),str(directory/'base.csr'),str(directory/'queries.csr'),str(result),'3'],capture_output=True,text=True)
        if run.returncode:
            raise RuntimeError(run.stdout+run.stderr)
        observation=summarize(result,3)
        if len(observation['candidates'])<2 or not observation['not_a_default_QPS_result']:
            raise ValueError('Audit scope was not preserved')
        with (result/'profile.csv').open() as stream:
            profiles=list(csv.DictReader(stream))
        if len(profiles)!=3*len(observation['candidates']):
            raise ValueError('Incomplete separate profiling records')
        for row in profiles:
            for key,value in row.items():
                if key not in ('K','query_id') and (not math.isfinite(float(value)) or float(value)<0):
                    raise ValueError('Invalid profile time')
        complete=directory/'complete';complete.mkdir()
        command=[str(core),'run','--data',str(directory/'base.csr'),'--queries',str(directory/'queries.csr'),
                 '--out',str(complete),'--ell','24','--delta-h','0.2','--k','50','--kmax','100',
                 '--calibration-queries','4','--eval-queries','3','--seed','20261007',
                 '--threads','4','--iterations','10','--repeats','2','--max-index-gib','9',
                 '--keep-sketch-cache','0','--benchmark-exact','0','--diagnostic-queries','0','--equivalence-queries','3']
        run=subprocess.run(command,capture_output=True,text=True)
        if run.returncode:
            raise RuntimeError(run.stdout+run.stderr)
        audit(complete)
        selected=json.loads((complete/'automatic_configuration.json').read_text())
        replay=directory/'replay';replay.mkdir()
        run=subprocess.run([str(binary),str(directory/'base.csr'),str(directory/'queries.csr'),str(replay),'3',str(selected['K']),str(selected['B']),'routing'],capture_output=True,text=True)
        if run.returncode:
            raise RuntimeError(run.stdout+run.stderr)
        summarize(replay,3)
        traced=read_csv(replay/'routing_bounds.csv')
        if {int(r['query_id']) for r in traced}!={int(r['query_id']) for r in read_csv(replay/'full_results.csv')}:
            raise ValueError('Routing trace omitted queries')
        for row in traced:
            if float(row['cluster_exact_max'])>float(row['cluster_upper']):
                raise ValueError('Routing bound below exact cluster maximum')
            if int(row['stopped']) and not float(row['suffix_upper'])<float(row['gamma_before']):
                raise ValueError('Invalid strict stopping trace')
        expected={}
        for row in read_csv(complete/'retrieved.csv'):
            expected.setdefault(int(row['query_id']),[]).append((int(row['identifier']),float(row['score'])))
        observed={}
        for row in read_csv(replay/'full_results.csv'):
            observed.setdefault(int(row['query_id']),[]).append((int(row['identifier']),float(row['score'])))
        if observed!=expected:
            raise ValueError('Selected-index audit differs from the full production result')
        records={int(r['query_id']):r for r in read_csv(complete/'queries.csv') if int(r['repeat'])==0}
        for row in read_csv(replay/'full_queries.csv'):
            if row['split']=='evaluation':
                match_recorded_counters(row,records[int(row['query_id'])])
        print(json.dumps({'status':'PASS','candidate_configurations':len(observation['candidates']),
                          'profile_queries':len(profiles),'B_and_query_records_recomputed':True,
                          'selected_final_index_production_results_equal':True}))


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--core-binary',type=Path,required=True)
    args=parser.parse_args()
    main(args.binary.resolve(),args.core_binary.resolve())
