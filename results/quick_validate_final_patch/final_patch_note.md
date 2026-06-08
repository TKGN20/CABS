# final_patch_note

本轮仅执行 SPLADE-FULL (8.84M) / CABS 低预算侧最小补扫。
冻结项：SPLADE-1M / CABS、SPLADE-0.1M / CABS、全部SOSIA。

## Sweep params
- K=20
- tau in {0.75, 0.80}
- eta in {0.012, 0.015, 0.018, 0.020}
- nprobe in {2,3,4,5,6}

- jobs_total=8
- jobs_success=8
- jobs_failed=0

low-budget final patch completed; no better lower-budget operating points found for SPLADE-FULL in 0.90~0.95