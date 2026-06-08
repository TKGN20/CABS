# audit_note_v2

本轮仅新增 CABS 补扫：SPLADE-0.1M 与 SPLADE-FULL (8.84M)；未重跑 SOSIA，未重跑 SPLADE-1M。

## Sweep specs
- 0.1M: K={10,15,20}, tau={0.7}, eta={0.03,0.05}, nprobe={10,15,20,25}
- FULL: K={15,20,32}, tau={0.7,0.8}, eta={0.005,0.01,0.02,0.03}, nprobe={1,2,3,5,8,12}

## Outputs
- summary_0p1m_extend.csv
- best_0p1m_extend.csv
- summary_full_extend.csv
- best_full_extend.csv
- paper_table_main_v2.md
- paper_table_supp_v2.md

- FULL 0.90~0.97 uses 3 distinct configs: K32_tau0.7_eta0.005_nprobe3, K32_tau0.7_eta0.005_nprobe5, K32_tau0.7_eta0.005_nprobe8