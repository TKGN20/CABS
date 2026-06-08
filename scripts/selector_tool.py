#!/usr/bin/env python3
import argparse, csv
TARGETS=[0.90,0.93,0.95,0.97]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--summary_csv', required=True)
    args=ap.parse_args()
    with open(args.summary_csv) as f:
        rows=list(csv.DictReader(f))
    datasets=sorted(set(r['dataset'] for r in rows))
    for ds in datasets:
        print(f'[{ds}]')
        for m in ['CABS','SOSIA']:
            cand=[r for r in rows if r['dataset']==ds and r['method']==m]
            for t in TARGETS:
                ok=[r for r in cand if float(r['recall50'])>=t]
                if not ok:
                    print(f'  {m} @ {t:.2f}: N/A')
                else:
                    b=max(ok,key=lambda x:float(x['qps']))
                    print(f"  {m} @ {t:.2f}: {float(b['qps']):.2f} ({float(b['avg_full_eval']):.1f}) cfg={b['config_id']}")

if __name__=='__main__':
    main()
