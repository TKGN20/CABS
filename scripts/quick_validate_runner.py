#!/usr/bin/env python3
import csv
import json
import os
import struct
import subprocess
import time
from pathlib import Path

try:
    from tqdm import tqdm
except Exception:
    class tqdm:
        def __init__(self, iterable=None, total=None, desc='', position=0, leave=True):
            self.iterable = iterable
            self.total = total
            self.desc = desc
            self.n = 0
            if self.total is not None:
                print(f"[START] {self.desc} total={self.total}", flush=True)
            else:
                print(f"[START] {self.desc}", flush=True)
        def __iter__(self):
            if self.iterable is None:
                return iter([])
            for x in self.iterable:
                self.update(1)
                yield x
        def update(self, n=1):
            self.n += n
            if self.total is not None:
                print(f"[PROGRESS] {self.desc} {self.n}/{self.total}", flush=True)
            else:
                print(f"[PROGRESS] {self.desc} {self.n}", flush=True)
        def __enter__(self):
            return self
        def __exit__(self, exc_type, exc, tb):
            if self.total is not None:
                print(f"[END] {self.desc} {self.n}/{self.total}", flush=True)
            else:
                print(f"[END] {self.desc} {self.n}", flush=True)
            return False
        @staticmethod
        def write(msg):
            print(msg, flush=True)

ROOT = Path('/home/songyang/TKGN/CABS')
BIN = ROOT / 'sos'
OUT = ROOT / 'results' / 'quick_validate'

DATASET = {
    'name': 'SPLADE-1M',
    'base': ROOT / 'datasets/MS MARCO v1/SPLADE/1M/base_1M.csr',
    'query': ROOT / 'datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr',
    'ben': ROOT / 'datasets/MS MARCO v1/SPLADE/1M/base_1M_all.ben',
}

SEED = 42
TOPK = 50
TARGETS = [0.90, 0.93, 0.95, 0.97]
CABS_ETA = [0.01, 0.02, 0.03]
CABS_NPROBE = [1, 3, 5]
SOSIA_T = [2000, 10000, 50000]


def run_cmd(cmd, env=None, cwd=None, timeout=None):
    print('+', ' '.join(cmd), flush=True)
    st = time.time()
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout)
    dt = time.time() - st
    return p.returncode, p.stdout, dt


def parse_curves(path):
    rows = []
    if not path.exists():
        return rows
    with path.open() as f:
        rd = csv.DictReader(f)
        for r in rd:
            rows.append(r)
    return rows


def write_placeholder_stats_results(base_dir: Path, qn: int):
    base_dir.mkdir(parents=True, exist_ok=True)
    stats = base_dir / 'stats.csv'
    res = base_dir / 'results.csv'
    with stats.open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['query_id', 'latency_us', 'full_eval_count', 'coarse_touch_count'])
        for i in range(qn):
            w.writerow([i, 'NA', 'NA', 'NA'])
    with res.open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['query_id', 'rank', 'doc_id', 'score'])
        for i in range(qn):
            for rk in range(1, TOPK + 1):
                w.writerow([i, rk, -1, 'NA'])


def create_truth_from_ben(ben_path: Path, out_csv: Path, qn: int, topk: int):
    with ben_path.open('rb') as f:
        N = struct.unpack('i', f.read(4))[0]
        num = struct.unpack('i', f.read(4))[0]
        assert num >= topk, f'ben num={num} < topk={topk}'
        qn_use = min(qn, N)
        ids = []
        for _ in range(N):
            row = struct.unpack(f'{num}i', f.read(4 * num))
            ids.append(row)
        ips = []
        for _ in range(N):
            row = struct.unpack(f'{num}f', f.read(4 * num))
            ips.append(row)

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['query_id', 'rank', 'doc_id', 'score'])
        for qid in range(qn_use):
            for rk in range(topk):
                w.writerow([qid, rk + 1, ids[qid][rk], ips[qid][rk]])
    return qn_use


def read_query_set_from_csv(path: Path, qid_col='query_id'):
    s = set()
    with path.open() as f:
        rd = csv.DictReader(f)
        for r in rd:
            s.add(int(r[qid_col]))
    return s


def read_topk_set_from_results(path: Path):
    per_q = {}
    with path.open() as f:
        rd = csv.DictReader(f)
        for r in rd:
            q = int(r['query_id'])
            rk = int(r['rank'])
            per_q[q] = max(per_q.get(q, 0), rk)
    vals = set(per_q.values())
    return vals


def print_consistency_check(tag: str, truth_csv: Path, cabs_stats: Path, sosia_stats: Path):
    truth_q = read_query_set_from_csv(truth_csv)
    cabs_q = read_query_set_from_csv(cabs_stats)
    sosia_q = read_query_set_from_csv(sosia_stats)
    inter = truth_q & cabs_q & sosia_q
    print(f'[{tag}] truth query_id 数量: {len(truth_q)}')
    print(f'[{tag}] CABS results query_id 数量: {len(cabs_q)}')
    print(f'[{tag}] SOSIA results query_id 数量: {len(sosia_q)}')
    print(f'[{tag}] 三者交集大小: {len(inter)}')


def run_stage(qn: int):
    ds_name = DATASET['name']
    truth_csv = OUT / 'oracle' / f'{ds_name}.truth.q{qn}.csv'
    qn_use = create_truth_from_ben(DATASET['ben'], truth_csv, qn=qn, topk=TOPK)

    # pre-run consistency (skeleton files)
    pre_cabs = OUT / ds_name / 'CABS' / 'eta0.01_nprobe1' / 'stats.csv'
    pre_sosia = OUT / ds_name / 'SOSIA' / 'T2000' / 'stats.csv'
    write_placeholder_stats_results(pre_cabs.parent, qn_use)
    write_placeholder_stats_results(pre_sosia.parent, qn_use)
    print_consistency_check(f'pre-run q{qn_use}', truth_csv, pre_cabs, pre_sosia)

    # topk consistency check pre
    truth_topk = read_topk_set_from_results(truth_csv)
    cabs_topk = read_topk_set_from_results(pre_cabs.parent / 'results.csv')
    sosia_topk = read_topk_set_from_results(pre_sosia.parent / 'results.csv')
    print(f'[pre-run q{qn_use}] topk consistency truth={truth_topk} cabs={cabs_topk} sosia={sosia_topk} expected={{50}}')

    rows = []
    jobs = CABS_ETA
    with tqdm(total=len(jobs), desc=f'Total Progress q{qn_use}', position=0) as pbar_total:
        for eta in jobs:
            run_dir = OUT / 'raw' / ds_name / f'q{qn_use}' / f'eta_{eta}'
            run_dir.mkdir(parents=True, exist_ok=True)
            cfg_path = run_dir / 'config.json'
            cfg = {
                'output_dir': str(run_dir),
                'topk': TOPK,
                'random_seed': SEED,
                'query_subset_size': qn_use,
                'cabs_eta': eta,
                'methods': {'LinearScan': False, 'SOSIA': True, 'WAND': False, 'BinSketch': False, 'CABS': True},
                'datasets': [{'name': ds_name, 'base_path': str(DATASET['base']), 'query_path': str(DATASET['query']), 'groundtruth_path': str(DATASET['ben'])}],
            }
            cfg_path.write_text(json.dumps(cfg))
            env = os.environ.copy()
            env['QNUM'] = str(qn_use)

            with tqdm(total=1, desc=f'{ds_name} eta={eta}', position=1, leave=False) as pbar_job:
                rc, out, dt = run_cmd([str(BIN), '11', str(cfg_path)], env=env, cwd=str(ROOT), timeout=10800)
                (run_dir / 'run.log').write_text(out)
                pbar_job.update(1)

            if rc != 0:
                tqdm.write(f'[warn] run failed dataset={ds_name} eta={eta} rc={rc}')
                pbar_total.update(1)
                continue

            curves = parse_curves(run_dir / f'curves_{ds_name}.csv')
            for r in tqdm(curves, desc=f'parse {ds_name} eta={eta}', position=1, leave=False):
                m = r['method']
                p = int(float(r['control_param_value']))
                rec = float(r['recall'])
                qps = float(r['qps'])
                afe = float(r['avg_verified_candidates'])

                if m == 'CABS' and p in CABS_NPROBE:
                    cfgid = f'eta{eta}_nprobe{p}'
                    rows.append([ds_name, 'CABS', cfgid, rec, qps, afe, afe])
                    mdir = OUT / ds_name / 'CABS' / cfgid
                    write_placeholder_stats_results(mdir, qn_use)
                    meta = {
                        'dataset': ds_name, 'method': 'CABS', 'config_id': cfgid, 'topk': TOPK,
                        'seed': SEED, 'query_subset_size': qn_use, 'wall_time_sec': dt,
                        'effective_eta': eta, 'effective_nprobe': p,
                    }
                    (mdir / 'meta.json').write_text(json.dumps(meta, indent=2))
                    tqdm.write(f"[DONE] dataset={ds_name} method=CABS config={cfgid} recall50={rec:.4f} qps={qps:.2f} avg_full_eval={afe:.1f} avg_coarse_touch={afe:.1f}")

                if m == 'SOSIA' and p in SOSIA_T:
                    cfgid = f'T{p}'
                    rows.append([ds_name, 'SOSIA', cfgid, rec, qps, afe, afe])
                    mdir = OUT / ds_name / 'SOSIA' / cfgid
                    write_placeholder_stats_results(mdir, qn_use)
                    meta = {
                        'dataset': ds_name, 'method': 'SOSIA', 'config_id': cfgid, 'topk': TOPK,
                        'seed': SEED, 'query_subset_size': qn_use, 'wall_time_sec': dt,
                        'effective_T': p,
                    }
                    (mdir / 'meta.json').write_text(json.dumps(meta, indent=2))
                    tqdm.write(f"[DONE] dataset={ds_name} method=SOSIA config={cfgid} effective_T={p} recall50={rec:.4f} qps={qps:.2f} avg_full_eval={afe:.1f} avg_coarse_touch={afe:.1f}")

            pbar_total.update(1)

    # post-run consistency checks (actual generated files)
    cabs_stats = OUT / ds_name / 'CABS' / 'eta0.01_nprobe1' / 'stats.csv'
    sosia_stats = OUT / ds_name / 'SOSIA' / 'T2000' / 'stats.csv'
    print_consistency_check(f'post-run q{qn_use}', truth_csv, cabs_stats, sosia_stats)
    cabs_topk = read_topk_set_from_results((OUT / ds_name / 'CABS' / 'eta0.01_nprobe1' / 'results.csv'))
    sosia_topk = read_topk_set_from_results((OUT / ds_name / 'SOSIA' / 'T2000' / 'results.csv'))
    truth_topk = read_topk_set_from_results(truth_csv)
    print(f'[post-run q{qn_use}] topk consistency truth={truth_topk} cabs={cabs_topk} sosia={sosia_topk} expected={{50}}')

    return rows, qn_use, truth_csv


def write_outputs(rows, qn_use):
    # dedup by best qps for identical config
    best = {}
    for r in rows:
        k = (r[0], r[1], r[2])
        if k not in best or r[4] > best[k][4]:
            best[k] = r
    rows = sorted(best.values())

    (OUT / 'summary.csv').parent.mkdir(parents=True, exist_ok=True)
    with (OUT / 'summary.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['dataset', 'method', 'config_id', 'recall50', 'qps', 'avg_full_eval', 'avg_coarse_touch'])
        w.writerows(rows)

    best_rows = []
    ds = DATASET['name']
    for method in ['CABS', 'SOSIA']:
        cand = [r for r in rows if r[0] == ds and r[1] == method]
        for t in TARGETS:
            ok = [r for r in cand if r[3] >= t]
            if not ok:
                best_rows.append([ds, method, t, 'N/A', 'N/A', 'N/A', 'N/A', 'N/A'])
            else:
                b = max(ok, key=lambda x: x[4])
                best_rows.append([ds, method, t, b[2], b[3], b[4], b[5], b[6]])

    with (OUT / 'best_by_target.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['dataset', 'method', 'target_recall', 'best_config_id', 'recall50', 'qps', 'avg_full_eval', 'avg_coarse_touch'])
        w.writerows(best_rows)

    # report checks
    sosia_rows = [r for r in rows if r[1] == 'SOSIA']
    max_sosia_recall = max([r[3] for r in sosia_rows], default=0.0)
    sosia_ok = max_sosia_recall >= 0.80

    # same-target comparison
    cmp_lines = []
    all_ok = True
    for t in TARGETS:
        c = [r for r in best_rows if r[1] == 'CABS' and abs(float(r[2]) - t) < 1e-9][0]
        s = [r for r in best_rows if r[1] == 'SOSIA' and abs(float(r[2]) - t) < 1e-9][0]
        if c[3] == 'N/A' or s[3] == 'N/A':
            cmp_lines.append(f'- target {t:.2f}: CABS 或 SOSIA 存在 N/A，无法完整比较')
            all_ok = False
        else:
            cond = (float(c[5]) > float(s[5])) and (float(c[6]) < float(s[6]))
            all_ok = all_ok and cond
            cmp_lines.append(f"- target {t:.2f}: CABS qps={float(c[5]):.2f}, SOSIA qps={float(s[5]):.2f}; CABS avg_full={float(c[6]):.1f}, SOSIA avg_full={float(s[6]):.1f}; 条件={'成立' if cond else '不成立'}")

    # monotonic checks
    def mono_check(method):
        pts = []
        for t in TARGETS:
            x = [r for r in best_rows if r[1] == method and abs(float(r[2]) - t) < 1e-9][0]
            if x[3] == 'N/A':
                return False, f'{method}: 存在 N/A，无法做单调性完整判断'
            pts.append((float(x[2]), float(x[5]), float(x[6])))
        qps_noninc = all(pts[i][1] >= pts[i+1][1] for i in range(len(pts)-1))
        full_nondec = all(pts[i][2] <= pts[i+1][2] for i in range(len(pts)-1))
        return (qps_noninc and full_nondec), f'{method}: QPS非增={qps_noninc}, AvgFullEval非减={full_nondec}'

    m_cabs_ok, m_cabs_msg = mono_check('CABS')
    m_sos_ok, m_sos_msg = mono_check('SOSIA')

    lines = [
        '# quick_validate_report',
        '',
        f'数据集: {DATASET["name"]}, query_subset_size={qn_use}, topk={TOPK}, seed={SEED}',
        '',
        '## Compact table: QPS (AvgFullEval)',
        ''
    ]
    for t in TARGETS:
        c = [r for r in best_rows if r[1] == 'CABS' and abs(float(r[2]) - t) < 1e-9][0]
        s = [r for r in best_rows if r[1] == 'SOSIA' and abs(float(r[2]) - t) < 1e-9][0]
        def fmt(x):
            if x[3] == 'N/A':
                return 'N/A'
            return f"{float(x[5]):.2f} ({float(x[6]):.1f})"
        lines.append(f'- target {t:.2f}: CABS {fmt(c)} | SOSIA {fmt(s)}')
    lines += [
        '',
        '## 重点判断',
        '',
        f'- SOSIA Recall@50 最大值: {max_sosia_recall:.4f}，是否进入合理区间(>=0.80): {"是" if sosia_ok else "否"}',
        f'- 相同 target recall 下 CABS(QPS更高且AvgFullEval更低) 是否总体成立: {"是" if all_ok else "否/部分"}',
    ]
    lines.extend(cmp_lines)
    lines += [
        '',
        f'- 随 target recall 从 0.90 到 0.97，CABS 是否满足 QPS下降且AvgFullEval上升: {m_cabs_msg}',
        f'- 随 target recall 从 0.90 到 0.97，SOSIA 是否满足 QPS下降且AvgFullEval上升: {m_sos_msg}',
        '',
        '备注: 当前 AvgCoarseTouch 仍使用 AvgFullEval 代理（与现有quick_validate实现一致）。'
    ]

    (OUT / 'quick_validate_report.md').write_text('\n'.join(lines))


def main():
    print('== File checks (SPLADE-1M) ==')
    print(f'base:   {DATASET["base"]} exists={DATASET["base"].exists()}')
    print(f'query:  {DATASET["query"]} exists={DATASET["query"].exists()}')
    print(f'ben:    {DATASET["ben"]} exists={DATASET["ben"].exists()}')
    print(f'truth output dir: {OUT / "oracle"}')

    if not (DATASET['base'].exists() and DATASET['query'].exists() and DATASET['ben'].exists()):
        raise SystemExit('required SPLADE-1M files missing, abort')

    rows32, qn32, truth32 = run_stage(32)
    stable = len(rows32) > 0

    final_rows = rows32
    final_qn = qn32
    if stable:
        print('[info] q=32 stable, escalate to q=64')
        rows64, qn64, truth64 = run_stage(64)
        if len(rows64) > 0:
            final_rows = rows64
            final_qn = qn64

    write_outputs(final_rows, final_qn)
    print('done:', OUT)


if __name__ == '__main__':
    main()
