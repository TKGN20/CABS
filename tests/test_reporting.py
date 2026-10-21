#!/usr/bin/env python3
"""Reject incomplete or tampered result records; fixture is synthetic test data."""
import csv
import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from evaluate import audit


class ReportingTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.root=Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        self.json('summary.json',dict(k=2,n=10,calibration_queries=1,evaluation_queries=1,repeats=2,
            B=3,K=1,recall=1,qps=2,dataset='SYNTHETIC-UNIT-FIXTURE',index_structure_bytes=64,
            original_vector_bytes=128,build_seconds_including_calibration=3,
            calibration_ground_truth_seconds=1,construction_seconds=6))
        self.json('automatic_configuration.json',dict(K=1,B=3,clusters=[dict(m=3)],
            stage_seconds=dict(df_statistics=1,dataset_loading_validation=1)))
        self.json('index_space.json',dict(definition='synthetic fixture',components_bytes={'test':64},
            index_structure_bytes=64,index_plus_vectors_bytes=192))
        self.csv('query_split.csv','role,query_id',[['calibration',0],['evaluation',1]])
        self.csv('ground_truth.csv','query_id,rank,identifier,score',[[0,1,0,4],[0,2,1,3],[1,1,2,4],[1,2,3,3]])
        self.csv('retrieved.csv','k,query_id,rank,identifier,score',[[2,1,1,2,4],[2,1,2,3,3]])
        self.csv('queries.csv','k,repeat,query_id,recall,seconds,pool_peak,verified',
                 [[2,0,1,1,.5,3,3],[2,1,1,1,.5,3,3]])
    def json(self,name,value):
        (self.root/name).write_text(json.dumps(value))
    def csv(self,name,header,rows):
        with (self.root/name).open('w',newline='') as stream:
            writer=csv.writer(stream);writer.writerow(header.split(','));writer.writerows(rows)
    def test_recompute(self):
        self.assertEqual(audit(self.root)['QPS'],2)
        self.assertEqual(audit(self.root)['IS_bytes'],192)
    def test_duplicate_repeat(self):
        self.csv('queries.csv','k,repeat,query_id,recall,seconds,pool_peak,verified',[[2,0,1,1,.5,3,3]]*2)
        with self.assertRaisesRegex(ValueError,'Duplicate timing'):
            audit(self.root)
    def test_wrong_recall(self):
        self.csv('queries.csv','k,repeat,query_id,recall,seconds,pool_peak,verified',[[2,0,1,.5,.5,3,3],[2,1,1,1,.5,3,3]])
        with self.assertRaisesRegex(ValueError,'Recall differs'):
            audit(self.root)
    def test_query_leakage(self):
        self.csv('query_split.csv','role,query_id',[['calibration',1],['evaluation',1]])
        with self.assertRaisesRegex(ValueError,'leakage'):
            audit(self.root)
    def test_published_compressed_records(self):
        for name in ('ground_truth.csv','queries.csv','retrieved.csv'):
            path=self.root/name
            with gzip.open(str(path)+'.gz','wb') as stream:
                stream.write(path.read_bytes())
            path.unlink()
        self.assertEqual(audit(self.root)['QPS'],2)
    def test_modified_coherent_times_are_detected(self):
        original=hashlib.sha256((self.root/'queries.csv').read_bytes()).hexdigest()
        self.json('artifact_manifest.json',{'sha256':{'queries.csv':original}})
        self.csv('queries.csv','k,repeat,query_id,recall,seconds,pool_peak,verified',[[2,0,1,1,.25,3,3],[2,1,1,1,.25,3,3]])
        summary=json.loads((self.root/'summary.json').read_text());summary['qps']=4
        self.json('summary.json',summary)
        with self.assertRaisesRegex(ValueError,'checksum mismatch'):
            audit(self.root)


if __name__=='__main__':
    unittest.main()
