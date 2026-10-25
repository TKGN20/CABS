#!/usr/bin/env python3

import argparse
import csv
import gzip
import itertools
import json
import math
from pathlib import Path
import statistics


def csv_rows(path):
    path=Path(path)
    if path.exists():
        stream=path.open(newline='',encoding='utf-8')
    else:
        stream=gzip.open(str(path)+'.gz','rt',newline='',encoding='utf-8')
    with stream:
        yield from csv.DictReader(stream)


def routing_summary(directory):
    rows=list(csv_rows(Path(directory)/'routing_bounds.csv'))
    if not rows:
        raise ValueError('Empty routing trace')
    by_query={}
    for row in rows:
        by_query.setdefault(int(row['query_id']),[]).append(row)
        upper=float(row['cluster_upper']);suffix=float(row['suffix_upper'])
        theta=float(row['centroid_angle']);radius=float(row['cover_radius'])
        factor=float(row['angular_factor']);cap=float(row['norm_cap'])
        if not math.isclose(factor,math.cos(max(0,theta-radius)),rel_tol=1e-12,abs_tol=1e-12):
            raise ValueError('Angular factor mismatch')
        if not math.isclose(upper,cap*factor,rel_tol=1e-11,abs_tol=1e-10) or suffix+1e-10<upper:
            raise ValueError('Invalid routing upper bound')
        if row['cluster_exact_max'] and float(row['cluster_exact_max'])>upper:
            raise ValueError('Upper bound below enumerated cluster maximum')
        if int(row['stopped']) and (not row['gamma_before'] or not suffix<float(row['gamma_before'])):
            raise ValueError('Invalid stopping certificate')
    for sequence in by_query.values():
        if [int(r['route_position']) for r in sequence]!=list(range(len(sequence))):
            raise ValueError('Noncontiguous route trace')
    factors=[float(r['angular_factor']) for r in rows]
    ratios=[float(r['suffix_upper'])/float(r['gamma_before']) for r in rows if r['gamma_before'] and float(r['gamma_before'])>0]
    exact=[float(r['cluster_upper'])/float(r['cluster_exact_max']) for r in rows if r['cluster_exact_max'] and float(r['cluster_exact_max'])>0]
    return {'queries':len(by_query),'trace_rows':len(rows),'stop_events':sum(int(r['stopped']) for r in rows),
            'angular_factor_min':min(factors),'angular_factor_median':statistics.median(factors),
            'fraction_angular_factor_one':sum(abs(v-1)<1e-14 for v in factors)/len(factors),
            'suffix_over_gamma_min':min(ratios) if ratios else None,
            'suffix_over_gamma_median':statistics.median(ratios) if ratios else None,
            'exact_cluster_maximum_checks':len(exact),
            'upper_over_exact_max_min':min(exact) if exact else None,
            'upper_over_exact_max_median':statistics.median(exact) if exact else None,
            'cover_radius_min':min(float(r['cover_radius']) for r in rows),
            'cover_radius_max':max(float(r['cover_radius']) for r in rows),
            'scope':'selected-index subset diagnostic; exact cluster scans and traces excluded from QPS'}


def frozen_budget_analysis(target_directory,complete_directory):
    targets=list(csv_rows(Path(target_directory)/'pool_targets.csv'))
    ids=sorted({int(r['query_id']) for r in targets})
    wanted={(int(r['query_id']),int(r['target_id'])) for r in targets};ranks={}
    for row in csv_rows(Path(complete_directory)/'ground_truth.csv'):
        key=(int(row['query_id']),int(row['identifier']))
        if key in wanted:ranks[key]=int(row['rank'])
        if len(ranks)==len(wanted):break
    if len(ids)!=4 or len(ranks)!=len(wanted):
        raise ValueError('Expected four fully recorded calibration queries')
    result=[]
    for count in range(1,5):
        for subset in itertools.combinations(ids,count):
            for kmax in (50,100):
                retained=[r for r in targets if int(r['query_id']) in subset and ranks[(int(r['query_id']),int(r['target_id']))]<=kmax and int(r['positive_overlap']) and int(r['touched'])]
                driver=max(retained,key=lambda r:int(r['minimum_prefix_rank']))
                result.append({'calibration_count':count,'query_ids':';'.join(map(str,subset)),'target_kmax':kmax,
                               'B_frozen_index':max([kmax]+[int(r['minimum_prefix_rank']) for r in retained]),
                               'driver_query':int(driver['query_id']),'driver_id':int(driver['target_id']),
                               'driver_exact_rank':ranks[(int(driver['query_id']),int(driver['target_id']))]})
    return {'scope':'frozen m, partition, hash and proxy-prefix ranks; no full recalibration, QPS, Recall or confidence interval',
            'input_query_ids':ids,'rows':result}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description='Read-only routing and frozen-budget diagnostics.')
    parser.add_argument('mode',choices=['routing','frozen-b'])
    parser.add_argument('directory',type=Path)
    parser.add_argument('--complete-record',type=Path)
    args=parser.parse_args()
    if args.mode=='routing':
        result=routing_summary(args.directory)
    else:
        if args.complete_record is None:parser.error('frozen-b requires --complete-record')
        result=frozen_budget_analysis(args.directory,args.complete_record)
    print(json.dumps(result,indent=2))
