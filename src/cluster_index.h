#pragma once

#include "def.h"
#include "spherical_kmeans.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct ClusterParams {
    int cluster_id;
    int n_i;
    int l_i;
    int m_i;
    int T_i;
    float t_i;
};

struct DimLStats {
    int min_l = 0;
    int max_l = 0;
    float avg_l = 0.0f;
    int total_l = 0;
};

struct IDFWeights {
    std::vector<float> idf;
    std::vector<int> dim_l;      // dim_l[j]
    std::vector<int> prefix_l;   // prefix_l[j]
    int total_l = 0;
    int dim = 0;
    int n = 0;
    DimLStats stats;
};

IDFWeights compute_idf_weights(
    const SparseData& data, int n, int dim, float l_base = 20.0f,
    float idf_min = 0.5f, float idf_max = 2.0f
);

ClusterParams compute_adaptive_params(
    int cluster_id, int n_i, int k, int n_global,
    float c = 0.5f, float l_base = 20.0f, float eta = 0.3f,
    int m0_base = -1
);

struct SingleClusterIndex {
    struct HashFunc {
        uint64_t a;
        uint64_t b;
    };
    static const uint64_t HASH_PRIME = 2147483647ULL;

    ClusterParams params;
    std::vector<float> centroid;
    std::vector<int> point_ids;

    std::vector<std::vector<int>> local_sets;
    std::vector<std::vector<int>> local_hashvals;
    std::vector<HashFunc> hash_funcs;
    std::vector<std::unordered_map<int, std::vector<int>>> local_indexes;
    std::vector<int> local_set_sizes;
    std::vector<int> local_sorted_norms;

    int hash_domain = 0;

    float avg_effective_l = 0.0f;
    int num_active_dims = 0;

    static inline int compute_hash(uint64_t a, uint64_t b, int elem, int domain) {
        return (int)(((a * (uint64_t)elem + b) % HASH_PRIME) % (uint64_t)domain);
    }
};


struct BuildTimeBreakdown {
    double idf_computation_s = 0.0;
    double spill_processing_s = 0.0;
    double sos_transform_s = 0.0;
    double minhash_compute_s = 0.0;
    double bucket_indexing_s = 0.0;
    double total_build_s = 0.0;
};

struct CompositeIndex {
    std::vector<SingleClusterIndex> cluster_indexes;
    int K = 0;
    int dim = 0;
    int total_points = 0;
    int dim_limit = -1;
    bool use_idf = true;
    bool use_adaptive_l = true;
    IDFWeights idf_weights;
};

CompositeIndex build_composite_index(
    const SparseData& data, int n, int dim,
    const ClusterResult& clustering, int k,
    float c = 0.5f, float l_base = 20.0f, float eta = 0.3f,
    int dim_limit = -1,
    bool use_idf = true,
    bool use_adaptive_l = true,
    float spill_factor = 0.0f,
    float idf_min = 0.5f,
    float idf_max = 2.0f,
    int m0_base = -1,
    bool use_adaptive_params = true,
    BuildTimeBreakdown* breakdown = nullptr
);
