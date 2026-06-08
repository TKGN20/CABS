# patch_note_small

本轮为极简补扫：固定 SPLADE-1M 与 SOSIA，仅补 CABS 局部前沿。

## Scope
- SPLADE-0.1M/CABS: K={15,20}, tau={0.70}, eta={0.03,0.05}, nprobe={15,20,25}
- SPLADE-FULL (8.84M)/CABS: K={20,32}, tau={0.70,0.80}, eta={0.005,0.01,0.02}, nprobe={1,2,3,5,8}
- SPLADE-1M/SOSIA: fixed (no rescan)

## Execution status
- successful groups: 7
- failed groups (rc=-9): 9
  - SPLADE-FULL (8.84M) K=20 tau=0.7 eta=0.005
  - SPLADE-FULL (8.84M) K=20 tau=0.7 eta=0.01
  - SPLADE-FULL (8.84M) K=20 tau=0.8 eta=0.005
  - SPLADE-FULL (8.84M) K=20 tau=0.8 eta=0.01
  - SPLADE-FULL (8.84M) K=32 tau=0.7 eta=0.005
  - SPLADE-FULL (8.84M) K=32 tau=0.7 eta=0.01
  - SPLADE-FULL (8.84M) K=32 tau=0.7 eta=0.02
  - SPLADE-FULL (8.84M) K=32 tau=0.8 eta=0.005
  - SPLADE-FULL (8.84M) K=32 tau=0.8 eta=0.01

## Selector interpretation
- 相邻 target recall 复用同一配置并非错误；这是在当前已完成网格上的真实 selector 结果。
- 但 FULL 的部分低 eta 组失败，导致低预算侧覆盖不完整。

## Merge outcome
- paper_table_main_v3_small core_rows_changed_vs_v2: false

## Outputs
- summary_patch_small.csv (包含成功与失败组，失败行为 N/A + rc=-9)
- best_patch_small.csv
- paper_table_main_v3_small.md
- 本次补扫未产生可覆盖 v2 主表单元格的新更优点（按 recall>=target 且 qps 最大规则）。
