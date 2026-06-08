#include "spherical_kmeans.h"
#include "basis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_set>

float sparse_dense_dot(
    const int* indices,
    const float* vals,
    int nnz,
    const std::vector<float>& dense
) {
    float sum = 0.0f;
    for (int j = 0; j < nnz; ++j) {
        sum += vals[j] * dense[indices[j]];
    }
    return sum;
}

static void l2_normalize(std::vector<float>& v) {
    float norm_sq = 0.0f;
    for (float x : v) norm_sq += x * x;
    if (norm_sq <= 0.0f) return;
    const float inv_norm = 1.0f / std::sqrt(norm_sq);
    for (float& x : v) x *= inv_norm;
}



void apply_boundary_spill(
    ClusterResult& cr,
    const SparseData& data,
    int n,
    int dim,
    float spill_factor
) {
    (void)dim;
    cr.spill_map.clear();
    cr.spill_threshold = spill_factor;
    if (spill_factor <= 0.0f || cr.K <= 1) {
        return;
    }

    int spill_count = 0;
    for (int j = 0; j < n; ++j) {
        if (j < 0 || j >= (int)cr.point_to_cluster.size()) continue;
        int best_cluster = cr.point_to_cluster[j];
        if (best_cluster < 0 || best_cluster >= cr.K) continue;

        int start = (int)data.indptr[j];
        int end = (int)data.indptr[j + 1];
        int nnz = end - start;
        const int* ind = data.indices + start;
        const float* val = data.val + start;

        float best_score = sparse_dense_dot(ind, val, nnz, cr.centroids[best_cluster]);
        if (best_score <= 0.0f) continue;

        for (int c = 0; c < cr.K; ++c) {
            if (c == best_cluster) continue;
            float score = sparse_dense_dot(ind, val, nnz, cr.centroids[c]);
            if (score >= best_score * spill_factor) {
                cr.spill_map[j].push_back(c);
                ++spill_count;
            }
        }
    }

    std::printf("  Boundary spill: %d points spilled (%.1f%% of n=%d, factor=%.2f)\n",
        spill_count, 100.0f * (float)spill_count / (float)std::max(1, n), n, spill_factor);
}
ClusterResult spherical_kmeans_seeded(
    const SparseData& data,
    int n,
    int dim,
    int K,
    int max_iter,
    float tol,
    bool verbose,
    int seed
) {
    if (n <= 0 || dim <= 0 || K <= 0) {
        throw std::invalid_argument("spherical_kmeans: n, dim, K must be positive");
    }
    if (K > n) {
        throw std::invalid_argument("spherical_kmeans: K cannot be larger than n");
    }

    lsh::timer all_timer;
    std::printf("[spherical_kmeans] K=%d, n=%d, dim=%d started\n", K, n, dim);

    std::mt19937 rng(seed >= 0 ? static_cast<uint32_t>(seed) : std::random_device{}());
    std::uniform_int_distribution<int> pick_point(0, n - 1);

    std::vector<std::vector<float>> centroids(K, std::vector<float>(dim, 0.0f));
    std::unordered_set<int> chosen;
    chosen.reserve(static_cast<size_t>(K) * 2U);

    for (int c = 0; c < K; ++c) {
        int idx = pick_point(rng);
        while (chosen.find(idx) != chosen.end()) idx = pick_point(rng);
        chosen.insert(idx);

        const size_t begin = data.indptr[idx];
        const size_t end = data.indptr[idx + 1];
        for (size_t p = begin; p < end; ++p) {
            centroids[c][data.indices[p]] = data.val[p];
        }
        l2_normalize(centroids[c]);
    }

    std::vector<int> point_to_cluster(n, -1);
    std::vector<std::vector<int>> clusters(K);

    for (int t = 0; t < max_iter; ++t) {
        for (auto& cl : clusters) cl.clear();

        for (int j = 0; j < n; ++j) {
            const size_t begin = data.indptr[j];
            const size_t end = data.indptr[j + 1];
            const int nnz_j = static_cast<int>(end - begin);
            const int* idx_ptr = data.indices + begin;
            const float* val_ptr = data.val + begin;

            float best_score = -std::numeric_limits<float>::infinity();
            int best_cluster = 0;
            for (int c = 0; c < K; ++c) {
                const float score = sparse_dense_dot(idx_ptr, val_ptr, nnz_j, centroids[c]);
                if (score > best_score) {
                    best_score = score;
                    best_cluster = c;
                }
            }
            point_to_cluster[j] = best_cluster;
            clusters[best_cluster].push_back(j);
        }

        int min_size = std::numeric_limits<int>::max();
        int max_size = 0;
        long long total_size = 0;
        for (int c = 0; c < K; ++c) {
            const int sz = static_cast<int>(clusters[c].size());
            min_size = std::min(min_size, sz);
            max_size = std::max(max_size, sz);
            total_size += sz;
        }
        const double avg_size = static_cast<double>(total_size) / static_cast<double>(K);
        if (verbose) {
            std::printf("[spherical_kmeans] iter %d: cluster size min=%d max=%d avg=%.2f\n",
                        t, min_size, max_size, avg_size);
        }

        std::vector<std::vector<float>> new_centroids(K, std::vector<float>(dim, 0.0f));
        for (int c = 0; c < K; ++c) {
            if (clusters[c].empty()) {
                new_centroids[c] = centroids[c];
                continue;
            }
            for (int pid : clusters[c]) {
                const size_t begin = data.indptr[pid];
                const size_t end = data.indptr[pid + 1];
                for (size_t p = begin; p < end; ++p) {
                    new_centroids[c][data.indices[p]] += data.val[p];
                }
            }
            l2_normalize(new_centroids[c]);
        }

        float max_shift = 0.0f;
        for (int c = 0; c < K; ++c) {
            float diff_sq = 0.0f;
            for (int d = 0; d < dim; ++d) {
                const float diff = new_centroids[c][d] - centroids[c][d];
                diff_sq += diff * diff;
            }
            max_shift = std::max(max_shift, std::sqrt(diff_sq));
        }

        centroids.swap(new_centroids);
        if (max_shift < tol) {
            if (verbose) {
                std::printf("[spherical_kmeans] converged at iter %d (max centroid shift=%.6f)\n", t, max_shift);
            }
            break;
        }
    }

    ClusterResult result;
    result.clusters = std::move(clusters);
    result.centroids = std::move(centroids);
    result.point_to_cluster = std::move(point_to_cluster);
    result.K = K;
    result.dim = dim;

    int min_size = n;
    int max_size = 0;
    for (const auto& cl : result.clusters) {
        const int sz = static_cast<int>(cl.size());
        min_size = std::min(min_size, sz);
        max_size = std::max(max_size, sz);
    }
    std::printf("[spherical_kmeans] done in %.2fs, sizes: min=%d max=%d\n", all_timer.elapsed(), min_size, max_size);
    return result;
}


ClusterResult spherical_kmeans(
    const SparseData& data,
    int n,
    int dim,
    int K,
    int max_iter,
    float tol,
    bool verbose
) {
    return spherical_kmeans_seeded(data, n, dim, K, max_iter, tol, verbose, -1);
}
