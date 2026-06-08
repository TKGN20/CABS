# quick_validate_report

## 1) 三个数据集是否跑通
- SPLADE-0.1M: 跑通
- SPLADE-1M: 跑通
- SPLADE-10M: 跑通（其中 eta=0.01 一次任务失败，已用 eta=0.02/0.03 完成主表）

## 2) query 使用策略
- SPLADE-0.1M: full benchmark queries (N=200)
- SPLADE-1M: full benchmark queries (N=200)
- SPLADE-10M: full benchmark queries (N=200)
- seed=42

## 3) CABS vs SOSIA（相同target recall）
- 在 SPLADE-1M 与 SPLADE-10M 上，CABS 在多数 target 下 QPS 高于 SOSIA；同时 AvgFullEval 通常高于 SOSIA（更高候选验证量换取更高吞吐）。
- 在 SPLADE-0.1M 上，SOSIA 在高 recall 区域可达点更多，CABS 在当前参数网格下可达 recall 上限偏低。

## 4) target recall 从 0.90 到 0.99 的趋势
- 两方法整体上呈现 QPS 下降趋势。
- AvgFullEval 整体上升，个别点存在离散跳变（配置离散网格导致）。

## 5) 可直接用于论文主表的点
- 采用 best_by_target.csv 中非 N/A 单元格，格式 QPS (AvgFullEval)。
- 主表文件：paper_table.md。

## 6) provisional 点
- SPLADE-10M 的 eta=0.01 任务失败后未重跑该 eta，相关 target 已由其余 eta 覆盖；标记为 provisional。
- 所有结果为 quick_validate 快速版，用于 6.4 主表先验填充。

## 参数映射
- 外部 nprobe -> curves 中 control_param_name=nprobe
- 外部 eta -> mode11 配置键 cabs_eta
- 外部 T -> curves 中 SOSIA ub

## 数据集映射
- 使用数据集路径 datasets/MS MARCO v1/SPLADE/10M/base_full.csr 作为 SPLADE-10M（即 SPLADE-FULL 等价映射）。