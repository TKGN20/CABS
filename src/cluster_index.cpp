#include "cluster_index.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
static inline uint32_t fast_rand(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

static inline float fast_randf(uint32_t& state) {
    return static_cast<float>(fast_rand(state) & 0x7FFFFFu) / static_cast<float>(0x800000u);
}
}  // namespace

IDFWeights compute_idf_weights(const SparseData& data, int n, int dim, float l_base, float idf_min, float idf_max) {
    IDFWeights w;
    w.dim = dim;
    w.n = n;
    w.idf.assign(static_cast<size_t>(std::max(0, dim)), 0.0f);

    std::vector<int> df(static_cast<size_t>(std::max(0, dim)), 0);
    for (int i = 0; i < n; i++) {
        const size_t start = data.indptr[i];
        const size_t end = data.indptr[i + 1];
        for (size_t p = start; p < end; p++) {
            const int idx = data.indices[p];
            if (idx >= 0 && idx < dim) df[static_cast<size_t>(idx)]++;
        }
    }

    for (int j = 0; j < dim; j++) {
        w.idf[static_cast<size_t>(j)] = logf((float)std::max(n, 1) / (float)(df[static_cast<size_t>(j)] + 1)) + 1.0f;
    }

    float max_idf = *std::max_element(w.idf.begin(), w.idf.end());
    float min_idf_raw = *std::min_element(w.idf.begin(), w.idf.end());
    float range = std::max(1e-6f, max_idf - min_idf_raw);
    float lo = std::min(idf_min, idf_max);
    float hi = std::max(idf_min, idf_max);
    if (fabsf(hi - lo) < 1e-6f) {
        for (int j = 0; j < dim; j++) w.idf[static_cast<size_t>(j)] = lo;
    } else {
        for (int j = 0; j < dim; j++) {
            w.idf[static_cast<size_t>(j)] = lo + (hi - lo) * (w.idf[static_cast<size_t>(j)] - min_idf_raw) / range;
        }
    }

    w.dim_l.resize(static_cast<size_t>(dim));
    w.prefix_l.assign(static_cast<size_t>(dim + 1), 0);
    for (int j = 0; j < dim; j++) {
        w.dim_l[static_cast<size_t>(j)] = std::max(3, std::min(40, (int)roundf(l_base * w.idf[static_cast<size_t>(j)])));
        w.prefix_l[static_cast<size_t>(j + 1)] = w.prefix_l[static_cast<size_t>(j)] + w.dim_l[static_cast<size_t>(j)];
    }
    w.total_l = w.prefix_l[static_cast<size_t>(dim)];

    if (!w.dim_l.empty()) {
        w.stats.min_l = *std::min_element(w.dim_l.begin(), w.dim_l.end());
        w.stats.max_l = *std::max_element(w.dim_l.begin(), w.dim_l.end());
        float sum_l = 0.0f;
        int count = 0;
        for (int j = 0; j < dim; ++j) {
            if (w.dim_l[static_cast<size_t>(j)] > 0) {
                sum_l += (float)w.dim_l[static_cast<size_t>(j)];
                ++count;
            }
        }
        w.stats.avg_l = (count > 0) ? (sum_l / (float)count) : 0.0f;
        w.stats.total_l = w.total_l;
    }

    return w;
}

ClusterParams compute_adaptive_params(
    int cluster_id, int n_i, int k, int n_global,
    float c, float l_base, float eta, int m0_base) {
    ClusterParams p;
    p.cluster_id = cluster_id;
    p.n_i = n_i;

    p.l_i = std::max(3, (int)l_base);

    const float sc = sqrtf(std::max(0.0f, c));
    p.t_i = ((sc + 1.0f) / 2.0f) * ((sc + 1.0f) / 2.0f);

    int base_m;
    if (m0_base > 0) {
        base_m = m0_base;
    } else if (c >= 0.85f)
        base_m = 200;
    else if (c >= 0.7f)
        base_m = 170;
    else if (c >= 0.5f)
        base_m = 150;
    else
        base_m = 120;

    const int safe_n_i = std::max(n_i, 2);
    const int safe_n_global = std::max(n_global, 2);
    const double ln_ref = std::log(200000.0);  // ln(2 * 100K)
    const double ln_2n = std::log(2.0 * (double)safe_n_global);
    const double n_scale = std::max(1.0, ln_2n / ln_ref);
    const int m_global = (int)std::ceil((double)base_m * n_scale);

    float log_ratio = (float)(std::log(2.0 * (double)safe_n_i) / ln_2n);
    log_ratio = std::max(0.3f, std::min(1.0f, log_ratio));

    p.m_i = std::max(15, std::min(320, (int)std::ceil((double)m_global * (double)log_ratio)));
    p.T_i = std::max(k * 5, (int)ceilf(eta * n_i + k));

    return p;
}

CompositeIndex build_composite_index(
    const SparseData& data,
    int n,
    int dim,
    const ClusterResult& clustering,
    int k,
    float c,
    float l_base,
    float eta,
    int dim_limit,
    bool use_idf,
    bool use_adaptive_l,
    float spill_factor,
    float idf_min,
    float idf_max,
    int m0_base,
    bool use_adaptive_params,
    BuildTimeBreakdown* breakdown) {
    CompositeIndex out{};
    const auto t_total_begin = std::chrono::high_resolution_clock::now();
    double t_idf = 0.0, t_spill = 0.0, t_sos = 0.0, t_mh = 0.0, t_bucket = 0.0;
    ClusterResult clustering_work = clustering;
    if (spill_factor > 0.0f) {
        auto ts = std::chrono::high_resolution_clock::now();
        apply_boundary_spill(clustering_work, data, n, dim, spill_factor);
        auto te = std::chrono::high_resolution_clock::now();
        t_spill += std::chrono::duration<double>(te - ts).count();
    }
    out.K = clustering_work.K;
    out.dim = dim;
    out.total_points = n;
    out.dim_limit = dim_limit;
    out.use_idf = use_idf;
    out.use_adaptive_l = use_adaptive_l;

    const int active_dim = (dim_limit > 0) ? std::min(dim, dim_limit) : dim;

    IDFWeights idf_w;
    if (use_idf || use_adaptive_l) {
        auto ts = std::chrono::high_resolution_clock::now();
        idf_w = compute_idf_weights(data, n, dim, l_base, idf_min, idf_max);
        auto te = std::chrono::high_resolution_clock::now();
        t_idf += std::chrono::duration<double>(te - ts).count();
    } else {
        idf_w.dim = dim;
        idf_w.n = n;
        idf_w.idf.assign(static_cast<size_t>(std::max(0, dim)), 1.0f);
        idf_w.dim_l.assign(static_cast<size_t>(std::max(0, dim)), std::max(3, (int)l_base));
        idf_w.prefix_l.assign(static_cast<size_t>(std::max(0, dim) + 1), 0);
        for (int j = 0; j < dim; ++j) {
            idf_w.prefix_l[static_cast<size_t>(j + 1)] = idf_w.prefix_l[static_cast<size_t>(j)] + idf_w.dim_l[static_cast<size_t>(j)];
        }
        idf_w.total_l = idf_w.prefix_l[static_cast<size_t>(dim)];
    }

    if (!use_adaptive_l) {
        const int l_fixed = std::max(3, (int)l_base);
        idf_w.dim_l.assign(static_cast<size_t>(std::max(0, dim)), l_fixed);
        idf_w.prefix_l.assign(static_cast<size_t>(std::max(0, dim) + 1), 0);
        for (int j = 0; j < dim; ++j) {
            idf_w.prefix_l[static_cast<size_t>(j + 1)] = idf_w.prefix_l[static_cast<size_t>(j)] + l_fixed;
        }
        idf_w.total_l = idf_w.prefix_l[static_cast<size_t>(dim)];
    }

    if (!idf_w.dim_l.empty()) {
        idf_w.stats.min_l = *std::min_element(idf_w.dim_l.begin(), idf_w.dim_l.end());
        idf_w.stats.max_l = *std::max_element(idf_w.dim_l.begin(), idf_w.dim_l.end());
        float sum_l_stats = 0.0f;
        int cnt_l_stats = 0;
        for (int j = 0; j < dim; ++j) {
            if (idf_w.dim_l[static_cast<size_t>(j)] > 0) {
                sum_l_stats += (float)idf_w.dim_l[static_cast<size_t>(j)];
                ++cnt_l_stats;
            }
        }
        idf_w.stats.avg_l = (cnt_l_stats > 0) ? (sum_l_stats / (float)cnt_l_stats) : 0.0f;
        idf_w.stats.total_l = idf_w.total_l;
    }

    // Build cluster points with optional boundary spill
    std::vector<std::vector<int>> cluster_points(static_cast<size_t>(std::max(0, out.K)));
    if (!clustering_work.point_to_cluster.empty()) {
        for (int j = 0; j < n && j < (int)clustering_work.point_to_cluster.size(); ++j) {
            int c = clustering_work.point_to_cluster[j];
            if (c >= 0 && c < out.K) cluster_points[static_cast<size_t>(c)].push_back(j);
            auto it = clustering_work.spill_map.find(j);
            if (it != clustering_work.spill_map.end()) {
                for (int sc : it->second) {
                    if (sc >= 0 && sc < out.K) cluster_points[static_cast<size_t>(sc)].push_back(j);
                }
            }
        }
    } else {
        for (int c = 0; c < out.K; ++c) {
            cluster_points[static_cast<size_t>(c)] = clustering_work.clusters[static_cast<size_t>(c)];
        }
    }
    #pragma omp parallel for schedule(dynamic, 8)
    for (int c = 0; c < out.K; ++c) {
        auto& pts = cluster_points[static_cast<size_t>(c)];
        std::sort(pts.begin(), pts.end());
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    }

    out.idf_weights = idf_w;
    out.cluster_indexes.resize(static_cast<size_t>(std::max(0, out.K)));

    int max_m = 0;
    long long sum_m = 0;
    long long adaptive_mn = 0;

    const int hash_domain = (dim <= 0) ? 1 :
        (use_adaptive_l ? (idf_w.total_l + 1)
                        : (dim * std::max(3, (int)l_base) + 1));

    for (int i = 0; i < out.K; ++i) {
        SingleClusterIndex& sci = out.cluster_indexes[static_cast<size_t>(i)];
        sci.point_ids = cluster_points[static_cast<size_t>(i)];
        sci.centroid = clustering_work.centroids[static_cast<size_t>(i)];

        const int n_i = static_cast<int>(sci.point_ids.size());
        if (use_adaptive_params) {
            sci.params = compute_adaptive_params(i, n_i, k, n, c, l_base, eta, m0_base);
        } else {
            sci.params.cluster_id = i;
            sci.params.n_i = n_i;
            sci.params.l_i = std::max(3, (int)l_base);
            sci.params.m_i = (m0_base > 0) ? m0_base : 150;
            sci.params.T_i = std::max(k * 5, (int)ceilf(eta * n + k));
            sci.params.t_i = 0.0f;
        }
        sci.hash_domain = std::max(2, hash_domain);

        const int m_i = sci.params.m_i;
        max_m = std::max(max_m, m_i);
        sum_m += m_i;
        adaptive_mn += (long long)m_i * (long long)n_i;

        sci.local_sets.assign(static_cast<size_t>(n_i), {});
        sci.local_set_sizes.assign(static_cast<size_t>(n_i), 0);
        sci.local_sorted_norms.assign(static_cast<size_t>(n_i), 0);

        uint32_t seed_base = static_cast<uint32_t>(i * 10007 + 1337);
        std::vector<unsigned char> active_dim_mask(static_cast<size_t>(std::max(0, active_dim)), 0);

                auto t_sos_begin = std::chrono::high_resolution_clock::now();
        for (int li = 0; li < n_i; ++li) {
            const int pid = sci.point_ids[static_cast<size_t>(li)];
            if (pid < 0 || pid + 1 > (int)data.n) continue;

            const size_t begin = data.indptr[pid];
            const size_t end = data.indptr[pid + 1];
            if (end < begin || end > data.nnz) continue;

            float max_val = 0.0f;
            for (size_t p = begin; p < end; ++p) max_val = std::max(max_val, data.val[p]);
            if (max_val <= 0.0f) max_val = 1.0f;

            std::vector<int>& local_set = sci.local_sets[static_cast<size_t>(li)];
            uint32_t rng_state = seed_base + static_cast<uint32_t>(pid * 12345 + 17);

            for (size_t p = begin; p < end; ++p) {
                const int idx = data.indices[p];
                const float val = data.val[p];
                if (val <= 0.0f) continue;
                if (idx < 0 || idx >= active_dim) continue;
                active_dim_mask[static_cast<size_t>(idx)] = 1;

                const float norm_val = std::min(1.0f, std::max(0.0f, val / max_val));
                const float weighted_prob = use_idf
                    ? std::min(1.0f, norm_val * idf_w.idf[static_cast<size_t>(idx)])
                    : norm_val;
                if (weighted_prob <= 0.0f) continue;

                const int l_j = use_adaptive_l ? idf_w.dim_l[static_cast<size_t>(idx)] : std::max(3, (int)l_base);
                const int start_pos = use_adaptive_l ? idf_w.prefix_l[static_cast<size_t>(idx)]
                                                     : idx * std::max(3, (int)l_base);

                const int upper = std::min(sci.hash_domain, start_pos + l_j);
                for (int slot = start_pos; slot < upper; ++slot) {
                    if (fast_randf(rng_state) < weighted_prob) local_set.push_back(slot);
                }
            }

            std::sort(local_set.begin(), local_set.end());
            local_set.erase(std::unique(local_set.begin(), local_set.end()), local_set.end());
            sci.local_set_sizes[static_cast<size_t>(li)] = static_cast<int>(local_set.size());
            sci.local_sorted_norms[static_cast<size_t>(li)] = static_cast<int>(local_set.size());
        }
        auto t_sos_end = std::chrono::high_resolution_clock::now();
        t_sos += std::chrono::duration<double>(t_sos_end - t_sos_begin).count();

        auto t_mh_begin = std::chrono::high_resolution_clock::now();
        sci.hash_funcs.resize(static_cast<size_t>(m_i));
        std::mt19937_64 rng_hash(static_cast<uint64_t>(i) * 10007ULL + 42ULL);
        std::uniform_int_distribution<uint64_t> dist_coeff(1, SingleClusterIndex::HASH_PRIME - 1);
        std::uniform_int_distribution<uint64_t> dist_offset(0, SingleClusterIndex::HASH_PRIME - 1);
        for (int h = 0; h < m_i; ++h) {
            sci.hash_funcs[static_cast<size_t>(h)].a = dist_coeff(rng_hash);
            sci.hash_funcs[static_cast<size_t>(h)].b = dist_offset(rng_hash);
        }

        sci.local_hashvals.assign(static_cast<size_t>(n_i), std::vector<int>(static_cast<size_t>(m_i), std::numeric_limits<int>::max()));

        #pragma omp parallel for schedule(dynamic, 4)
        for (int h = 0; h < m_i; ++h) {
            const uint64_t a = sci.hash_funcs[static_cast<size_t>(h)].a;
            const uint64_t b = sci.hash_funcs[static_cast<size_t>(h)].b;
            for (int li = 0; li < n_i; ++li) {
                const std::vector<int>& s = sci.local_sets[static_cast<size_t>(li)];
                int best = std::numeric_limits<int>::max();
                for (int elem : s) {
                    const int hv = SingleClusterIndex::compute_hash(a, b, elem, sci.hash_domain);
                    if (hv < best) best = hv;
                }
                if (best == std::numeric_limits<int>::max()) best = sci.hash_domain - 1;
                sci.local_hashvals[static_cast<size_t>(li)][static_cast<size_t>(h)] = best;
            }
        }

        auto t_mh_end = std::chrono::high_resolution_clock::now();
        t_mh += std::chrono::duration<double>(t_mh_end - t_mh_begin).count();

        auto t_bucket_begin = std::chrono::high_resolution_clock::now();
        sci.local_indexes.resize(static_cast<size_t>(m_i));
        #pragma omp parallel for schedule(dynamic, 4)
        for (int h = 0; h < m_i; ++h) {
            auto& table = sci.local_indexes[static_cast<size_t>(h)];
            table.reserve(static_cast<size_t>(std::max(1, n_i)));
            for (int li = 0; li < n_i; ++li) {
                const int key = sci.local_hashvals[static_cast<size_t>(li)][static_cast<size_t>(h)];
                table[key].push_back(li);
            }
        }
        auto t_bucket_end = std::chrono::high_resolution_clock::now();
        t_bucket += std::chrono::duration<double>(t_bucket_end - t_bucket_begin).count();

        float sum_effective_l = 0.0f;
        int cnt_effective_l = 0;
        for (int d = 0; d < active_dim; ++d) {
            if (active_dim_mask[static_cast<size_t>(d)]) {
                sum_effective_l += (float)idf_w.dim_l[static_cast<size_t>(d)];
                ++cnt_effective_l;
            }
        }
        sci.num_active_dims = cnt_effective_l;
        sci.avg_effective_l = (cnt_effective_l > 0) ? (sum_effective_l / (float)cnt_effective_l) : 0.0f;

        sci.local_sets.clear();
        sci.local_sets.shrink_to_fit();
    }

    const long long global_mn = (long long)max_m * (long long)n;
    std::printf("Adaptive m_i sum = %lld, baseline global m*n = %lld, adaptive sum(m_i*n_i) = %lld\n",
                sum_m, global_mn, adaptive_mn);

    const auto t_total_end = std::chrono::high_resolution_clock::now();
    const double t_total = std::chrono::duration<double>(t_total_end - t_total_begin).count();
    if (breakdown) {
        breakdown->idf_computation_s = t_idf;
        breakdown->spill_processing_s = t_spill;
        breakdown->sos_transform_s = t_sos;
        breakdown->minhash_compute_s = t_mh;
        breakdown->bucket_indexing_s = t_bucket;
        breakdown->total_build_s = t_total;
    }

    return out;
}
