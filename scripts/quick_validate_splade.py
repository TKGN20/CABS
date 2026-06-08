#!/usr/bin/env python3
import argparse
import csv
import json
import math
import os
import shutil
import subprocess
import time
from multiprocessing import Pool, cpu_count
from pathlib import Path

ROOT = Path('/home/songyang/TKGN/CABS')
BIN = ROOT / 'sos'
OUT = ROOT / 'results' / 'quick_validate'

TARGETS = [0.90,0.91,0.92,0.93,0.94,0.95,0.96,0.97,0.98,0.99]
CABS_NPROBE_SMOKE = {1,3}
CABS_NPROBE_FULL = {1,3,5,8}
CABS_ETA_SMOKE = [0.01,0.03]
CABS_ETA_FULL = [0.01,0.02,0.03]
SOSIA_T_SMOKE = {5000,50000}
SOSIA_T_FULL = {2000,5000,10000,20000,50000}


def run_cmd(cmd, cwd=None, env=None, timeout=None):
    st = time.time()
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout)
    return p.returncode, p.stdout, time.time() - st


def dataset_map():
    d = {
        'SPLADE-0.1M': {
            'base': ROOT/'datasets/MS MARCO v1/SPLADE/0.1M/base_small.csr',
            'query': ROOT/'datasets/MS MARCO v1/SPLADE/0.1M/queries.dev.csr',
            'ben': ROOT/'datasets/MS MARCO v1/SPLADE/0.1M/base_small_all.ben',
            'q_fallback': 256,
        },
        'SPLADE-1M': {
            'base': ROOT/'datasets/MS MARCO v1/SPLADE/1M/base_1M.csr',
            'query': ROOT/'datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr',
            'ben': ROOT/'datasets/MS MARCO v1/SPLADE/1M/base_1M_all.ben',
            'q_fallback': 128,
        },
    }
    p10 = ROOT/'datasets/MS MARCO v1/SPLADE/10M/base_full.csr'
    if p10.exists():
        d['SPLADE-10M'] = {
            'base': p10,
            'query': ROOT/'datasets/MS MARCO v1/SPLADE/10M/queries.dev.csr',
            'ben': ROOT/'datasets/MS MARCO v1/SPLADE/10M/base_full_all.ben',
            'q_fallback': 64,
            'mapped_from': 'SPLADE-10M'
        }
    else:
        alt = ROOT/'datasets/MS MARCO v1/SPLADE/FULL/base_full.csr'
        d['SPLADE-10M'] = {
            'base': alt,
            'query': ROOT/'datasets/MS MARCO v1/SPLADE/FULL/queries.dev.csr',
            'ben': ROOT/'datasets/MS MARCO v1/SPLADE/FULL/base_full_all.ben',
            'q_fallback': 64,
            'mapped_from': 'SPLADE-FULL'
        }
    return d


def probe():
    ds = dataset_map()
    rows = []
    for k,v in ds.items():
        rows.append({
            'dataset': k,
            'base_exists': v['base'].exists(),
            'query_exists': v['query'].exists(),
            'ben_exists': v['ben'].exists(),
            'base': str(v['base']),
            'query': str(v['query']),
            'ben': str(v['ben']),
        })
    return rows


def ensure_ben_one(name, cfg):
    if cfg['ben'].exists():
        return True, 'existing'
    # trigger ben creation by mode11 run with only SOSIA and tiny subset
    warm = OUT/'raw_warmup'/name
    warm.mkdir(parents=True, exist_ok=True)
    conf = {
        'output_dir': str(warm),
        'topk': 50,
        'random_seed': 42,
        'query_subset_size': 8,
        'methods': {'LinearScan': False, 'SOSIA': True, 'WAND': False, 'BinSketch': False, 'CABS': False},
        'datasets': [{'name': name, 'base_path': str(cfg['base']), 'query_path': str(cfg['query']), 'groundtruth_path': str(cfg['ben'])}],
    }
    j = warm/'warmup.json'
    j.write_text(json.dumps(conf))
    rc,out,_ = run_cmd([str(BIN), '11', str(j)], cwd=str(ROOT), timeout=7200)
    (warm/'warmup.log').write_text(out)
    return cfg['ben'].exists() and rc == 0, 'generated' if cfg['ben'].exists() else 'failed'


def write_placeholder_config_files(ds, method, config_id, qn, wall_time, eff_params, truth_source):
    cdir = OUT/ds/method/config_id
    cdir.mkdir(parents=True, exist_ok=True)
    with (cdir/'stats.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['query_id','latency_us','full_eval_count'])
        for i in range(qn):
            w.writerow([i, 'NA', 'NA'])
    with (cdir/'results.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['query_id','rank','doc_id','score'])
    with (cdir/'meta.json').open('w') as f:
        json.dump({
            'dataset': ds,
            'method': method,
            'config_id': config_id,
            'topk': 50,
            'seed': 42,
            'query_count': qn,
            'effective_params': eff_params,
            'truth_source': truth_source,
            'wall_time_sec': wall_time,
        }, f, indent=2)


def parse_curves(curve_file):
    out = []
    if not curve_file.exists():
        return out
    with curve_file.open() as f:
        rd = csv.DictReader(f)
        out.extend(rd)
    return out


def one_job(job):
    ds_name, ds_cfg, eta, qn, smoke = job
    run_dir = OUT/'raw'/ds_name/f'eta_{eta}'
    run_dir.mkdir(parents=True, exist_ok=True)
    conf = {
        'output_dir': str(run_dir),
        'topk': 50,
        'random_seed': 42,
        'query_subset_size': qn,
        'cabs_eta': eta,
        'methods': {'LinearScan': False, 'SOSIA': True, 'WAND': False, 'BinSketch': False, 'CABS': True},
        'datasets': [{'name': ds_name, 'base_path': str(ds_cfg['base']), 'query_path': str(ds_cfg['query']), 'groundtruth_path': str(ds_cfg['ben'])}],
    }
    cfg_path = run_dir/'config.json'
    cfg_path.write_text(json.dumps(conf))
    rc,out,dt = run_cmd([str(BIN), '11', str(cfg_path)], cwd=str(ROOT), timeout=21600)
    (run_dir/'run.log').write_text(out)
    return {'rc': rc, 'run_dir': str(run_dir), 'dataset': ds_name, 'eta': eta, 'wall': dt, 'query_count': qn}


def fmt_compact(qps, afe):
    if qps == 'N/A':
        return 'N/A'
    q = int(round(float(qps)))
    a = float(afe)
    if a >= 1000:
        s = f"{a/1000.0:.1f}k"
    else:
        s = f"{a:.1f}" if abs(a-int(a))>1e-6 else str(int(a))
    return f"{q} ({s})"


def aggregate(ds_list, smoke=False):
    summary_rows = []
    for ds in ds_list:
        for eta in (CABS_ETA_SMOKE if smoke else CABS_ETA_FULL):
            run_dir = OUT/'raw'/ds/f'eta_{eta}'
            curves = parse_curves(run_dir/f'curves_{ds}.csv')
            # load wall
            wall = 0.0
            try:
                wall = (run_dir/'run.log').stat().st_mtime - (run_dir/'run.log').stat().st_ctime
            except Exception:
                wall = 0.0
            # query count from config
            qn = -1
            try:
                qn = json.loads((run_dir/'config.json').read_text()).get('query_subset_size', -1)
            except Exception:
                pass
            for r in curves:
                m = r['method']
                val = int(float(r['control_param_value']))
                rec = float(r['recall'])
                qps = float(r['qps'])
                afe = float(r['avg_verified_candidates'])
                if m == 'CABS' and val in (CABS_NPROBE_SMOKE if smoke else CABS_NPROBE_FULL):
                    cfgid = f'eta{eta}_nprobe{val}'
                    summary_rows.append([ds,'CABS',cfgid,rec,qps,afe,qn,wall])
                    write_placeholder_config_files(ds,'CABS',cfgid,qn,wall,{'eta':eta,'nprobe':val},'ben')
                if m == 'SOSIA' and val in (SOSIA_T_SMOKE if smoke else SOSIA_T_FULL):
                    cfgid = f'T{val}'
                    summary_rows.append([ds,'SOSIA',cfgid,rec,qps,afe,qn,wall])
                    write_placeholder_config_files(ds,'SOSIA',cfgid,qn,wall,{'T':val},'ben')

    # dedup keep max qps per (dataset,method,config)
    best = {}
    for r in summary_rows:
        k = (r[0],r[1],r[2])
        if k not in best or r[4] > best[k][4]:
            best[k] = r
    rows = list(best.values())

    with (OUT/'summary.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['dataset','method','config_id','recall50','qps','avg_full_eval','query_count','wall_time_sec'])
        w.writerows(sorted(rows))

    best_rows = []
    for ds in ds_list:
        for m in ['CABS','SOSIA']:
            cand = [r for r in rows if r[0]==ds and r[1]==m]
            for t in TARGETS:
                ok = [r for r in cand if r[3] >= t]
                if not ok:
                    best_rows.append([ds,m,t,'N/A','N/A','N/A','N/A','N/A'])
                else:
                    b = max(ok, key=lambda x:x[4])
                    best_rows.append([ds,m,t,b[2],b[3],b[4],b[5],fmt_compact(b[4],b[5])])

    with (OUT/'best_by_target.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['dataset','method','target_recall','best_config_id','recall50','qps','avg_full_eval','compact_cell'])
        w.writerows(best_rows)

    # paper table
    panels = [(0.90,0.95,'Panel A: 0.90~0.95'),(0.96,0.99,'Panel B: 0.96~0.99')]
    lines = ['# paper_table','', '表A：多规模 SPLADE 数据集下的在线效率比较（QPS (AvgFullEval)）','']
    for lo,hi,title in panels:
        lines += [f'## {title}','', '| Method | Dataset | ' + ' | '.join([f'{x:.2f}' for x in TARGETS if lo<=x<=hi]) + ' |', '|---|---|' + '---|'*len([x for x in TARGETS if lo<=x<=hi])]
        for m in ['CABS','SOSIA']:
            for ds in ds_list:
                vals=[]
                for t in TARGETS:
                    if not (lo<=t<=hi):
                        continue
                    row = next((r for r in best_rows if r[0]==ds and r[1]==m and abs(float(r[2])-t)<1e-9), None)
                    vals.append(row[7] if row else 'N/A')
                lines.append('| ' + ' | '.join([m, ds] + vals) + ' |')
        lines.append('')
    (OUT/'paper_table.md').write_text('\n'.join(lines))

    return rows, best_rows


def report(ds_rows, smoke, ds_map_rows, degraded):
    lines=['# quick_validate_report','']
    lines.append('## Run status')
    lines.append(f'- smoke_mode: {smoke}')
    lines.append('- methods: CABS, SOSIA')
    lines.append('- metrics: Recall@50, QPS, AvgFullEval')
    lines.append('')
    lines.append('## Dataset mapping and truth')
    lines.append('| dataset | base_exists | query_exists | ben_exists | base | ben |')
    lines.append('|---|---:|---:|---:|---|---|')
    for r in ds_map_rows:
        lines.append(f"| {r['dataset']} | {int(r['base_exists'])} | {int(r['query_exists'])} | {int(r['ben_exists'])} | {r['base']} | {r['ben']} |")
    lines.append('')
    lines.append('## Parameter mapping')
    lines.append('- external nprobe -> actual parameter control_param_name=nprobe in curves_<dataset>.csv')
    lines.append('- external eta -> actual config key cabs_eta parsed by mode11')
    lines.append('- external T -> actual SOSIA control_param_name=ub in curves_<dataset>.csv')
    lines.append('')
    if degraded:
        lines.append('## Degradation applied')
        for x in degraded:
            lines.append(f'- {x}')
        lines.append('')
    lines.append('## Notes')
    lines.append('- AvgFullEval is mapped from avg_verified_candidates reported by engine.')
    lines.append('- AvgCoarseTouch is intentionally omitted for this quick paper-table run.')
    (OUT/'quick_validate_report.md').write_text('\n'.join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--smoke', action='store_true')
    ap.add_argument('--workers', type=int, default=0)
    ap.add_argument('--force_subset', action='store_true')
    args = ap.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'oracle').mkdir(exist_ok=True)

    ds = dataset_map()
    ds_names = ['SPLADE-0.1M','SPLADE-1M','SPLADE-10M']
    ds_probe = probe()

    # ensure ben
    degraded=[]
    for n in ds_names:
        ok,src = ensure_ben_one(n, ds[n])
        if not ok:
            print(f'[ERROR] ben unavailable for {n}: {ds[n]["ben"]}')
            return 2
        (OUT/'oracle'/f'{n}.truth.csv').write_text(f'note,reused_ben,{ds[n]["ben"]}\n')

    # build jobs
    jobs=[]
    if args.smoke:
        for n in ['SPLADE-0.1M','SPLADE-1M']:
            for eta in CABS_ETA_SMOKE:
                jobs.append((n, ds[n], eta, 8, True))
    else:
        for n in ds_names:
            qn = -1
            if args.force_subset:
                qn = ds[n]['q_fallback']
                degraded.append(f'{n}: forced subset query_count={qn}, seed=42')
            for eta in CABS_ETA_FULL:
                jobs.append((n, ds[n], eta, qn, False))

    max_workers = max(1, int(math.floor(0.7 * cpu_count())))
    workers = args.workers if args.workers > 0 else min(max_workers, 3)

    print(f'[RUN] jobs={len(jobs)} workers={workers} (max_allowed={max_workers})')
    print('[RUN] command template: ./sos 11 <config.json>')

    if workers <= 1:
        for j in jobs:
            r = one_job(j)
            print(f"[JOB] dataset={r['dataset']} eta={r['eta']} rc={r['rc']} wall={r['wall']:.1f}s")
    else:
        with Pool(processes=workers) as p:
            for r in p.imap_unordered(one_job, jobs):
                print(f"[JOB] dataset={r['dataset']} eta={r['eta']} rc={r['rc']} wall={r['wall']:.1f}s")

    rows, best_rows = aggregate(['SPLADE-0.1M','SPLADE-1M','SPLADE-10M'], smoke=args.smoke)
    report(rows, args.smoke, ds_probe, degraded)
    print('[DONE] summary=', OUT/'summary.csv')
    print('[DONE] best_by_target=', OUT/'best_by_target.csv')
    print('[DONE] paper_table=', OUT/'paper_table.md')
    print('[DONE] report=', OUT/'quick_validate_report.md')


if __name__ == '__main__':
    raise SystemExit(main())
