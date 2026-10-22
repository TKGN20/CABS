# 固定索引 B 规则诊断边界

输入为既有 selected_profile_1m_20261022/pool_targets.csv 与对应完整 GT。
对 4 条校准查询的 15 个非空子集，分别按 exact-rank≤50 与≤100 的已触及正目标计算预算。

B_counterfactual=max(k_max, min_prefix_rank of each retained touched-positive target)。

这只重算同一临时索引的前缀排名，不改变该索引已实现的 encoding、partition、m 或 hash。
它不是把 k_max 或校准集合改变后完整重建 CABS 的结果，不给出新的 QPS/Recall。
小样本的组合分布不是对所有查询划分的统计置信区间。
