#include "cluster_query.h"

#include "basis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <queue>
#include <unordered_set>

namespace {

struct MinHeapCmp {
    bool operator()(const QueryResult& a, const QueryResult& b) const {
        return a.inner_product > b.inner_product;
    }
};

static inline float fast_randf(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state & 0x7FFFFFu) / static_cast<float>(0x800000u);
}

struct QueryDebugStat {
    int requested_nprobe = 0;
    int selected_clusters_before_postprocess = 0;
    int selected_clusters_after_postprocess = 0;
    int active_clusters_hit = 0;
};

std::vector<int> build_query_set(
    const CompositeIndex& cidx,
    const int* q_indices,
    const float* q_vals,
    int q_nnz,
    int hash_domain,
    int seed_extra = 0
) {
    std::vector<int> q_set;
    q_set.reserve(static_cast<size_t>(q_nnz) * 4);

    float q_max = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < q_nnz; ++i) q_max = std::max(q_max, q_vals[i]);
    if (!std::isfinite(q_max) || q_max <= 0.0f) q_max = 1.0f;

    const int active_dim = (cidx.dim_limit > 0) ? std::min(cidx.dim, cidx.dim_limit) : cidx.dim;
    const int l_fixed = std::max(3, (int)20);

    uint32_t rng_state = static_cast<uint32_t>(2027 + seed_extra * 1315423911u);

    for (int j = 0; j < q_nnz; ++j) {
        const int idx = q_indices[j];
        const float v = q_vals[j];
        if (v <= 0.0f) continue;
        if (idx < 0 || idx >= active_dim) continue;

        const float q_norm_val = std::max(0.0f, std::min(1.0f, v / q_max));
        float weighted_prob = q_norm_val;
        if (cidx.use_idf && idx < static_cast<int>(cidx.idf_weights.idf.size())) {
            weighted_prob = std::min(1.0f, q_norm_val * cidx.idf_weights.idf[static_cast<size_t>(idx)]);
        }
        if (weighted_prob <= 0.0f) continue;

        int l_j = l_fixed;
        int start_pos = idx * l_fixed;
        if (cidx.use_adaptive_l && idx < static_cast<int>(cidx.idf_weights.dim_l.size())) {
            l_j = cidx.idf_weights.dim_l[static_cast<size_t>(idx)];
            start_pos = cidx.idf_weights.prefix_l[static_cast<size_t>(idx)];
        }

        const int upper = std::min(hash_domain, start_pos + l_j);
        for (int slot = start_pos; slot < upper; ++slot) {
            if (fast_randf(rng_state) < weighted_prob) q_set.push_back(slot);
        }
    }

    std::sort(q_set.begin(), q_set.end());
    q_set.erase(std::unique(q_set.begin(), q_set.end()), q_set.end());
    return q_set;
}

std::vector<QueryResult> query_cabs_impl(
    const CompositeIndex& cidx,
    const SparseData& data,
    const int* q_indices,
    const float* q_vals,
    int q_nnz,
    int k,
    float probe_ratio,
    size_t* checked_candidates,
    int* active_cluster_count,
    QueryDebugStat* dbg
) {
    if (checked_candidates) *checked_candidates = 0;
    if (active_cluster_count) *active_cluster_count = 0;
    if (dbg) *dbg = QueryDebugStat{};

    if (cidx.K <= 0 || k <= 0 || q_nnz <= 0) return {};

    std::vector<std::pair<float, int>> centroid_scores_sorted;
    centroid_scores_sorted.reserve(static_cast<size_t>(cidx.K));
    for (int i = 0; i < cidx.K; ++i) {
        float s = sparse_dense_dot(q_indices, q_vals, q_nnz, cidx.cluster_indexes[static_cast<size_t>(i)].centroid);
        centroid_scores_sorted.emplace_back(s, i);
    }
    std::sort(centroid_scores_sorted.begin(), centroid_scores_sorted.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    int requested_nprobe = std::max(1, (int)std::round((float)cidx.K * probe_ratio));
    int selected_before = requested_nprobe;
    int num_active = std::max(1, (int)ceilf((float)cidx.K * probe_ratio));
    num_active = std::min(num_active, cidx.K);
    if (dbg) {
        dbg->requested_nprobe = requested_nprobe;
        dbg->selected_clusters_before_postprocess = selected_before;
        dbg->selected_clusters_after_postprocess = num_active;
    }
    if (active_cluster_count) *active_cluster_count = num_active;

    int hash_domain = cidx.cluster_indexes[static_cast<size_t>(centroid_scores_sorted.front().second)].hash_domain;
    std::vector<int> q_set = build_query_set(cidx, q_indices, q_vals, q_nnz, hash_domain, q_nnz + k);

    std::priority_queue<QueryResult, std::vector<QueryResult>, MinHeapCmp> global_min_heap;
        int total_verified = 0;
    int active_hit = 0;

    std::vector<int> q_hash;
    std::vector<int> cand_count;
    struct ScoredCand { int li; float score; };
    std::vector<ScoredCand> cands;

    for (int p = 0; p < num_active; p++) {
        const float this_centroid = centroid_scores_sorted[static_cast<size_t>(p)].first;
        const int cid = centroid_scores_sorted[static_cast<size_t>(p)].second;
        const auto& cluster = cidx.cluster_indexes[static_cast<size_t>(cid)];
        const int n_p = (int)cluster.point_ids.size();
        const int m_p = cluster.params.m_i;
        if (n_p <= 0 || m_p <= 0) continue;

        q_hash.assign(static_cast<size_t>(m_p), cluster.hash_domain - 1);
        for (int j = 0; j < m_p; j++) {
            uint64_t a = cluster.hash_funcs[static_cast<size_t>(j)].a;
            uint64_t b = cluster.hash_funcs[static_cast<size_t>(j)].b;
            int minv = cluster.hash_domain - 1;
            for (int elem : q_set) {
                int hv = (int)(((a * (uint64_t)elem + b) % cluster.HASH_PRIME) % (uint64_t)cluster.hash_domain);
                if (hv < minv) minv = hv;
            }
            q_hash[static_cast<size_t>(j)] = minv;
        }

        cand_count.assign(static_cast<size_t>(n_p), 0);
        for (int j = 0; j < m_p; j++) {
            const auto& table = cluster.local_indexes[static_cast<size_t>(j)];
            const auto it = table.find(q_hash[static_cast<size_t>(j)]);
            if (it == table.end()) continue;
            for (int li : it->second) {
                if (li >= 0 && li < n_p) cand_count[static_cast<size_t>(li)]++;
            }
        }

        cands.clear();
        cands.reserve(static_cast<size_t>(n_p));
        for (int li = 0; li < n_p; li++) {
            int cc = cand_count[static_cast<size_t>(li)];
            if (cc <= 0) continue;
            float collision_ratio = (float)cc / (float)std::max(1, m_p);
            float set_size_li = (float)cluster.local_set_sizes[static_cast<size_t>(li)];
            float q_set_sz = (float)q_set.size();
            float so_est = (q_set_sz + set_size_li) / (1.0f + (float)m_p / (float)std::max(1, cc));
            float ip_est = so_est / (float)std::max(1, cluster.params.l_i);
            float alpha = 0.7f;
            float score = alpha * ip_est + (1.0f - alpha) * this_centroid * collision_ratio;
            cands.push_back({li, score});
        }

        int T_budget = std::min(cluster.params.T_i, n_p);
        const int verify_limit = std::min((int)cands.size(), T_budget);
        if (verify_limit > 0) active_hit++;

        if (verify_limit < (int)cands.size()) {
            std::partial_sort(cands.begin(), cands.begin() + verify_limit, cands.end(),
                              [](const ScoredCand& a, const ScoredCand& b) {
                                  return a.score > b.score;
                              });
        } else {
            std::sort(cands.begin(), cands.end(), [](const ScoredCand& a, const ScoredCand& b) {
                return a.score > b.score;
            });
        }
        for (int vi = 0; vi < verify_limit; vi++) {
            int li = cands[static_cast<size_t>(vi)].li;
            int pid = cluster.point_ids[static_cast<size_t>(li)];
            if (pid < 0 || pid + 1 > (int)data.n) continue;

            size_t d_start = data.indptr[pid];
            size_t d_end = data.indptr[pid + 1];
            float ip = sparse_sparse_dot(
                q_indices, q_vals, q_nnz,
                data.indices + d_start, data.val + d_start,
                (int)(d_end - d_start));

            if ((int)global_min_heap.size() < k) {
                global_min_heap.push({pid, ip});
            } else if (ip > global_min_heap.top().inner_product) {
                global_min_heap.pop();
                global_min_heap.push({pid, ip});
            }
        }

        total_verified += verify_limit;
    }

    if (checked_candidates) *checked_candidates = static_cast<size_t>(total_verified);
    if (dbg) dbg->active_clusters_hit = active_hit;

    std::vector<QueryResult> results;
    results.reserve(global_min_heap.size());
    while (!global_min_heap.empty()) {
        results.push_back(global_min_heap.top());
        global_min_heap.pop();
    }
    std::sort(results.begin(), results.end(), [](const QueryResult& a, const QueryResult& b) {
        if (a.inner_product != b.inner_product) return a.inner_product > b.inner_product;
        return a.point_id < b.point_id;
    });

    return results;
}

}  // namespace

float sparse_sparse_dot(
    const int* ind1,
    const float* val1,
    int nnz1,
    const int* ind2,
    const float* val2,
    int nnz2) {
    int i = 0, j = 0;
    float res = 0.0f;
    while (i < nnz1 && j < nnz2) {
        if (ind1[i] == ind2[j]) {
            res += val1[i] * val2[j];
            ++i; ++j;
        } else if (ind1[i] < ind2[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return res;
}

std::vector<QueryResult> query_cabs(
    const CompositeIndex& cidx,
    const SparseData& data,
    const int* q_indices,
    const float* q_vals,
    int q_nnz,
    int dim,
    int k,
    float probe_ratio) {
    (void)dim;
    return query_cabs_impl(cidx, data, q_indices, q_vals, q_nnz, k, probe_ratio, nullptr, nullptr, nullptr);
}

std::vector<QueryResult> query_cabs_with_full_eval(
    const CompositeIndex& cidx,
    const SparseData& data,
    const int* q_indices,
    const float* q_vals,
    int q_nnz,
    int dim,
    int k,
    float probe_ratio,
    size_t* full_eval_count) {
    (void)dim;
    return query_cabs_impl(cidx, data, q_indices, q_vals, q_nnz, k, probe_ratio,
                           full_eval_count, nullptr, nullptr);
}


SubsetBenchmark compute_subset_benchmark(
    Preprocess& prep, int n_sub, int k, int max_queries) {
    SubsetBenchmark sb;
    int nq = std::min(prep.benchmark.N, max_queries);
    int n_cap = std::min(n_sub, (int)prep.data->n);
    int actual_k = std::min(k, n_cap);

    sb.q_num = nq;
    sb.k = actual_k;
    sb.gt_ids.resize(static_cast<size_t>(nq));
    sb.gt_ips.resize(static_cast<size_t>(nq));

    for (int qi = 0; qi < nq; qi++) {
        int q_start = (int)prep.queries->indptr[qi];
        int q_end = (int)prep.queries->indptr[qi + 1];
        int q_nnz = q_end - q_start;
        const int* q_ind = prep.queries->indices + q_start;
        const float* q_val = prep.queries->val + q_start;

        std::vector<std::pair<float, int>> ips;
        ips.reserve(static_cast<size_t>(n_cap));
        for (int j = 0; j < n_cap; j++) {
            int d_start = (int)prep.data->indptr[j];
            int d_end = (int)prep.data->indptr[j + 1];
            float ip = sparse_sparse_dot(
                q_ind, q_val, q_nnz,
                prep.data->indices + d_start,
                prep.data->val + d_start,
                d_end - d_start);
            ips.push_back({ip, j});
        }

        if (actual_k > 0) {
            std::partial_sort(ips.begin(), ips.begin() + actual_k, ips.end(),
                              [](const auto& a, const auto& b) { return a.first > b.first; });
        }

        sb.gt_ids[static_cast<size_t>(qi)].resize(static_cast<size_t>(actual_k));
        sb.gt_ips[static_cast<size_t>(qi)].resize(static_cast<size_t>(actual_k));
        for (int r = 0; r < actual_k; r++) {
            sb.gt_ids[static_cast<size_t>(qi)][static_cast<size_t>(r)] = ips[static_cast<size_t>(r)].second;
            sb.gt_ips[static_cast<size_t>(qi)][static_cast<size_t>(r)] = ips[static_cast<size_t>(r)].first;
        }
    }

    return sb;
}

BenchmarkResult run_cabs_benchmark(
    const CompositeIndex& cidx, Preprocess& prep,
    int k, float probe_ratio, float c, int query_limit) {
    (void)c;
    int qn = prep.benchmark.N;
    if (query_limit > 0) qn = std::min(qn, query_limit);
    const int gt_top = std::min(k, prep.benchmark.num);
    if (qn <= 0 || gt_top <= 0 || k <= 0) {
        return BenchmarkResult{};
    }

    lsh::timer timer;
    double recall_sum = 0.0, ratio_sum = 0.0;
    long long verified_sum = 0;
    long long active_sum = 0;
    long long req_sum = 0;
    long long before_sum = 0;
    long long after_sum = 0;
    long long hit_sum = 0;

#pragma omp parallel for schedule(dynamic, 1) reduction(+:recall_sum, ratio_sum, verified_sum, active_sum, req_sum, before_sum, after_sum, hit_sum)
    for (int qi = 0; qi < qn; ++qi) {
        const size_t q_begin = prep.queries->indptr[qi];
        const size_t q_end = prep.queries->indptr[qi + 1];
        const int* q_ind = prep.queries->indices + q_begin;
        const float* q_val = prep.queries->val + q_begin;
        const int q_nnz = static_cast<int>(q_end - q_begin);

        size_t checked_local = 0;
        int active_local = 0;
        QueryDebugStat dbg{};
        auto results = query_cabs_impl(cidx, *prep.data, q_ind, q_val, q_nnz, k, probe_ratio, &checked_local, &active_local, &dbg);
        verified_sum += (long long)checked_local;
        active_sum += active_local;
        req_sum += dbg.requested_nprobe;
        before_sum += dbg.selected_clusters_before_postprocess;
        after_sum += dbg.selected_clusters_after_postprocess;
        hit_sum += dbg.active_clusters_hit;

        std::unordered_set<int> gt_set;
        gt_set.reserve(static_cast<size_t>(gt_top * 2));
        for (int r = 0; r < gt_top; r++) gt_set.insert(prep.benchmark.indice[qi][r]);

        int hit = 0;
        for (const auto& rr : results) {
            if (gt_set.count(rr.point_id)) hit++;
        }
        double recall_qi = (double)hit / (double)std::max(1, k);
        recall_sum += recall_qi;

        double ret_sum = 0.0, gt_sum = 0.0;
        for (int r = 0; r < std::min(k, (int)results.size()); r++) ret_sum += results[static_cast<size_t>(r)].inner_product;
        for (int r = 0; r < gt_top; r++) gt_sum += prep.benchmark.innerproduct[qi][r];
        double ratio_qi = (gt_sum > 1e-9) ? std::min(1.0, ret_sum / gt_sum) : 1.0;
        ratio_sum += ratio_qi;
    }

    double total_t = timer.elapsed();
    BenchmarkResult br;
    br.avg_recall = (float)(recall_sum / std::max(1, qn));
    br.avg_ratio = (float)(ratio_sum / std::max(1, qn));
    br.throughput = (float)((total_t > 0.0) ? ((double)qn / total_t) : 0.0);
    br.avg_candidates = (float)((double)verified_sum / std::max(1, qn));
    br.avg_latency_ms = (float)((total_t > 0.0) ? (total_t * 1000.0 / (double)qn) : 0.0);
    br.avg_active_clusters = (float)((double)active_sum / std::max(1, qn));
    br.avg_requested_nprobe = (float)((double)req_sum / std::max(1, qn));
    br.avg_selected_clusters_before_postprocess = (float)((double)before_sum / std::max(1, qn));
    br.avg_selected_clusters_after_postprocess = (float)((double)after_sum / std::max(1, qn));
    br.avg_active_clusters_hit = (float)((double)hit_sum / std::max(1, qn));

    std::printf("[CABS] Recall@%d = %.4f, Ratio = %.4f\n", k, br.avg_recall, br.avg_ratio);
    std::printf("[CABS] Total time = %.4f s, Throughput = %.2f qps\n", total_t, br.throughput);
    std::printf("[CABS] Avg verified candidates/query = %.2f, Avg active clusters = %.2f\n", br.avg_candidates, br.avg_active_clusters);
    std::printf("[CABS][routing] requested_nprobe=%.2f, selected_clusters_before_postprocess=%.2f, selected_clusters_after_postprocess=%.2f, avg_active_clusters_hit=%.2f\n",
                br.avg_requested_nprobe,
                br.avg_selected_clusters_before_postprocess,
                br.avg_selected_clusters_after_postprocess,
                br.avg_active_clusters_hit);

    return br;
}

BenchmarkResult run_cabs_benchmark_subset(
    const CompositeIndex& cidx, Preprocess& prep,
    int k, float probe_ratio, float c,
    const SubsetBenchmark& sub_ben) {
    (void)c;
    const int qn = std::min(sub_ben.q_num, (int)prep.queries->n);
    const int gt_top = std::min(k, sub_ben.k);
    if (qn <= 0 || gt_top <= 0 || k <= 0) {
        return BenchmarkResult{};
    }

    lsh::timer timer;
    double recall_sum = 0.0, ratio_sum = 0.0;
    long long verified_sum = 0;
    long long active_sum = 0;
    long long req_sum = 0;
    long long before_sum = 0;
    long long after_sum = 0;
    long long hit_sum = 0;

#pragma omp parallel for schedule(dynamic, 1) reduction(+:recall_sum, ratio_sum, verified_sum, active_sum, req_sum, before_sum, after_sum, hit_sum)
    for (int qi = 0; qi < qn; ++qi) {
        const size_t q_begin = prep.queries->indptr[qi];
        const size_t q_end = prep.queries->indptr[qi + 1];
        const int* q_ind = prep.queries->indices + q_begin;
        const float* q_val = prep.queries->val + q_begin;
        const int q_nnz = static_cast<int>(q_end - q_begin);

        size_t checked_local = 0;
        int active_local = 0;
        QueryDebugStat dbg{};
        auto results = query_cabs_impl(cidx, *prep.data, q_ind, q_val, q_nnz, k, probe_ratio, &checked_local, &active_local, &dbg);
        verified_sum += (long long)checked_local;
        active_sum += active_local;
        req_sum += dbg.requested_nprobe;
        before_sum += dbg.selected_clusters_before_postprocess;
        after_sum += dbg.selected_clusters_after_postprocess;
        hit_sum += dbg.active_clusters_hit;

        std::unordered_set<int> gt_set(sub_ben.gt_ids[static_cast<size_t>(qi)].begin(),
                                       sub_ben.gt_ids[static_cast<size_t>(qi)].end());
        int hit = 0;
        for (auto& r : results) {
            if (gt_set.count(r.point_id)) hit++;
        }
        float recall_qi = (float)hit / (float)std::max(1, sub_ben.k);
        recall_sum += recall_qi;

        float ret_sum = 0.0f, gt_sum = 0.0f;
        for (int r = 0; r < std::min(k, (int)results.size()); r++) ret_sum += results[static_cast<size_t>(r)].inner_product;
        for (int r = 0; r < sub_ben.k; r++) gt_sum += sub_ben.gt_ips[static_cast<size_t>(qi)][static_cast<size_t>(r)];
        float ratio_qi = (gt_sum > 1e-9f) ? std::min(1.0f, ret_sum / gt_sum) : 1.0f;
        ratio_sum += ratio_qi;
    }

    double total_t = timer.elapsed();
    BenchmarkResult br;
    br.avg_recall = (float)(recall_sum / std::max(1, qn));
    br.avg_ratio = (float)(ratio_sum / std::max(1, qn));
    br.throughput = (float)((total_t > 0.0) ? ((double)qn / total_t) : 0.0);
    br.avg_candidates = (float)((double)verified_sum / std::max(1, qn));
    br.avg_latency_ms = (float)((total_t > 0.0) ? (total_t * 1000.0 / (double)qn) : 0.0);
    br.avg_active_clusters = (float)((double)active_sum / std::max(1, qn));
    br.avg_requested_nprobe = (float)((double)req_sum / std::max(1, qn));
    br.avg_selected_clusters_before_postprocess = (float)((double)before_sum / std::max(1, qn));
    br.avg_selected_clusters_after_postprocess = (float)((double)after_sum / std::max(1, qn));
    br.avg_active_clusters_hit = (float)((double)hit_sum / std::max(1, qn));

    std::printf("[CABS-Subset] Recall@%d = %.4f, Ratio = %.4f\n", k, br.avg_recall, br.avg_ratio);
    std::printf("[CABS-Subset] Total time = %.4f s, Throughput = %.2f qps\n", total_t, br.throughput);
    std::printf("[CABS-Subset] Avg verified candidates/query = %.2f, Avg active clusters = %.2f\n", br.avg_candidates, br.avg_active_clusters);
    std::printf("[CABS-Subset][routing] requested_nprobe=%.2f, selected_clusters_before_postprocess=%.2f, selected_clusters_after_postprocess=%.2f, avg_active_clusters_hit=%.2f\n",
                br.avg_requested_nprobe,
                br.avg_selected_clusters_before_postprocess,
                br.avg_selected_clusters_after_postprocess,
                br.avg_active_clusters_hit);

    return br;
}

BenchmarkResult run_linear_scan(Preprocess& prep, int k) {
    BenchmarkResult br{};
    const int n = (int)prep.data->n;
    int nq = std::min(prep.benchmark.N, 200);

    if (n <= 0 || nq <= 0 || k <= 0) return br;

    double total_time_ms = 0.0;
    for (int qi = 0; qi < nq; ++qi) {
        const size_t q_start = prep.queries->indptr[qi];
        const size_t q_end = prep.queries->indptr[qi + 1];
        const int q_nnz = (int)(q_end - q_start);
        const int* q_ind = prep.queries->indices + q_start;
        const float* q_val = prep.queries->val + q_start;

        auto st = std::chrono::high_resolution_clock::now();
        std::vector<std::pair<float, int>> all_ips(static_cast<size_t>(n));
        for (int j = 0; j < n; ++j) {
            const size_t d_start = prep.data->indptr[j];
            const size_t d_end = prep.data->indptr[j + 1];
            float ip = sparse_sparse_dot(q_ind, q_val, q_nnz,
                                         prep.data->indices + d_start,
                                         prep.data->val + d_start,
                                         (int)(d_end - d_start));
            all_ips[static_cast<size_t>(j)] = {ip, j};
        }
        int topk = std::min(k, n);
        std::partial_sort(all_ips.begin(), all_ips.begin() + topk, all_ips.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        auto ed = std::chrono::high_resolution_clock::now();
        total_time_ms += std::chrono::duration<double, std::milli>(ed - st).count();
    }

    br.avg_recall = 1.0f;
    br.avg_ratio = 1.0f;
    br.avg_latency_ms = (float)(total_time_ms / std::max(1, nq));
    br.throughput = (total_time_ms > 0.0) ? (float)(nq * 1000.0 / total_time_ms) : 0.0f;
    br.avg_candidates = (float)n;
    br.avg_active_clusters = 1.0f;
    return br;
}
