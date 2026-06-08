# Final Results Release

- Generated at: 2026-04-19 17:27:33
- Goal: unify and separate final results by purpose (parameter tuning / scalability / multi-algorithm multi-dataset / final paper tables).

## Structure
- 01_parameter_tuning/cabs_0p1m_full: CABS补扫与selector结果
- 02_scalability/raw_logs: 可扩展性相关日志
- 03_multi_algo_multi_dataset/baseline_quick_validate: 多算法多数据集基线汇总
- 04_final_tables: 论文终版主表与补充表
- 99_audit_and_notes: spot audit与审计说明

## Selected Source Files
- /home/songyang/TKGN/CABS/results/quick_validate_fix_v2/summary_0p1m_extend.csv -> /home/songyang/TKGN/CABS/results/final_release/01_parameter_tuning/cabs_0p1m_full/summary_0p1m_extend.csv
- /home/songyang/TKGN/CABS/results/quick_validate_fix_v2/best_0p1m_extend.csv -> /home/songyang/TKGN/CABS/results/final_release/01_parameter_tuning/cabs_0p1m_full/best_0p1m_extend.csv
- /home/songyang/TKGN/CABS/results/quick_validate_fix_v2/summary_full_extend.csv -> /home/songyang/TKGN/CABS/results/final_release/01_parameter_tuning/cabs_0p1m_full/summary_full_extend.csv
- /home/songyang/TKGN/CABS/results/quick_validate_fix_v2/best_full_extend.csv -> /home/songyang/TKGN/CABS/results/final_release/01_parameter_tuning/cabs_0p1m_full/best_full_extend.csv
- /home/songyang/TKGN/CABS/results/SPLADE/1M/exp1_1M_v2.log -> /home/songyang/TKGN/CABS/results/final_release/02_scalability/raw_logs/exp1_1M_v2.log
- /home/songyang/TKGN/CABS/results/SPLADE/1M/exp2_1M_v2.log -> /home/songyang/TKGN/CABS/results/final_release/02_scalability/raw_logs/exp2_1M_v2.log
- /home/songyang/TKGN/CABS/results/SPLADE/1M/exp3_1M_v2.log -> /home/songyang/TKGN/CABS/results/final_release/02_scalability/raw_logs/exp3_1M_v2.log
- /home/songyang/TKGN/CABS/results/quick_validate/summary.csv -> /home/songyang/TKGN/CABS/results/final_release/03_multi_algo_multi_dataset/baseline_quick_validate/summary.csv
- /home/songyang/TKGN/CABS/results/quick_validate/best_by_target.csv -> /home/songyang/TKGN/CABS/results/final_release/03_multi_algo_multi_dataset/baseline_quick_validate/best_by_target.csv
- /home/songyang/TKGN/CABS/results/quick_validate/paper_table.md -> /home/songyang/TKGN/CABS/results/final_release/03_multi_algo_multi_dataset/baseline_quick_validate/paper_table.md
- /home/songyang/TKGN/CABS/results/quick_validate/quick_validate_report.md -> /home/songyang/TKGN/CABS/results/final_release/03_multi_algo_multi_dataset/baseline_quick_validate/quick_validate_report.md
- /home/songyang/TKGN/CABS/results/quick_validate_fix_v2/paper_table_main_v2.md -> /home/songyang/TKGN/CABS/results/final_release/04_final_tables/paper_table_main_v2.md
- /home/songyang/TKGN/CABS/results/quick_validate_fix_v2/paper_table_supp_v2.md -> /home/songyang/TKGN/CABS/results/final_release/04_final_tables/paper_table_supp_v2.md
- /home/songyang/TKGN/CABS/results/quick_validate_fix_v2/audit_note_v2.md -> /home/songyang/TKGN/CABS/results/final_release/99_audit_and_notes/audit_note_v2.md
- /home/songyang/TKGN/CABS/results/quick_validate_fix/splade_full_cabs_audit/spot10_eta0.03_nprobe8.csv -> /home/songyang/TKGN/CABS/results/final_release/99_audit_and_notes/spot10_eta0.03_nprobe8.csv