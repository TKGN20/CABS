import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tests'))
from smoke_test import write_csr
from oracle import audit,CSR


class OracleTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='baseline-oracle-');self.d=Path(self.temp.name)
        write_csr(self.d/'base.csr',[{0:1},{0:1},{1:2},{0:.5,1:1}],2)
        write_csr(self.d/'query.csr',[{1:1},{0:1},{0:1,1:1}],2)
        self.results=[dict(query_id=1,rank=1,identifier=0,score=1),dict(query_id=1,rank=2,identifier=1,score=1),dict(query_id=2,rank=1,identifier=2,score=2),dict(query_id=2,rank=2,identifier=3,score=1.5)]
        self.times=[dict(repeat=r,query_id=q,recall=1,seconds=.01) for r in range(2) for q in (1,2)]
        self.write()

    def tearDown(self):self.temp.cleanup()

    def write(self):
        for name,rows in [('results.csv',self.results),('timings.csv',self.times)]:
            with (self.d/name).open('w',newline='') as f:
                writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)

    def check(self):return audit(self.d/'base.csr',self.d/'query.csr',self.d/'results.csv',self.d/'timings.csv',[1,2],2,2)

    def test_valid_original_ids(self):self.assertEqual(self.check()['Recall'],1)
    def test_native_position_not_original_query(self):
        self.results[0]['query_id']=0;self.write()
        with self.assertRaises(ValueError):self.check()
    def test_truncated(self):
        self.results.pop();self.write()
        with self.assertRaisesRegex(ValueError,'Underfilled'):self.check()
    def test_ties(self):
        self.results[0]['identifier']=1;self.results[1]['identifier']=0;self.write()
        with self.assertRaisesRegex(ValueError,'tie order'):self.check()
    def test_score(self):
        self.results[0]['score']=.999;self.write()
        with self.assertRaisesRegex(ValueError,'inner product'):self.check()
    def test_missing_query(self):
        self.results=self.results[:2];self.write()
        with self.assertRaisesRegex(ValueError,'missing query'):self.check()
    def test_missing_time(self):
        self.times.pop();self.write()
        with self.assertRaisesRegex(ValueError,'timing'):self.check()
    def test_invalid_doc_id(self):
        self.results[0]['identifier']=4;self.write()
        with self.assertRaisesRegex(ValueError,'ID out of range'):self.check()
    def test_invalid_time(self):
        self.times[0]['seconds']=float('nan');self.write()
        with self.assertRaisesRegex(ValueError,'query time'):self.check()


if __name__=='__main__':unittest.main()
