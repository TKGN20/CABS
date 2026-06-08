#!/usr/bin/env python3
import argparse, csv

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--summary_csv', required=True)
    args=ap.parse_args()
    with open(args.summary_csv) as f:
        rd=csv.DictReader(f)
        rows=list(rd)
    print('dataset,method,config_id,recall50,qps,avg_full_eval,avg_coarse_touch')
    for r in rows:
        print(','.join([r['dataset'],r['method'],r['config_id'],r['recall50'],r['qps'],r['avg_full_eval'],r['avg_coarse_touch']]))

if __name__=='__main__':
    main()
