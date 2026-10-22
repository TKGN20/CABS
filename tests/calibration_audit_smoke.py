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
from audit_calibration import summarize


def main(binary):
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
        print(json.dumps({'status':'PASS','candidate_configurations':len(observation['candidates']),
                          'profile_queries':len(profiles),'B_and_query_records_recomputed':True}))


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    main(parser.parse_args().binary.resolve())
