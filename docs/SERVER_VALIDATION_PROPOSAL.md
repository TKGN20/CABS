# 同召回服务器试验：待审批执行方案

目的：在论文指定服务器上验证性能结论，而非让已有本机结果按硬件倍数换算。该方案本轮尚未执行。

2026-10-23 修订：明确校准集、参数选择集和最终评测集相互独立；补充未匹配处理、随机种子分离及 routing/K 诊断。算法基准固定为 `fa2455ed83de47fec1de60563949e32eb1d34c6c`，公开默认仍为 nc=4/k_max=100。

| 项目 | 拟执行内容 |
| --- | --- |
| 第一阶段范围 | SPLADE-0.1M、SPLADE-1M；CABS、SINDI、SEISMIC、SOSIA |
| 源码 | 固定 CABS 审阅提交；每个 baseline 固定官方仓库/分支/commit，保留所需适配补丁，不使用浮动 main |
| 输入 | 原始 CSR 的 SHA 与当前公开配置一致；每个 corpus 独立精确 top-50 GT，另作固定查询的独立评分核验 |
| 查询划分 | 固定且不重叠的 calibration reservoir、selection validation、final evaluation；在查看性能前保存原始 query IDs 和划分文件 SHA，所有方法共用 selection/evaluation IDs |
| 协议 | 单线程查询；相同原始 query IDs；候选精确评分计入查询时间；GT、IO、离线调参不计入 QPS |
| 参数 | 第一组保留各方法记录明确的默认设置；Recall-QPS 组按预先登记的官方搜索参数网格；CABS 默认仍 nc=4/k_max=100，候选 50 另列实验配置 |
| 初始 Recall 目标 | 0.90、0.95、0.97、0.99；目标须由实际工作点达到 |
| 匹配规则 | 仅在独立 selection validation 上，从 Recall 落入 [r,r+0.002] 的工作点中选择 QPS 最高者并锁定参数；最终 evaluation 不参与选择 |
| 未匹配 | validation 无合格点，或锁定配置在 final evaluation 上未落入预定区间，均标为 unmatched；展示实际点，不放宽区间、不插值构造加速比，也不根据 final evaluation 重新选参 |
| 重复 | 每个最终保留工作点至少 3 次独立构建，各 3 轮查询计时；计时重复与独立构建方差分开 |
| 指标 | Recall@50、pooled QPS、完整 IT、含检索所需向量存储的 IS；同时保留结构空间和 peak RSS |
| 路由指标 | probe-depth 分布、触及/验证文档量、上界余量；上界插桩单独运行，不冒充未插桩 QPS |
| 环境 | 对照当前论文的 Dell PowerEdge R740、双 Xeon Silver 4215R、16 物理核/32 线程及约 125 GiB 可用内存；执行前用实际系统信息确认，并记录 OS/编译器、频率/电源策略、CPU affinity、NUMA、线程与编译选项 |
| 产物 | 全部参数点、GT/ID/分数、原始调用时间、解析后 K/m/B、命令/构建日志、输入/源码/二进制哈希；不得只保留最优结果 |

## 查询集与参数选择

拟对当前 6,980 条查询使用独立划分 seed=20261023，预留 128 条作为 calibration reservoir、1,000 条作为 selection validation，其余 5,852 条作为 final evaluation。实施前应确认输入查询数量，并冻结打乱算法、版本、实际 ID 清单和 SHA；若输入规模不同，先修订计划，不能在看到结果后改变划分。校准只从 reservoir 抽取；未使用的 reservoir 查询不流入另外两组。

各方法的默认配置直接在固定 final evaluation 上评测。搜索参数网格只能在 selection validation 上选择，默认配置和自动 K/m/B 的规则不通过最终评测反馈调整。先用预先登记的网格测量 validation，按目标区间选最快点（QPS 完全相同则按预登记配置 ID 排序），保存选中配置及其文件 SHA，再开启最终评测。

锁定配置在至少三次独立构建、每次三轮查询计时后报告 final Recall/QPS 及构建间波动。匹配标签按预先约定的最终平均 Recall 检查；只有双方都落入同一目标区间才报告该目标的加速比。若未匹配，保留实际结果及曲线并明确标记，不能重选、更改容差或把不同 Recall 的比值描述成同召回加速比。曲线所用配置也必须在最终评测前固定；最终曲线不能反过来选择表格中的最快点。

当前公开 CLI 只有校准/评测两组划分，`--seed` 同时影响查询打乱和索引随机化，尚未直接实现上述三组固定 ID 协议。服务器运行入口需先支持显式 query ID 清单、独立的 split/calibration/index seed，并保留原始 query ID 到输入行的映射。这属于待实现的实验驱动适配，不能把当前 CLI 命令记为已经满足该协议；生产 K/m/B 和安全上界规则不随适配改变。

## 校准稳定性与路由有效性

在独立的小规模试跑之后，拟采用三个 calibration seed（20261007、20261008、20261009）与三个 index seed（20261017、20261018、20261019）的交叉设计。每个 calibration seed 从固定 reservoir 抽取 nc=4 条查询；对同一校准子集改变 index seed，对不同子集保留相同 index-seed 列表。split seed 与 selection/evaluation IDs 始终固定。九个组合均重新执行自动 K/m/B，不能通过更换当前单一 `--seed` 冒充“只改变校准样本”。

保留所有组合的 K、B、局部 m 的最小值/中位数/最大值及完整簇参数，报告 Recall、pooled QPS、IT/IS 和构建间离散程度。每个种子组合的三轮计时仍是同一索引的计时重复，不增加独立构建样本数；不挑最好 seed 当默认结果。这个交叉试验是稳定性分析，不与四方法默认比较中的独立构建重复混为一组；其资源投入在试跑后确定。

Section VI 的 routing effectiveness 分析拟报告 probe-depth 分布、访问全部 K 簇的比例、stopping ratio、触及及累计验证文档量、suffix/gamma 余量和上界紧度。按 K 和校准 seed 分组解释这些统计。correctness 与 selectivity 分开陈述：停止条件能正确执行，不等于真实工作负载上已减少访问。少量穷举上界核查不作为全输入浮点形式化证明；插桩和穷举另行运行，不进入 QPS。

K 代理成本与完整查询成本也单独诊断：在相同候选 K、同一 realized index 和同一组 selection queries 上，保存代理成本、未插桩完整耗时、Recall、验证量和候选池统计；同时呈现原始配对点、各自的最小值位置及排名相关性。若只有一个候选或成本恒定，相关性标为不适用。不同 K 的 Recall 可能不同，不能把相关性或最小耗时解释成同召回优胜，也不据此悄然替换生产 K 目标函数。最终评测集只用于锁定协议后的报告。

每个更紧上界、新 K 成本目标或校准规则都另列优化实验；当前方案不加入坐标最大值上界，也不将默认配置描述为跨 workload 最优。

建议审批顺序：先批准小规模两数据集/四方法的默认与匹配点试跑，再决定是否扩展至论文全部十二个 workload。搜索网格需在确认官方 baseline 的参数接口和服务器指令集后填入，不用占位参数冒充可执行命令。

官方源码的候选固定版本已经只读核对存在：

- [SOSIA 55433a618f39e3fbc98dc34a1ffa1433d9b631b9](https://github.com/Jacyhust/SOSIA/commit/55433a618f39e3fbc98dc34a1ffa1433d9b631b9)。
- [SINDI sparse 93e034f920334ea9e0d92784bb1d8964fb7322fa](https://github.com/Roxanne0321/vsag/commit/93e034f920334ea9e0d92784bb1d8964fb7322fa)。
- [SEISMIC 492de11c9b6fd0c46c2c7c274c30836353b882f0](https://github.com/tusKANNy/seismic/commit/492de11c9b6fd0c46c2c7c274c30836353b882f0)，为本轮查询时的候选版本，需再与论文所引用版本核对；不能直接声称它就是原论文实验的实现。

建议先每数据集/方法一次独立构建及三轮查询试跑，保存实测耗时后再审批全部网格和最终三次独立构建的资源窗口。上表的重复要求属于最终证据协议，本轮未执行，也不预先保证某个小时数足够完成。

执行前需落实指定服务器可用性、baseline 固定版本和数据路径，并约定单次资源上限与失败记录方式。上述条件未确认时，不启动长期服务器作业，不把该计划记为已完成实验。

任何关于改变 K 的成本目标、B 的最坏样本规则或安全上界的方案，应另外提交方法定义、正确性条件与实验计划；不与此项公平比较混合审批。
