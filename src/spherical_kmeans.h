#pragma once

#include "def.h"
#include <vector>
#include <unordered_map>

struct ClusterResult {
    std::vector<std::vector<int>> clusters;
    std::vector<std::vector<float>> centroids;
    std::vector<int> point_to_cluster;
    std::unordered_map<int, std::vector<int>> spill_map;
    float spill_threshold = 0.0f;
    int K = 0;
    int dim = 0;
};

ClusterResult spherical_kmeans(
    const SparseData& data,
    int n,
    int dim,
    int K,
    int max_iter = 30,
    float tol = 1e-4f,
    bool verbose = false
);

float sparse_dense_dot(
    const int* indices,
    const float* vals,
    int nnz,
    const std::vector<float>& dense
);

void apply_boundary_spill(
    ClusterResult& cr,
    const SparseData& data,
    int n,
    int dim,
    float spill_factor = 0.8f
);

ClusterResult spherical_kmeans_seeded(
    const SparseData& data,
    int n,
    int dim,
    int K,
    int max_iter,
    float tol,
    bool verbose,
    int seed
);
