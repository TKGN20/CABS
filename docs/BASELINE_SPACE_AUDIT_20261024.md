# Baseline retained-space checkpoint — 2026-10-24

后续进展：[2026-10-25 四方法共同成本记录](COMMON_COST_CHECKPOINT_20261025.md)
已补齐 CABS 的分项导出和统一记录。本页保留此前三基线核验的范围与数据。

本轮结论：SOSIA、SINDI、SEISMIC 三个基线均已有可执行的只读空间分项接口、
计数检查及一组真实输入上的记录。**这关闭了缺少基线分项导出器的问题，尚未
关闭四方法共同 IS 口径与正式性能验收。整体仍为 NOT FINAL PASS。**

全部改动属于 PR #5 的实验层。PR #4 的 19 个生产算法／默认配置文件通过冻结
检查，未修改 CABS 算法、自动规则、历史指标或论文数字。

## 实现与边界

- SINDI：在已披露正确性补丁之后，新增注册的只读 `GetStats()` override。
  不增加原生字段，不修改构建、搜索或序列化函数。逐项统计倒排表对象、ID、
  值、窗口偏移、保留的 mutex 容量，以及启用原生重排时的完整 forward 副本。
  适配器另计原始 CSR、行视图容器及数组，并核对容量余量。原生
  `GetMemoryUsage()` 的零值桩没有被当作 IS，也没有改写成另一种估计。
- SEISMIC：两个注册的只读方法统计 index/list 对象头、packed postings、
  block offsets、summary 数组与压缩结构。`mem_dbg` 使用 `CAPACITY`，已嵌入
  对象的头不重复相加。原生 plain forward store 已保存完整向量，计入一次；
  适配器用于恢复原始 ID 的 offset map 单独计入。原生 `SpaceUsage` 保留为
  独立字段，其值不直接替代完整分项求和。
- SOSIA：此前的 16 项导出器继续保留 SOS/hash 缓冲、桶容量、预处理缓存等。

所有遍历均在查询计时之后执行。序列化／重载是显式启用的额外诊断，正常运行
不要求它。分项数值来自实际对象、数组长度及容量；分配器开销、共享代码、
查询数据／临时工作区和已释放的构建临时结构不计入，RSS 与文件大小另报。
接口状态保持 `REPORTED_NOT_COMMON_VALIDATED`，避免误认为完成了共同 IS 验收。

补丁前后文件 SHA-256、依赖补丁 SHA、编译命令、工具链、库与二进制 SHA
均记录在 `experiments/baselines/*_space.json` 和公开 build receipts 中。
SINDI 仍是披露修复的研究原型，SEISMIC 仍是当前注册版本；没有将它们重标为
未经修改的论文原版。

## 已执行检查

SINDI 检查完整候选池、原始 ID、原始向量精确得分、普通分配器与两种诊断
初始化、并列排序、关闭原生重排、错误 ID、候选不足和尾部维度。新增检查包括：

1. 独立解析原生序列化字节，逐项验证 posting ID／值、窗口边界及 forward
   副本与原始 CSR 一致，核对文件字节公式。
2. 构建后和重载后返回的 native 候选、分数、统一精排结果完全一致。重载不
   重建构建用 mutex，其空间差异单独核对，不把文件大小当成运行期 IS。
3. 使用合法 `lambda=10000`、10,001 条文档跨越两个窗口；另核对关闭重排时
   forward 副本为零，篡改 posting 计数被拒绝。最初尝试的 `lambda=64`
   不在原生接口范围内，未放宽原生约束来迁就测试。

SEISMIC 检查 native／candidate 路径、固定种子、原始得分与排序，以及重复
计入原始向量的错误清单被拒绝。32 维和 2,048 维测试分别覆盖 dense summaries、
非空 sparse component-ID 数组和空尾部维度。序列化重载后全部 index 字段、
候选集合、native 排名与精确结果一致；容量可能随重载变化，不能据文件大小
推定运行期字节数。

保存记录验证现覆盖 **33 份收据、20 组合成测试、5 组既有真实方法／对照记录、
3 组真实空间清单**。它验证哈希、保存的分项公式与小型原始输入；不会重新
运行真实全量构建。真实输入上的得分检查在本轮另行执行，重新核查仍需数据。
Release 与 Debug＋ASan/UBSan 的六项实验 CTest 均通过；这不等于 SINDI 原生
库已经接受完整 sanitizer 检查，也不等于完整工作负载性能验证。

## 真实输入空间记录

本机 SPLADE-0.1M，100,000 条文档、30,109 维、12,729,954 个非零项。
沿用此前四条 selection 查询和各方法固定的接入配置；每方法一次构建、三轮
计时，单线程搜索，600 秒及 8 GiB 地址空间限制。新旧返回的 CSV 逐字节一致。

| 固定接入配置 | 索引结构字节 | 原始向量存储字节 | 合计字节 |
| --- | ---: | ---: | ---: |
| SOSIA：ell=4, m=16, budget=1000 | 114,976,071 | 102,639,664 | 217,615,735 |
| SINDI：lambda=10000, alpha=beta=1, gamma=1000 | 211,843,908 | 102,639,664 | 314,483,572 |
| SEISMIC：postings=1000, centroid fraction=0.1, energy=1, cut=20, factor=0.7 | 1,633,083,045 | 102,639,640 | 1,735,722,685 |

这些配置召回不同，**不能据此排序方法空间效率、计算加速比或替换论文表格**。
SEISMIC 的 `energy=1` 保留较大的 summaries；记录如实保留，不为接近草稿
数字而改值或另选有利配置。SINDI 的 native forward 副本为 104,239,632 字节，
已包含在其索引结构列内；这是实际重复存储，不是再加一遍同一个共享对象。

SOSIA／SINDI 的原始向量列沿用逻辑 CSR 文件字节，包含 24 字节头；SEISMIC
列是已存在的 forward heap，文件头并未存于该 heap。其对应 CSR 参考字节仍为
102,639,664。这个 24 字节的表示差别明确保留，最终共同口径需统一展示，
不能静默修正旧记录。对象头、容量和完整原始数据存储的四方法核对仍须完成。

## 复核入口与剩余工作

- 构建与核验命令：[baseline README](../experiments/baselines/README.md)。
- 补丁和工具：`experiments/baselines/sindi_space.*`、`seismic_space.*`、
  `space_inventory.py`、`test_sindi.py`、`test_seismic.py`。
- 原始记录：`experiments/delivery/fixtures/*_space*/`、
  `experiments/delivery/space_inventory/` 与 `receipts/*space*.json`。
- 无原生依赖的保存记录复算：
  `python3 experiments/delivery/verify_receipts.py`。

接下来导出 CABS 的同粒度分项，完成四方法容量／对象头／原始向量与计时口径
核验，再推进注册参数网格和验证集选参锁。服务器尚未就绪，正式服务器比较、
完整多种子矩阵、完整工作负载、权威稿件证据对齐和最终独立复核仍未完成。
本轮没有提供新的论文性能结论，也没有获得合并审批。
