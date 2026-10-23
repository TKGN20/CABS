import hashlib
import json
from pathlib import Path
import re
import sys


def main():
    root=Path(__file__).resolve().parent/'receipts'
    registry=json.loads((root/'sha256.json').read_text(encoding='utf-8'))
    expected={p.name for p in root.glob('*.json') if p.name!='sha256.json'}
    if set(registry)!=expected:raise ValueError('Missing or unregistered receipt')
    for name,wanted in registry.items():
        if Path(name).name!=name or not re.fullmatch('[0-9a-f]{64}',wanted):raise ValueError('Invalid receipt manifest entry')
        actual=hashlib.sha256((root/name).read_bytes()).hexdigest()
        if actual!=wanted:raise ValueError('Receipt changed: '+name)
    fixtures=json.loads((root/'fixture_artifacts_20261023.json').read_text(encoding='utf-8'))
    repository=root.parents[2];sys.path.insert(0,str(repository/'experiments/baselines'))
    from oracle import audit
    for method,record in fixtures['methods'].items():
        folder=repository/record['path']
        if not folder.resolve().is_relative_to(repository.resolve()):raise ValueError('Unsafe fixture path')
        for name,wanted in record['sha256'].items():
            if not (folder/name).resolve().is_relative_to(folder.resolve()):raise ValueError('Unsafe artifact path')
            if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=wanted:raise ValueError('Fixture changed: '+name)
        actual=audit(folder/'base.csr',folder/'queries.csr',folder/'run/retrieved.csv',folder/'run/queries.csv',[int(v) for v in (folder/'ids.txt').read_text().split()],50,3)
        if actual['Recall']!=record['Recall']:raise ValueError('Fixture recall differs: '+method)
    for record in fixtures.get('native_smokes',{}).values():
        folder=repository/record['path']
        if not folder.resolve().is_relative_to(repository.resolve()):raise ValueError('Unsafe native artifact path')
        for name,wanted in record['sha256'].items():
            if not (folder/name).resolve().is_relative_to(folder.resolve()):raise ValueError('Unsafe native artifact entry')
            if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=wanted:raise ValueError('Native artifact changed: '+name)
    print(json.dumps(dict(status='PASS',receipts=len(registry),fixtures_recomputed=len(fixtures['methods']),native_smoke_bundles_hash_checked=len(fixtures.get('native_smokes',{})),scope='Saved receipt integrity and small-fixture recomputation only; native smokes hash-only; not independent performance reproduction')))


if __name__=='__main__':main()
