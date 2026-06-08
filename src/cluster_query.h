#pragma once

#include "cluster_index.h"
#include "Preprocess.h"

#include <vector>

struct QueryResult {
    int point_id;
    float inner_product;
    bool operator<(const QueryResult& o) const { return inner_product < o.inner_product; }
};

float sparse_sparse_dot(
    const int* ind1,
    const float* val1,
    int nnz1,
    const int* ind2,
    const float* val2,
    int nnz2
);

std::vector<QueryResult> query_cabs(
    const CompositeIndex& cidx,
    const SparseData& data,
    const int* q_indices,
    const float* q_vals,
    int q_nnz,
    int dim,
    int k,
    float probe_ratio = 0.25f
);

std::vector<QueryResult> query_cabs_with_full_eval(
    const CompositeIndex& cidx,
    const SparseData& data,
    const int* q_indices,
    const float* q_vals,
    int q_nnz,
    int dim,
    int k,
    float probe_ratio,
    size_t* full_eval_count
);

struct BenchmarkResult {
    float avg_recall = 0.0f;
    float avg_ratio = 0.0f;
    float throughput = 0.0f;
    float avg_candidates = 0.0f;
    float avg_latency_ms = 0.0f;
    float avg_active_clusters = 0.0f;
    float avg_requested_nprobe = 0.0f;
    float avg_selected_clusters_before_postprocess = 0.0f;
    float avg_selected_clusters_after_postprocess = 0.0f;
    float avg_active_clusters_hit = 0.0f;
};

struct SubsetBenchmark {
    std::vector<std::vector<int>> gt_ids;
    std::vector<std::vector<float>> gt_ips;
    int q_num = 0;
    int k = 0;
};

SubsetBenchmark compute_subset_benchmark(
    Preprocess& prep, int n_sub, int k, int max_queries = 200
);

BenchmarkResult run_cabs_benchmark_subset(
    const CompositeIndex& cidx, Preprocess& prep,
    int k, float probe_ratio, float c,
    const SubsetBenchmark& sub_ben
);

BenchmarkResult run_cabs_benchmark(
    const CompositeIndex& cidx, Preprocess& prep,
    int k, float probe_ratio, float c,
    int query_limit = -1
);

BenchmarkResult run_linear_scan(Preprocess& prep, int k);
