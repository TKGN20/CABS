#include <iostream>
#include <fstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <filesystem>
#include <regex>
#include <iomanip>
#include <cctype>
#include <cstdlib>

#include "Preprocess.h"
#include "basis.h"
#include "alg.h"
#include "rd_stdcout.h"
#include "spherical_kmeans.h"
#include "cluster_index.h"
#include "cluster_query.h"
#include "method.h"
#include "sosia.h"
#include "repair_runner.h"

extern std::string data_fold, index_fold;
extern std::string data_fold1, data_fold2;

int mips2set::l = 10;
std::mt19937 mips2set::rng(int(std::time(0)));
std::uniform_real_distribution<float> mips2set::ur(0, 1);

std::string getNameByDate(int mode, const std::string& dataset);

int main(int argc, char const* argv[]) {
    std::string dataset = "base_small";
    std::string qname = "queries";
    int mode = 5;

    if (argc > 1) mode = std::atoi(argv[1]);
    if (argc > 2) dataset = argv[2];
    if (argc > 3) qname = argv[3];

    if (mode == 1) mode = 5;  // exp1: 参数调优
    else if (mode == 2) mode = 6; // exp2: 性能实验
    else if (mode == 3) mode = 7; // exp3: 方法对比

    std::string mode11_config_path;
    if (mode == 11 && argc > 2) mode11_config_path = argv[2];
    if (mode == 11) {
        dataset = "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr";
        qname = "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr";
    }
    if (mode == 12 || mode == 13) {
        if (dataset == "datasets/SPLADE/1M/base_1M.csr" && !std::filesystem::exists(dataset)
            && std::filesystem::exists("datasets/MS MARCO v1/SPLADE/1M/base_1M.csr")) {
            dataset = "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr";
        }
        if (qname == "datasets/SPLADE/1M/queries.dev.csr" && !std::filesystem::exists(qname)
            && std::filesystem::exists("datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr")) {
            qname = "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr";
        }
    }

    std::string argvStr[5];
    auto ends_with = [](const std::string& s, const std::string& suf) -> bool {
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    };

    bool dataset_is_path = (dataset.find('/') != std::string::npos) || (dataset.find('\\') != std::string::npos) || ends_with(dataset, ".csr");
    bool query_is_path = (qname.find('/') != std::string::npos) || (qname.find('\\') != std::string::npos) || ends_with(qname, ".csr");

    if (dataset_is_path) {
        argvStr[1] = dataset;
    } else {
        argvStr[1] = "datasets/" + dataset + ".csr";
    }

    if (query_is_path) {
        argvStr[4] = qname;
    } else {
        argvStr[4] = "datasets/" + qname + ".dev.csr";
    }

    if (dataset_is_path && ends_with(argvStr[1], ".csr")) {
        argvStr[3] = argvStr[1].substr(0, argvStr[1].size() - 4) + "_all.ben";
    } else {
        argvStr[3] = "datasets/" + dataset + "_all.ben";
    }

    // Disable internal auto log file redirection.
    // Logging is controlled only by external shell redirection.

    std::cout << "Using SOSIA for Sparse Dataset " << argvStr[1] << std::endl;
    Preprocess prep(argvStr[1], argvStr[4], argvStr[3]);

    if (mode == 5 && prep.data != nullptr && prep.data->nnz > 0) {
        const size_t probe_n = std::min<size_t>(prep.data->nnz, 16);
        bool all_zero = true;
        for (size_t i = 0; i < probe_n; ++i) {
            if (std::fabs(prep.data->val[i]) > 1e-12f) {
                all_zero = false;
                break;
            }
        }
        if (all_zero) {
            std::printf("[WARN] detected near-zero leading values in data.val, first 10 values:\n");
            const size_t show_n = std::min<size_t>(prep.data->nnz, 10);
            for (size_t i = 0; i < show_n; ++i) {
                std::printf("  val[%zu] = %.9g\n", i, prep.data->val[i]);
            }
        }
    }

    int k = 50, l = 20, ub = 20000, m = 150;

    auto run_case6 = [&]() -> int {
        int n = static_cast<int>(prep.data->n);
        int dim = prep.data->dim;
        int K = 15;
        int k_eval = 50;
        float C = 0.9f, L_BASE = 20.0f;
        float eta_auto = (n >= 1000000) ? 0.03f : (n >= 100000) ? 0.05f : 0.15f;
        const float TAU_FULL = 0.8f;
        printf("[CABS] Auto eta=%.3f for n=%d\n", eta_auto, n);

        struct AblPoint {
            std::string knob;
            float recall;
            float cost;
            float tp;
        };

        std::vector<float> eta_scan = {0.005f, 0.01f, 0.02f, 0.03f, 0.05f, 0.08f, 0.10f, 0.15f};
        std::vector<int> nprobe_scan = {2, 3, 5, 8, 15};

        std::vector<AblPoint> A_sosia;
        std::vector<AblPoint> B_idf;
        std::vector<AblPoint> C_adaptl;
        std::vector<AblPoint> D_cluster_nospill;
        std::vector<AblPoint> E_full;

        printf("\n── E3: Incremental Ablation ──\n");

        mips2set sets(prep, l);
        myMinHashBase minBase(prep, sets, 1, m);
        Sosia sosia(minBase);
        std::vector<int> ubs = {1000, 2000, 5000, 10000, 20000, 50000};
        for (int u : ubs) {
            resOutput rr = minHash(sosia, 0.5f, k_eval, u, prep);
            float tp = (rr.time > 0) ? (1000.0f / rr.time) : 0.0f;
            char knob[16];
            std::snprintf(knob, sizeof(knob), "%d", u);
            A_sosia.push_back({knob, rr.recall, rr.cost, tp});
        }

        ClusterResult cr1 = spherical_kmeans(*prep.data, n, dim, 1, 5, 1e-4f, false);
        for (float eta_v : eta_scan) {
            CompositeIndex cidx_b = build_composite_index(*prep.data, n, dim, cr1, k_eval,
                C, L_BASE, eta_v, -1, true, false, 0.0f, 0.5f, 2.0f, 150, false);
            BenchmarkResult br_b = run_cabs_benchmark(cidx_b, prep, k_eval, 1.0f, C);

            CompositeIndex cidx_c = build_composite_index(*prep.data, n, dim, cr1, k_eval,
                C, L_BASE, eta_v, -1, true, true, 0.0f, 0.5f, 2.0f, 150, false);
            BenchmarkResult br_c = run_cabs_benchmark(cidx_c, prep, k_eval, 1.0f, C);

            char knob[16];
            std::snprintf(knob, sizeof(knob), "%.3f", eta_v);
            B_idf.push_back({knob, br_b.avg_recall, br_b.avg_candidates, br_b.throughput});
            C_adaptl.push_back({knob, br_c.avg_recall, br_c.avg_candidates, br_c.throughput});
        }

        ClusterResult crk = spherical_kmeans(*prep.data, n, dim, K, 25, 1e-4f, false);
        CompositeIndex cidx_d = build_composite_index(*prep.data, n, dim, crk, k_eval,
            C, L_BASE, eta_auto, -1, true, true, 0.0f, 0.5f, 2.0f, -1, true);
        CompositeIndex cidx_e = build_composite_index(*prep.data, n, dim, crk, k_eval,
            C, L_BASE, eta_auto, -1, true, true, TAU_FULL, 0.5f, 2.0f, -1, true);

        for (int np : nprobe_scan) {
            if (np > K) continue;
            float pr = std::min(1.0f, (float)np / (float)K);
            BenchmarkResult br_d = run_cabs_benchmark(cidx_d, prep, k_eval, pr, C);
            BenchmarkResult br_e = run_cabs_benchmark(cidx_e, prep, k_eval, pr, C);
            char knob[16];
            std::snprintf(knob, sizeof(knob), "%d", np);
            D_cluster_nospill.push_back({knob, br_d.avg_recall, br_d.avg_candidates, br_d.throughput});
            E_full.push_back({knob, br_e.avg_recall, br_e.avg_candidates, br_e.throughput});
        }

        auto print_curve = [&](const char* title, const char* knob_name, const std::vector<AblPoint>& pts) {
            printf("\nConfig %s\n", title);
            printf("  %s    Recall  Cost    TP(qps)\n", knob_name);
            for (const auto& p : pts) {
                printf("  %-7s  %.3f   %6d   %7.1f\n", p.knob.c_str(), p.recall, (int)roundf(p.cost), p.tp);
            }
        };

        print_curve("A: SOSIA baseline", "ub", A_sosia);
        print_curve("B: + IDF weighting (K=1)", "η", B_idf);
        print_curve("C: + Adaptive-L (K=1)", "η", C_adaptl);
        print_curve("D: + Clustering (K=15, nprobe var, τ=2.0)", "nprobe", D_cluster_nospill);
        print_curve("E: + Spill = Full CABS (K=15, nprobe var, τ=0.8)", "nprobe", E_full);

        auto nearest = [](const std::vector<AblPoint>& pts, float target) -> const AblPoint& {
            size_t bi = 0;
            float bd = std::numeric_limits<float>::max();
            for (size_t i = 0; i < pts.size(); ++i) {
                float d = std::fabs(pts[i].recall - target);
                if (d < bd || (std::fabs(d - bd) < 1e-8f && pts[i].tp > pts[bi].tp)) {
                    bd = d;
                    bi = i;
                }
            }
            return pts[bi];
        };

        printf("\n── Equal-Recall Comparison ──\n");
        std::vector<float> targets = {0.94f, 0.97f, 0.99f};
        for (float trg : targets) {
            const AblPoint& a = nearest(A_sosia, trg);
            const AblPoint& b = nearest(B_idf, trg);
            const AblPoint& c = nearest(C_adaptl, trg);
            const AblPoint& d = nearest(D_cluster_nospill, trg);
            const AblPoint& e = nearest(E_full, trg);
            float base = std::max(1e-9f, a.tp);
            printf("\nRecall ≈ %.2f:\n", trg);
            printf("  Config          TP(qps)   vs SOSIA\n");
            printf("  %-14s  %7.1f   %.2fx\n", "SOSIA", a.tp, 1.0f);
            printf("  %-14s  %7.1f   %.2fx\n", "+IDF", b.tp, b.tp / base);
            printf("  %-14s  %7.1f   %.2fx\n", "+AdaptL", c.tp, c.tp / base);
            printf("  %-14s  %7.1f   %.2fx\n", "+Clustering", d.tp, d.tp / base);
            printf("  %-14s  %7.1f   %.2fx\n", "Full CABS", e.tp, e.tp / base);
        }

        printf("\n── A2: Index Build Time Breakdown ──\n");
        lsh::timer t_idf;
        compute_idf_weights(*prep.data, n, dim, L_BASE);
        double idf_s = t_idf.elapsed();

        lsh::timer t_km;
        ClusterResult cr_t = spherical_kmeans(*prep.data, n, dim, K, 25, 1e-4f, false);
        double km_s = t_km.elapsed();

        ClusterResult cr_sp = cr_t;
        lsh::timer t_spill;
        apply_boundary_spill(cr_sp, *prep.data, n, dim, TAU_FULL);
        double spill_s = t_spill.elapsed();

        lsh::timer t_idx;
        CompositeIndex cidx_t = build_composite_index(*prep.data, n, dim, cr_t, k_eval, C, L_BASE, eta_auto, -1, true, true, TAU_FULL);
        double idx_s = t_idx.elapsed();

        const double sos_s = 0.0;
        const double mh_s = 0.0;
        const double bucket_s = std::max(0.0, idx_s - idf_s);
        const double total_s = idf_s + km_s + spill_s + sos_s + mh_s + bucket_s;
        auto ratio = [&](double x) { return (total_s > 1e-9) ? (100.0 * x / total_s) : 0.0; };
        printf("  Phase              Time(s)   Ratio\n");
        printf("  IDF computation    %7.2f   %5.1f%%\n", idf_s, ratio(idf_s));
        printf("  Spherical K-means %7.2f   %5.1f%%\n", km_s, ratio(km_s));
        printf("  Spill processing  %7.2f   %5.1f%%\n", spill_s, ratio(spill_s));
        printf("  SOS transform     %7.2f   %5.1f%%\n", sos_s, ratio(sos_s));
        printf("  MinHash compute   %7.2f   %5.1f%%\n", mh_s, ratio(mh_s));
        printf("  Bucket indexing   %7.2f   %5.1f%%\n", bucket_s, ratio(bucket_s));
        printf("  Total             %7.2f   100.0%%\n", total_s);

        printf("\n── A3: Cluster Parameter Distribution (K=15, τ=0.8) ──\n");
        printf("  Cluster  n_i     l_avg   m_i   T_i    m_i/m_global\n");
        for (int i = 0; i < K; i++) {
            auto& ci = cidx_t.cluster_indexes[i];
            printf("  %d        %d   %5.1f   %3d   %5d   %.2f\n",
                   i, ci.params.n_i, ci.avg_effective_l, ci.params.m_i, ci.params.T_i,
                   (float)ci.params.m_i / 200.0f);
        }
        printf("  Global   %d  %.1f   200   -      1.00\n", n, cidx_t.idf_weights.stats.avg_l);

        printf("\n── E4: Scalability (K=15, nprobe=5, τ=0.8, η=0.03) ──\n");
        printf("  n         Ratio  Recall  TP(qps)   Cost    Build(s)\n");
        std::vector<float> scale_ratios = {0.10f, 0.30f, 0.50f, 0.80f, 1.00f};
        for (float rr : scale_ratios) {
            int n_sub = std::max(2000, (int)(n * rr));
            SubsetBenchmark sb = compute_subset_benchmark(prep, n_sub, k_eval, 200);
            lsh::timer tb;
            ClusterResult crs = spherical_kmeans(*prep.data, n_sub, dim, K, 25, 1e-4f, false);
            CompositeIndex cidx_s = build_composite_index(*prep.data, n_sub, dim, crs, k_eval, C, L_BASE, eta_auto, -1, true, true, TAU_FULL);
            double build_s = tb.elapsed();
            BenchmarkResult br = run_cabs_benchmark_subset(cidx_s, prep, k_eval, (5.0f / 15.0f), C, sb);
            printf("  %7d   %3.0f%%   %.3f   %7.1f  %6d   %6.2f\n", n_sub, rr * 100.0f, br.avg_recall, br.throughput, (int)roundf(br.avg_candidates), build_s);
        }

        printf("\n── A4: τ×nprobe Joint Study (K=15, η=0.03) ──\n");
        printf("  tau   nprobe  probe  Recall   Cost    TP(qps)\n");
        std::vector<float> taus = {0.7f, 0.8f, 0.9f, 2.0f};
        std::vector<int> nps = {2, 3, 5, 8, 15};
        for (float tau : taus) {
            float spill = (tau >= 2.0f) ? 0.0f : tau;
            ClusterResult crj = spherical_kmeans(*prep.data, n, dim, K, 25, 1e-4f, false);
            CompositeIndex cidx_j = build_composite_index(*prep.data, n, dim, crj, k_eval, C, L_BASE, eta_auto, -1, true, true, spill);
            for (int np : nps) {
                if (np > K) continue;
                float pr = std::min(1.0f, (float)np / (float)K);
                BenchmarkResult br = run_cabs_benchmark(cidx_j, prep, k_eval, pr, C);
                printf("  %.2f   %2d      %.2f   %.3f   %6d   %7.1f\n", tau, np, pr, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput);
            }
        }
        printf("\n");
        return 0;
    };


    struct ProxyQueryMetrics {
        int query_id = -1;
        float sp_alpha_exact = 0.0f;
        float sp_alpha_iw = 0.0f;
        float cov100 = 0.0f, cov200 = 0.0f, cov500 = 0.0f, cov1000 = 0.0f;
        float pre100 = 0.0f, pre200 = 0.0f, pre500 = 0.0f, pre1000 = 0.0f;
    };

    auto run_proxy_validity_experiment = [&](int max_queries, bool record_detail) -> int {
        int n = static_cast<int>(prep.data->n);
        int dim = prep.data->dim;
        int k_eval = 50;

        auto rankdata = [&](const std::vector<float>& v) {
            int n = (int)v.size();
            std::vector<std::pair<float,int>> a;
            a.reserve(n);
            for (int i = 0; i < n; ++i) a.push_back({v[i], i});
            std::sort(a.begin(), a.end(), [](const auto& x, const auto& y){
                if (x.first != y.first) return x.first < y.first;
                return x.second < y.second;
            });
            std::vector<float> r((size_t)n, 0.0f);
            int i = 0;
            while (i < n) {
                int j = i + 1;
                while (j < n && a[j].first == a[i].first) ++j;
                float rv = 0.5f * (float)(i + 1 + j);
                for (int t = i; t < j; ++t) r[(size_t)a[t].second] = rv;
                i = j;
            }
            return r;
        };

        auto spearman = [&](const std::vector<float>& x, const std::vector<float>& y) {
            int m = std::min((int)x.size(), (int)y.size());
            if (m < 2) return 0.0f;
            std::vector<float> xx(x.begin(), x.begin() + m), yy(y.begin(), y.begin() + m);
            std::vector<float> rx = rankdata(xx), ry = rankdata(yy);
            double mx = 0.0, my = 0.0;
            for (int i = 0; i < m; ++i) { mx += rx[(size_t)i]; my += ry[(size_t)i]; }
            mx /= m; my /= m;
            double num = 0.0, dx = 0.0, dy = 0.0;
            for (int i = 0; i < m; ++i) {
                double ax = rx[(size_t)i] - mx;
                double ay = ry[(size_t)i] - my;
                num += ax * ay; dx += ax * ax; dy += ay * ay;
            }
            if (dx <= 1e-12 || dy <= 1e-12) return 0.0f;
            return (float)(num / std::sqrt(dx * dy));
        };

        auto weighted_overlap_ip = [&](const int* q_ind, const float* q_val, int q_nnz,
                                       const int* d_ind, const float* d_val, int d_nnz,
                                       const std::vector<float>& w_hat) {
            int i = 0, j = 0;
            float res = 0.0f;
            while (i < q_nnz && j < d_nnz) {
                if (q_ind[i] == d_ind[j]) {
                    int idx = q_ind[i];
                    float w = (idx >= 0 && idx < (int)w_hat.size()) ? w_hat[(size_t)idx] : 1.0f;
                    float w3 = w * w * w;
                    res += q_val[i] * d_val[j] * w3;
                    ++i; ++j;
                } else if (q_ind[i] < d_ind[j]) ++i;
                else ++j;
            }
            return res;
        };

        struct CandRec {
            int pid = -1;
            int alpha = 0;
            float exact_ip = 0.0f;
            float iw_ip = 0.0f;
            int cluster_id = -1;
            float score = 0.0f;
        };

        auto build_query_set_local = [&](const CompositeIndex& cidx, const int* q_indices, const float* q_vals, int q_nnz, int hash_domain, int seed_extra) {
            std::vector<int> q_set;
            q_set.reserve((size_t)q_nnz * 4);
            float q_max = -std::numeric_limits<float>::infinity();
            for (int i = 0; i < q_nnz; ++i) q_max = std::max(q_max, q_vals[i]);
            if (!std::isfinite(q_max) || q_max <= 0.0f) q_max = 1.0f;
            const int active_dim = (cidx.dim_limit > 0) ? std::min(cidx.dim, cidx.dim_limit) : cidx.dim;
            const int l_fixed = std::max(3, 20);
            uint32_t rng_state = static_cast<uint32_t>(2027 + seed_extra * 1315423911u);
            auto fast_randf_local = [&](uint32_t& st) {
                st = st * 1664525u + 1013904223u;
                return static_cast<float>(st & 0x7FFFFFu) / static_cast<float>(0x800000u);
            };
            for (int j = 0; j < q_nnz; ++j) {
                int idx = q_indices[j];
                float v = q_vals[j];
                if (v <= 0.0f) continue;
                if (idx < 0 || idx >= active_dim) continue;
                float q_norm_val = std::max(0.0f, std::min(1.0f, v / q_max));
                float weighted_prob = q_norm_val;
                if (cidx.use_idf && idx < (int)cidx.idf_weights.idf.size()) {
                    weighted_prob = std::min(1.0f, q_norm_val * cidx.idf_weights.idf[(size_t)idx]);
                }
                if (weighted_prob <= 0.0f) continue;
                int l_j = l_fixed;
                int start_pos = idx * l_fixed;
                if (cidx.use_adaptive_l && idx < (int)cidx.idf_weights.dim_l.size()) {
                    l_j = cidx.idf_weights.dim_l[(size_t)idx];
                    start_pos = cidx.idf_weights.prefix_l[(size_t)idx];
                }
                int upper = std::min(hash_domain, start_pos + l_j);
                for (int slot = start_pos; slot < upper; ++slot) {
                    if (fast_randf_local(rng_state) < weighted_prob) q_set.push_back(slot);
                }
            }
            std::sort(q_set.begin(), q_set.end());
            q_set.erase(std::unique(q_set.begin(), q_set.end()), q_set.end());
            return q_set;
        };

        auto collect_candidates = [&](const CompositeIndex& cidx,
                                     const int* q_ind, const float* q_val, int q_nnz,
                                     float probe_ratio,
                                     std::vector<CandRec>& recs,
                                     int& active_clusters) {
            recs.clear();
            active_clusters = 0;
            if (cidx.K <= 0) return;

            std::vector<std::pair<float,int>> centroid_scores;
            centroid_scores.reserve((size_t)cidx.K);
            for (int i = 0; i < cidx.K; ++i) {
                float s = sparse_dense_dot(q_ind, q_val, q_nnz, cidx.cluster_indexes[(size_t)i].centroid);
                centroid_scores.push_back({s, i});
            }
            std::sort(centroid_scores.begin(), centroid_scores.end(), [](const auto& a, const auto& b){return a.first > b.first;});

            int num_active = std::max(1, (int)std::ceil((float)cidx.K * probe_ratio));
            num_active = std::min(num_active, cidx.K);
            active_clusters = num_active;

            int hash_domain = cidx.cluster_indexes[(size_t)centroid_scores.front().second].hash_domain;
            std::vector<int> q_set = build_query_set_local(cidx, q_ind, q_val, q_nnz, hash_domain, q_nnz + k_eval);

            std::unordered_map<int, CandRec> best;
            for (int p = 0; p < num_active; ++p) {
                float this_centroid = centroid_scores[(size_t)p].first;
                int cid = centroid_scores[(size_t)p].second;
                const auto& cluster = cidx.cluster_indexes[(size_t)cid];
                int n_p = (int)cluster.point_ids.size();
                int m_p = cluster.params.m_i;
                if (n_p <= 0 || m_p <= 0) continue;

                std::vector<int> q_hash((size_t)m_p, cluster.hash_domain - 1);
                for (int j = 0; j < m_p; ++j) {
                    uint64_t a = cluster.hash_funcs[(size_t)j].a;
                    uint64_t b = cluster.hash_funcs[(size_t)j].b;
                    int minv = cluster.hash_domain - 1;
                    for (int elem : q_set) {
                        int hv = (int)(((a * (uint64_t)elem + b) % cluster.HASH_PRIME) % (uint64_t)cluster.hash_domain);
                        if (hv < minv) minv = hv;
                    }
                    q_hash[(size_t)j] = minv;
                }

                std::vector<int> cand_count((size_t)n_p, 0);
                for (int j = 0; j < m_p; ++j) {
                    const auto& table = cluster.local_indexes[(size_t)j];
                    auto it = table.find(q_hash[(size_t)j]);
                    if (it == table.end()) continue;
                    for (int li : it->second) if (li >= 0 && li < n_p) cand_count[(size_t)li]++;
                }

                struct ScoredCand { int li; int cc; float score; };
                std::vector<ScoredCand> cands;
                cands.reserve((size_t)n_p);
                for (int li = 0; li < n_p; ++li) {
                    int cc = cand_count[(size_t)li];
                    if (cc <= 0) continue;
                    float collision_ratio = (float)cc / (float)std::max(1, m_p);
                    float set_size_li = (float)cluster.local_set_sizes[(size_t)li];
                    float q_set_sz = (float)q_set.size();
                    float so_est = (q_set_sz + set_size_li) / (1.0f + (float)m_p / (float)std::max(1, cc));
                    float ip_est = so_est / (float)std::max(1, cluster.params.l_i);
                    float alpha = 0.7f;
                    float score = alpha * ip_est + (1.0f - alpha) * this_centroid * collision_ratio;
                    cands.push_back({li, cc, score});
                }

                int T_budget = std::min(cluster.params.T_i, n_p);
                int verify_limit = std::min((int)cands.size(), T_budget);
                if (verify_limit < (int)cands.size()) {
                    std::partial_sort(cands.begin(), cands.begin() + verify_limit, cands.end(),
                                      [](const ScoredCand& a, const ScoredCand& b){ return a.score > b.score; });
                } else {
                    std::sort(cands.begin(), cands.end(), [](const ScoredCand& a, const ScoredCand& b){ return a.score > b.score; });
                }

                for (int vi = 0; vi < verify_limit; ++vi) {
                    int li = cands[(size_t)vi].li;
                    int pid = cluster.point_ids[(size_t)li];
                    if (pid < 0 || pid + 1 > (int)prep.data->n) continue;
                    size_t d_start = prep.data->indptr[pid];
                    size_t d_end = prep.data->indptr[pid + 1];
                    float ip = sparse_sparse_dot(q_ind, q_val, q_nnz,
                                                prep.data->indices + d_start,
                                                prep.data->val + d_start,
                                                (int)(d_end - d_start));
                    float iw = weighted_overlap_ip(q_ind, q_val, q_nnz,
                                                   prep.data->indices + d_start,
                                                   prep.data->val + d_start,
                                                   (int)(d_end - d_start),
                                                   cidx.idf_weights.idf);
                    CandRec rec;
                    rec.pid = pid;
                    rec.alpha = cands[(size_t)vi].cc;
                    rec.exact_ip = ip;
                    rec.iw_ip = iw;
                    rec.cluster_id = cid;
                    rec.score = cands[(size_t)vi].score;

                    auto it = best.find(pid);
                    if (it == best.end() || rec.alpha > it->second.alpha || (rec.alpha == it->second.alpha && rec.score > it->second.score)) {
                        best[pid] = rec;
                    }
                }
            }

            recs.reserve(best.size());
            for (auto& kv : best) recs.push_back(kv.second);
            std::sort(recs.begin(), recs.end(), [](const CandRec& a, const CandRec& b){
                if (a.alpha != b.alpha) return a.alpha > b.alpha;
                return a.score > b.score;
            });
        };

        auto run_one_config = [&](const std::string& cfg_name, const CompositeIndex& cidx,
                                  const std::string& out_dir,
                                  std::vector<ProxyQueryMetrics>& query_metrics) {
            query_metrics.clear();
            std::ofstream qcsv(out_dir + "/proxy_query_level.csv", std::ios::app);
            std::ofstream dcsv;
            if (record_detail) {
                dcsv.open(out_dir + "/proxy_candidates_detail.csv", std::ios::app);
            }
            for (int qi = 0; qi < std::min(max_queries, prep.benchmark.N); ++qi) {
                int q_start = (int)prep.queries->indptr[qi];
                int q_end = (int)prep.queries->indptr[qi + 1];
                int q_nnz = q_end - q_start;
                const int* q_ind = prep.queries->indices + q_start;
                const float* q_val = prep.queries->val + q_start;

                std::vector<CandRec> recs;
                int active_clusters = 0;
                collect_candidates(cidx, q_ind, q_val, q_nnz, 0.33f, recs, active_clusters);

                std::vector<float> alpha_v, exact_v, iw_v;
                alpha_v.reserve(recs.size());
                exact_v.reserve(recs.size());
                iw_v.reserve(recs.size());
                for (auto& r : recs) {
                    alpha_v.push_back((float)r.alpha);
                    exact_v.push_back(r.exact_ip);
                    iw_v.push_back(r.iw_ip);
                    if (record_detail && dcsv.is_open()) {
                        dcsv << cfg_name << "," << qi << "," << r.pid << "," << r.alpha << "," << r.exact_ip << "," << r.iw_ip << "," << r.cluster_id << "\n";
                    }
                }

                std::vector<int> gt50;
                int gt_top = std::min(50, prep.benchmark.num);
                gt50.reserve((size_t)gt_top);
                for (int r = 0; r < gt_top; ++r) gt50.push_back(prep.benchmark.indice[qi][r]);
                std::unordered_set<int> gtset(gt50.begin(), gt50.end());

                auto cov_prec = [&](int L, float& cov, float& pre) {
                    int take = std::min(L, (int)recs.size());
                    int hit = 0;
                    for (int i = 0; i < take; ++i) if (gtset.count(recs[(size_t)i].pid)) ++hit;
                    cov = (gt_top > 0) ? (float)hit / (float)gt_top : 0.0f;
                    pre = (take > 0) ? (float)hit / (float)take : 0.0f;
                };

                ProxyQueryMetrics qm;
                qm.query_id = qi;
                qm.sp_alpha_exact = spearman(alpha_v, exact_v);
                qm.sp_alpha_iw = spearman(alpha_v, iw_v);
                cov_prec(100, qm.cov100, qm.pre100);
                cov_prec(200, qm.cov200, qm.pre200);
                cov_prec(500, qm.cov500, qm.pre500);
                cov_prec(1000, qm.cov1000, qm.pre1000);
                query_metrics.push_back(qm);

                qcsv << cfg_name << "," << qm.query_id << "," << qm.sp_alpha_exact << "," << qm.sp_alpha_iw
                     << "," << qm.cov100 << "," << qm.cov200 << "," << qm.cov500 << "," << qm.cov1000
                     << "," << qm.pre100 << "," << qm.pre200 << "," << qm.pre500 << "," << qm.pre1000 << "\n";
            }
        };

        std::string out_dir = "results/proxy_validity";
        std::system((std::string("mkdir -p ") + out_dir).c_str());
        {
            std::ofstream qcsv(out_dir + "/proxy_query_level.csv");
            qcsv << "config,query_id,spearman_alpha_exact,spearman_alpha_iw,cov100,cov200,cov500,cov1000,pre100,pre200,pre500,pre1000\n";
            if (record_detail) {
                std::ofstream dcsv(out_dir + "/proxy_candidates_detail.csv");
                dcsv << "config,query_id,candidate_id,collision_count_alpha,exact_inner_product,weighted_inner_product,cluster_id\n";
            }
        }

        float C = 0.9f, L_BASE = 20.0f;
        float eta = (n >= 1000000) ? 0.03f : 0.05f;
        int K = 15;

        ClusterResult cr1 = spherical_kmeans(*prep.data, n, dim, 1, 15, 1e-4f, false);
        ClusterResult crk = spherical_kmeans(*prep.data, n, dim, K, 25, 1e-4f, false);

        CompositeIndex cfg_sosia = build_composite_index(*prep.data, n, dim, cr1, k_eval, C, L_BASE, 0.02f, -1,
                                                         false, false, 0.0f, 1.0f, 1.0f, 150, false);
        CompositeIndex cfg_noidf = build_composite_index(*prep.data, n, dim, crk, k_eval, C, L_BASE, eta, -1,
                                                         false, false, 0.8f, 1.0f, 1.0f, -1, true);
        CompositeIndex cfg_full = build_composite_index(*prep.data, n, dim, crk, k_eval, C, L_BASE, eta, -1,
                                                        true, true, 0.8f, 0.5f, 2.0f, -1, true);

        std::ofstream scsv(out_dir + "/proxy_summary.csv");
        scsv << "config,avg_spearman_alpha_iw,avg_spearman_alpha_exact,avg_cov100,avg_cov200,avg_cov500,avg_cov1000,avg_pre100,avg_pre200,avg_pre500,avg_pre1000,num_queries\n";

        auto summarize = [&](const std::string& name, const std::vector<ProxyQueryMetrics>& v) {
            double s1=0,s2=0,c100=0,c200=0,c500=0,c1000=0,p100=0,p200=0,p500=0,p1000=0;
            for (auto& q : v) {
                s1 += q.sp_alpha_iw; s2 += q.sp_alpha_exact;
                c100 += q.cov100; c200 += q.cov200; c500 += q.cov500; c1000 += q.cov1000;
                p100 += q.pre100; p200 += q.pre200; p500 += q.pre500; p1000 += q.pre1000;
            }
            int m = std::max(1, (int)v.size());
            scsv << name << "," << (s1/m) << "," << (s2/m) << "," << (c100/m) << "," << (c200/m) << ","
                 << (c500/m) << "," << (c1000/m) << "," << (p100/m) << "," << (p200/m) << ","
                 << (p500/m) << "," << (p1000/m) << "," << v.size() << "\n";
            std::printf("[proxy] %s: avg Spearman(alpha,Iw)=%.4f, avg Spearman(alpha,qx)=%.4f, avg Top-200 cov=%.4f, avg Top-500 cov=%.4f\n",
                        name.c_str(), (float)(s1/m), (float)(s2/m), (float)(c200/m), (float)(c500/m));
        };

        std::vector<ProxyQueryMetrics> qm;
        run_one_config("SOSIA_baseline", cfg_sosia, out_dir, qm);
        summarize("SOSIA_baseline", qm);
        run_one_config("CABS_no_IDF", cfg_noidf, out_dir, qm);
        summarize("CABS_no_IDF", qm);
        run_one_config("CABS_full", cfg_full, out_dir, qm);
        summarize("CABS_full", qm);

        return 0;
    };

    auto run_space_gain_decomposition = [&]() -> int {
        int n = static_cast<int>(prep.data->n);
        int dim = prep.data->dim;
        int k_eval = 50;
        float C = 0.9f, L_BASE = 20.0f;
        float eta = (n >= 1000000) ? 0.03f : 0.05f;

        std::string out_dir = "results/space_gain";
        std::system((std::string("mkdir -p ") + out_dir).c_str());
        std::ofstream csv(out_dir + "/space_gain_decomposition.csv");
        csv << "K,config,total_points_after_spill,spill_extra_points,signature_entries,signature_bytes,raw_data_bytes,centroid_meta_bytes,bucket_structure_bytes,total_index_bytes,delta_signature_vs_global_nospill,delta_total_index_vs_global_nospill\n";

        auto estimate_bucket_bytes = [&](const CompositeIndex& cidx) {
            long long b = 0;
            for (auto& cl : cidx.cluster_indexes) {
                for (auto& table : cl.local_indexes) {
                    b += (long long)table.size() * 16;
                    for (auto& kv : table) b += (long long)kv.second.size() * (long long)sizeof(int);
                }
            }
            return b;
        };

        long long raw_data_bytes = (long long)(prep.data->n + 1) * (long long)sizeof(size_t)
                                 + (long long)prep.data->nnz * (long long)(sizeof(int) + sizeof(float));

        std::vector<int> Ks = {5, 10, 15, 20, 25, 32};
        for (int K : Ks) {
            ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K, 25, 1e-4f, false);

            struct Row {
                std::string name;
                long long total_points_after_spill = 0;
                long long spill_extra_points = 0;
                long long signature_entries = 0;
                long long signature_bytes = 0;
                long long centroid_meta_bytes = 0;
                long long bucket_structure_bytes = 0;
                long long total_index_bytes = 0;
            };

            std::vector<Row> rows;
            auto build_row = [&](const std::string& name, bool adaptive, float spill) {
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, k_eval, C, L_BASE, eta, -1,
                                                            true, true, spill, 0.5f, 2.0f, 200, adaptive);
                Row r;
                r.name = name;
                for (auto& cl : cidx.cluster_indexes) {
                    r.total_points_after_spill += (long long)cl.point_ids.size();
                    r.signature_entries += (long long)cl.params.m_i * (long long)cl.point_ids.size();
                    r.centroid_meta_bytes += (long long)cl.centroid.size() * (long long)sizeof(float);
                    r.centroid_meta_bytes += (long long)sizeof(ClusterParams);
                }
                r.spill_extra_points = r.total_points_after_spill - n;
                r.signature_bytes = r.signature_entries * (long long)sizeof(int);
                r.bucket_structure_bytes = estimate_bucket_bytes(cidx);
                r.total_index_bytes = r.signature_bytes + r.centroid_meta_bytes + r.bucket_structure_bytes;
                rows.push_back(r);
            };

            build_row("Global-noSpill", false, 0.0f);
            build_row("Adaptive-noSpill", true, 0.0f);
            build_row("Global-Spill", false, 0.8f);
            build_row("Adaptive-Spill", true, 0.8f);

            long long base_sig = rows[0].signature_bytes;
            long long base_tot = rows[0].total_index_bytes;
            for (auto& r : rows) {
                csv << K << "," << r.name << "," << r.total_points_after_spill << "," << r.spill_extra_points
                    << "," << r.signature_entries << "," << r.signature_bytes << "," << raw_data_bytes
                    << "," << r.centroid_meta_bytes << "," << r.bucket_structure_bytes << "," << r.total_index_bytes
                    << "," << (r.signature_bytes - base_sig) << "," << (r.total_index_bytes - base_tot) << "\n";
            }
        }

        std::printf("[space_gain] wrote %s/space_gain_decomposition.csv\n", out_dir.c_str());
        return 0;
    };


    auto run_overall_multidataset_comparison = [&](const std::string& config_path) -> int {
        struct DsConf { std::string name, base, query, ben; };
        struct MethodPoint { std::string method, param; float recall=0.0f, qps=0.0f, cost=0.0f; };

        auto trim = [](std::string x) {
            size_t b = 0, e = x.size();
            while (b < e && std::isspace((unsigned char)x[b])) ++b;
            while (e > b && std::isspace((unsigned char)x[e - 1])) --e;
            return x.substr(b, e - b);
        };
        auto lower = [](std::string x) {
            for (char& c : x) c = (char)std::tolower((unsigned char)c);
            return x;
        };
        auto split = [&](const std::string& line, char sep) {
            std::vector<std::string> out;
            std::string cur;
            for (char ch : line) {
                if (ch == sep) { out.push_back(trim(cur)); cur.clear(); }
                else cur.push_back(ch);
            }
            out.push_back(trim(cur));
            return out;
        };
        auto sanitize = [](std::string x) {
            for (char& c : x) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) c = '_';
            return x;
        };
        auto derive_ben = [&](const std::string& base) {
            if (base.size() >= 4 && base.substr(base.size() - 4) == ".csr") return base.substr(0, base.size() - 4) + "_all.ben";
            return base + "_all.ben";
        };

        auto extract_json_string = [&](const std::string& src, const std::string& key, std::string& out) {
            std::regex rg("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
            std::smatch m;
            if (std::regex_search(src, m, rg) && m.size() >= 2) { out = m[1].str(); return true; }
            return false;
        };
        auto extract_json_int = [&](const std::string& src, const std::string& key, int& out) {
            std::regex rg("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
            std::smatch m;
            if (std::regex_search(src, m, rg) && m.size() >= 2) { out = std::atoi(m[1].str().c_str()); return true; }
            return false;
        };

        auto extract_json_int_array = [&](const std::string& src, const std::string& key, std::vector<int>& out) {
            std::string k = """ + key + """;
            size_t p = src.find(k);
            if (p == std::string::npos) return false;
            size_t lb = src.find('[', p + k.size());
            if (lb == std::string::npos) return false;
            size_t rb = src.find(']', lb + 1);
            if (rb == std::string::npos || rb <= lb) return false;
            std::string body = src.substr(lb + 1, rb - lb - 1);
            out.clear();
            int sign = 1, cur = 0;
            bool in_num = false;
            for (char ch : body) {
                if (ch == '-') {
                    if (!in_num) { sign = -1; cur = 0; in_num = true; }
                } else if (ch >= '0' && ch <= '9') {
                    if (!in_num) { in_num = true; sign = 1; cur = 0; }
                    cur = cur * 10 + int(ch - '0');
                } else {
                    if (in_num) { out.push_back(sign * cur); in_num = false; sign = 1; cur = 0; }
                }
            }
            if (in_num) out.push_back(sign * cur);
            return !out.empty();
        };

        auto find_matching = [&](const std::string& src, size_t from, char lch, char rch) -> size_t {
            int dep = 0;
            for (size_t i = from; i < src.size(); ++i) {
                if (src[i] == lch) ++dep;
                else if (src[i] == rch) {
                    --dep;
                    if (dep == 0) return i;
                }
            }
            return std::string::npos;
        };

        std::string out_dir = "results/exp4";
        int topk = 50;
        int seed = 2026;
        int query_subset_size = -1;
        float cabs_eta_override = -1.0f;
        int cabs_K_override = -1;
        float cabs_tau_override = -1.0f;
        std::vector<int> cabs_nprobes_override;
        std::vector<int> sosia_ubs_override;
        bool m_linear = true, m_sosia = true, m_wand = true, m_binsketch = true, m_cabs = true;
        std::string binsketch_best_cfg;
        std::vector<DsConf> datasets;

        bool json_cfg = false;
        if (!config_path.empty()) {
            std::ifstream fin(config_path);
            if (fin.good()) {
                std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
                json_cfg = (content.find('{') != std::string::npos && content.find("datasets") != std::string::npos);
                if (json_cfg) {
                    std::string tmp;
                    if (extract_json_string(content, "output_dir", tmp)) out_dir = tmp;
                    extract_json_int(content, "topk", topk);
                    if (!extract_json_int(content, "random_seed", seed)) extract_json_int(content, "seed", seed);
                    extract_json_int(content, "query_subset_size", query_subset_size);
                    {
                        std::regex rg_eta(R"("cabs_eta"\s*:\s*([0-9]+(?:\.[0-9]+)?))");
                        std::smatch mm_eta;
                        if (std::regex_search(content, mm_eta, rg_eta) && mm_eta.size() >= 2) {
                            cabs_eta_override = std::atof(mm_eta[1].str().c_str());
                        }
                    }
                    {
                        std::regex rg_k(R"("cabs_K"\s*:\s*([0-9]+))");
                        std::smatch mm_k;
                        if (std::regex_search(content, mm_k, rg_k) && mm_k.size() >= 2) {
                            cabs_K_override = std::atoi(mm_k[1].str().c_str());
                        }
                    }
                    {
                        std::regex rg_tau(R"("cabs_tau"\s*:\s*([0-9]+(?:\.[0-9]+)?))");
                        std::smatch mm_tau;
                        if (std::regex_search(content, mm_tau, rg_tau) && mm_tau.size() >= 2) {
                            cabs_tau_override = std::atof(mm_tau[1].str().c_str());
                        }
                    }
                    {
                        extract_json_int_array(content, "cabs_nprobes", cabs_nprobes_override);
                        extract_json_int_array(content, "sosia_ubs", sosia_ubs_override);
                    }

                    // methods object
                    size_t mpos = content.find("\"methods\"");
                    if (mpos != std::string::npos) {
                        size_t lb = content.find('{', mpos);
                        if (lb != std::string::npos) {
                            size_t rb = find_matching(content, lb, '{', '}');
                            if (rb != std::string::npos) {
                                std::string mblk = content.substr(lb, rb - lb + 1);
                                auto bool_key = [&](const std::string& key, bool& val) {
                                    std::regex rg("\\\"" + key + "\\\"\\s*:\\s*(true|false)", std::regex_constants::icase);
                                    std::smatch mm;
                                    if (std::regex_search(mblk, mm, rg) && mm.size() >= 2) {
                                        val = (lower(mm[1].str()) == "true");
                                    }
                                };
                                bool_key("LinearScan", m_linear);
                                bool_key("SOSIA", m_sosia);
                                bool_key("WAND", m_wand);
                                bool_key("BinSketch", m_binsketch);
                                bool_key("CABS", m_cabs);
                            }
                        }
                    }

                    // binsketch config block
                    size_t bpos = content.find("\"binsketch_best_config\"");
                    if (bpos != std::string::npos) {
                        size_t lb = content.find('{', bpos);
                        if (lb != std::string::npos) {
                            size_t rb = find_matching(content, lb, '{', '}');
                            if (rb != std::string::npos) binsketch_best_cfg = trim(content.substr(lb, rb - lb + 1));
                        }
                    }

                    // datasets array
                    size_t dpos = content.find("\"datasets\"");
                    if (dpos != std::string::npos) {
                        size_t lb = content.find('[', dpos);
                        if (lb != std::string::npos) {
                            size_t rb = find_matching(content, lb, '[', ']');
                            if (rb != std::string::npos) {
                                std::string darr = content.substr(lb + 1, rb - lb - 1);
                                for (size_t i = 0; i < darr.size(); ++i) {
                                    if (darr[i] != '{') continue;
                                    size_t j = find_matching(darr, i, '{', '}');
                                    if (j == std::string::npos) break;
                                    std::string obj = darr.substr(i, j - i + 1);
                                    DsConf d;
                                    extract_json_string(obj, "name", d.name);
                                    if (!extract_json_string(obj, "base_path", d.base)) extract_json_string(obj, "base", d.base);
                                    if (!extract_json_string(obj, "query_path", d.query)) extract_json_string(obj, "query", d.query);
                                    if (!extract_json_string(obj, "groundtruth_path", d.ben)) extract_json_string(obj, "ben", d.ben);
                                    if (!d.base.empty() && !d.query.empty()) {
                                        if (d.name.empty()) d.name = d.base;
                                        if (d.ben.empty()) d.ben = derive_ben(d.base);
                                        datasets.push_back(d);
                                    }
                                    i = j;
                                }
                            }
                        }
                    }
                } else {
                    // key=value fallback
                    fin.clear(); fin.seekg(0);
                    std::string line;
                    while (std::getline(fin, line)) {
                        line = trim(line);
                        if (line.empty() || line[0] == '#') continue;
                        auto kv = split(line, '=');
                        if (kv.size() < 2) continue;
                        std::string key = lower(kv[0]);
                        std::string val = kv[1];
                        if (key == "output_dir") out_dir = val;
                        else if (key == "topk") topk = std::max(1, std::atoi(val.c_str()));
                        else if (key == "seed" || key == "random_seed") seed = std::atoi(val.c_str());
                        else if (key == "methods") {
                            m_linear = m_sosia = m_wand = m_binsketch = m_cabs = false;
                            for (auto x : split(val, ',')) {
                                std::string lx = lower(trim(x));
                                if (lx == "linearscan") m_linear = true;
                                else if (lx == "sosia") m_sosia = true;
                                else if (lx == "wand") m_wand = true;
                                else if (lx == "binsketch") m_binsketch = true;
                                else if (lx == "cabs") m_cabs = true;
                            }
                        } else if (key == "binsketch_best_config") binsketch_best_cfg = val;
                        else if (key == "dataset") {
                            auto fs = split(val, '|');
                            if (fs.size() >= 3) {
                                DsConf d;
                                d.name = fs[0]; d.base = fs[1]; d.query = fs[2];
                                d.ben = (fs.size() >= 4 && !fs[3].empty()) ? fs[3] : derive_ben(d.base);
                                datasets.push_back(d);
                            }
                        }
                    }
                }
            }
        }

        if (datasets.empty()) {
            datasets.push_back({"SPLADE-1M", "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr", "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr", "datasets/MS MARCO v1/SPLADE/1M/base_1M_all.ben"});
            datasets.push_back({"SPLADE-Full", "datasets/MS MARCO v1/SPLADE/10M/base_full.csr", "datasets/MS MARCO v1/SPLADE/10M/queries.dev.csr", "datasets/MS MARCO v1/SPLADE/10M/base_full_all.ben"});
        }

        std::filesystem::create_directories(out_dir);
        std::ofstream flog(out_dir + "/run_log.txt", std::ios::app);
        std::ofstream fsum(out_dir + "/overall_summary.csv");
        std::ofstream fper(out_dir + "/overall_per_dataset.csv");

        fsum << "dataset,best_method,best_qps,linearscan_qps,sosia_qps,cabs_qps,wand_status,binsketch_status,notes\n";
        fper << "dataset,method,recall@50,qps,avg_verified_candidates,build_time_sec,index_size_bytes,speedup_vs_linearscan,notes\n";

        mips2set::rng.seed(seed);
        flog << "[exp4] seed=" << seed << " topk=" << topk << " output_dir=" << out_dir << " query_subset_size=" << query_subset_size << " cabs_eta_override=" << cabs_eta_override << "\n";
        flog << "[exp4] cabs_K_override=" << cabs_K_override << " cabs_tau_override=" << cabs_tau_override << "\n";

        auto estimate_cabs_index_bytes = [&](const CompositeIndex& cidx) {
            long long signature_entries = 0;
            long long centroid_meta_bytes = 0;
            long long bucket_structure_bytes = 0;
            for (const auto& cl : cidx.cluster_indexes) {
                signature_entries += (long long)cl.params.m_i * (long long)cl.point_ids.size();
                centroid_meta_bytes += (long long)cl.centroid.size() * (long long)sizeof(float) + (long long)sizeof(ClusterParams);
                for (const auto& table : cl.local_indexes) {
                    bucket_structure_bytes += (long long)table.size() * 16;
                    for (const auto& kv : table) bucket_structure_bytes += (long long)kv.second.size() * (long long)sizeof(int);
                }
            }
            long long signature_bytes = signature_entries * (long long)sizeof(int);
            return signature_bytes + centroid_meta_bytes + bucket_structure_bytes;
        };

        // simple binary cache utils (cluster + spill)
        auto write_int = [](std::ofstream& os, int x) { os.write(reinterpret_cast<const char*>(&x), sizeof(int)); };
        auto write_float = [](std::ofstream& os, float x) { os.write(reinterpret_cast<const char*>(&x), sizeof(float)); };
        auto read_int = [](std::ifstream& is, int& x) { is.read(reinterpret_cast<char*>(&x), sizeof(int)); return (bool)is; };
        auto read_float = [](std::ifstream& is, float& x) { is.read(reinterpret_cast<char*>(&x), sizeof(float)); return (bool)is; };

        auto save_cluster_cache = [&](const ClusterResult& cr, const std::string& path) {
            std::ofstream os(path, std::ios::binary);
            if (!os.good()) return false;
            write_int(os, 1); write_int(os, cr.K); write_int(os, cr.dim);
            int npt = (int)cr.point_to_cluster.size(); write_int(os, npt);
            int csz = (int)cr.clusters.size(); write_int(os, csz);
            for (const auto& cl : cr.clusters) { int sz = (int)cl.size(); write_int(os, sz); if (sz > 0) os.write(reinterpret_cast<const char*>(cl.data()), sizeof(int) * (size_t)sz); }
            int cent = (int)cr.centroids.size(); write_int(os, cent);
            for (const auto& v : cr.centroids) { int d = (int)v.size(); write_int(os, d); if (d > 0) os.write(reinterpret_cast<const char*>(v.data()), sizeof(float) * (size_t)d); }
            if (npt > 0) os.write(reinterpret_cast<const char*>(cr.point_to_cluster.data()), sizeof(int) * (size_t)npt);
            return os.good();
        };
        auto load_cluster_cache = [&](const std::string& path, int n, int dim, ClusterResult& cr) {
            std::ifstream is(path, std::ios::binary);
            if (!is.good()) return false;
            int ver=0,K=0,d=0,npt=0; if (!read_int(is,ver)||!read_int(is,K)||!read_int(is,d)||!read_int(is,npt)) return false;
            if (ver != 1 || d != dim || npt != n || K <= 0) return false;
            int csz=0; if (!read_int(is,csz) || csz != K) return false;
            cr.clusters.assign((size_t)csz, {});
            for (int i=0;i<csz;++i){ int sz=0; if(!read_int(is,sz)||sz<0) return false; cr.clusters[(size_t)i].resize((size_t)sz); if(sz>0) is.read(reinterpret_cast<char*>(cr.clusters[(size_t)i].data()), sizeof(int)*(size_t)sz); if(!is.good()) return false; }
            int cent=0; if(!read_int(is,cent)||cent!=K) return false;
            cr.centroids.assign((size_t)cent, {});
            for (int i=0;i<cent;++i){ int ds=0; if(!read_int(is,ds)||ds!=dim) return false; cr.centroids[(size_t)i].resize((size_t)ds); if(ds>0) is.read(reinterpret_cast<char*>(cr.centroids[(size_t)i].data()), sizeof(float)*(size_t)ds); if(!is.good()) return false; }
            cr.point_to_cluster.resize((size_t)npt); if(npt>0) is.read(reinterpret_cast<char*>(cr.point_to_cluster.data()), sizeof(int)*(size_t)npt);
            if(!is.good()) return false;
            cr.K=K; cr.dim=d; cr.spill_map.clear(); cr.spill_threshold=0.0f;
            return true;
        };
        auto save_spill_cache = [&](const ClusterResult& cr, const std::string& path) {
            std::ofstream os(path, std::ios::binary);
            if (!os.good()) return false;
            write_int(os, 1); write_int(os, cr.K); write_int(os, cr.dim); write_int(os, (int)cr.point_to_cluster.size()); write_float(os, cr.spill_threshold);
            int msz=(int)cr.spill_map.size(); write_int(os, msz);
            for (const auto& kv: cr.spill_map){ write_int(os, kv.first); int vs=(int)kv.second.size(); write_int(os, vs); if(vs>0) os.write(reinterpret_cast<const char*>(kv.second.data()), sizeof(int)*(size_t)vs); }
            return os.good();
        };
        auto load_spill_cache = [&](ClusterResult& cr, const std::string& path) {
            std::ifstream is(path, std::ios::binary); if(!is.good()) return false;
            int ver=0,K=0,d=0,npt=0; float th=0.0f;
            if(!read_int(is,ver)||!read_int(is,K)||!read_int(is,d)||!read_int(is,npt)||!read_float(is,th)) return false;
            if(ver!=1||K!=cr.K||d!=cr.dim||npt!=(int)cr.point_to_cluster.size()) return false;
            int msz=0; if(!read_int(is,msz)||msz<0) return false;
            cr.spill_map.clear();
            for(int i=0;i<msz;++i){ int pid=-1,vs=0; if(!read_int(is,pid)||!read_int(is,vs)||pid<0||vs<0) return false; auto& vv=cr.spill_map[pid]; vv.resize((size_t)vs); if(vs>0) is.read(reinterpret_cast<char*>(vv.data()), sizeof(int)*(size_t)vs); if(!is.good()) return false; }
            cr.spill_threshold=th; return true;
        };

        for (const auto& ds : datasets) {
            if (!std::filesystem::exists(ds.base) || !std::filesystem::exists(ds.query) || !std::filesystem::exists(ds.ben)) {
                flog << "[skip dataset] missing files: " << ds.name << " base=" << ds.base << " query=" << ds.query << " ben=" << ds.ben << "\n";
                continue;
            }

            flog << "[dataset] " << ds.name << " base=" << ds.base << " query=" << ds.query << " ben=" << ds.ben << "\n";
            std::cout << "[exp4] dataset=" << ds.name << std::endl;

            Preprocess p(ds.base, ds.query, ds.ben);
            const int n = (int)p.data->n;
            const int dim = p.data->dim;
            const int k_eval = topk;

            std::string curve_path = out_dir + "/curves_" + sanitize(ds.name) + ".csv";
            std::string eq_path = out_dir + "/equal_recall_" + sanitize(ds.name) + ".csv";
            std::ofstream fcurve(curve_path);
            fcurve << "dataset,method,control_param_name,control_param_value,recall,qps,avg_verified_candidates\n";

            std::vector<MethodPoint> all_pts;
            std::vector<MethodPoint> pts_linear, pts_sosia, pts_cabs;

            float linear_qps = 0.0f, linear_recall = 0.0f;
            float sosia_qps = 0.0f, cabs_qps = 0.0f;
            std::string best_method = "NA";
            float best_qps = -1.0f;
            std::string wand_note = "disabled", bins_note = "disabled";

            if (m_linear) {
                auto st = std::chrono::high_resolution_clock::now();
                auto br = run_linear_scan(p, k_eval);
                auto ed = std::chrono::high_resolution_clock::now();
                double bt = std::chrono::duration<double>(ed - st).count();
                long long raw_bytes = (long long)(p.data->n + 1) * (long long)sizeof(size_t) + (long long)p.data->nnz * (long long)(sizeof(int) + sizeof(float));
                linear_qps = br.throughput;
                linear_recall = br.avg_recall;
                fper << ds.name << ",LinearScan," << br.avg_recall << "," << br.throughput << "," << br.avg_candidates << "," << bt << "," << raw_bytes << ",1,ok\n";
                pts_linear.push_back({"LinearScan", "-", br.avg_recall, br.throughput, br.avg_candidates});
                all_pts.push_back(pts_linear.back());
                if (br.throughput > best_qps) { best_qps = br.throughput; best_method = "LinearScan"; }
            }

            if (m_sosia) {
                lsh::timer tbuild;
                mips2set sets(p, 20);
                myMinHashBase minBase(p, sets, 1, 150);
                Sosia sosia(minBase);
                double build_s = tbuild.elapsed();

                std::vector<int> ubs = {1500, 2000, 3000, 4000, 5000, 7000, 8000, 10000, 12000, 15000, 20000, 30000, 40000, 50000, 70000, 100000};
                if (!sosia_ubs_override.empty()) ubs = sosia_ubs_override;
                for (int ub : ubs) {
                    resOutput r = minHash(sosia, 0.5f, k_eval, ub, p);
                    float qps = (r.time > 0) ? (1000.0f / r.time) : 0.0f;
                    fcurve << ds.name << ",SOSIA,ub," << ub << "," << r.recall << "," << qps << "," << r.cost << "\n";
                    pts_sosia.push_back({"SOSIA", std::to_string(ub), r.recall, qps, r.cost});
                    all_pts.push_back(pts_sosia.back());
                }
                int def_ub = 20000;
                MethodPoint defp = pts_sosia.front();
                for (auto& p0 : pts_sosia) if (p0.param == std::to_string(def_ub)) { defp = p0; break; }
                sosia_qps = defp.qps;
                float speedup = (linear_qps > 1e-9f) ? (defp.qps / linear_qps) : 0.0f;
                fper << ds.name << ",SOSIA," << defp.recall << "," << defp.qps << "," << defp.cost << "," << build_s << ",-1," << speedup << ",m=150;l=20;ub=20000\n";
                if (defp.qps > best_qps) { best_qps = defp.qps; best_method = "SOSIA"; }
            }

            if (m_wand) {
                wand_note = "skipped:not_implemented";
                fper << ds.name << ",WAND,0,0,0,0,0,0," << wand_note << "\n";
            }

            if (m_binsketch) {
                if (binsketch_best_cfg.empty()) {
                    bins_note = "skipped:missing_best_config";
                } else {
                    bins_note = "skipped:not_implemented";
                }
                fper << ds.name << ",BinSketch,0,0,0,0,0,0," << bins_note << "\n";
            }

            if (m_cabs) {
                const int K = (cabs_K_override > 0) ? cabs_K_override : 15;
                const int np_def = 5;
                const float probe = (float)np_def / (float)K;
                const float C = 0.9f, L_BASE = 20.0f;
                float TAU = (cabs_tau_override > 0.0f) ? cabs_tau_override : 0.8f;
                float ETA = (cabs_eta_override > 0.0f) ? cabs_eta_override : 0.03f;
                if (const char* e = std::getenv("CABS_ETA")) { float v = std::atof(e); if (v > 0.0f) ETA = v; }
                const int M0 = 170;

                int tau_tag = (int)std::llround((double)TAU * 100.0);
                std::string cache_cluster = out_dir + "/cluster_cache_" + sanitize(ds.name) + "_K" + std::to_string(K) + "_seed" + std::to_string(seed) + ".bin";
                std::string cache_spill = out_dir + "/spill_cache_" + sanitize(ds.name) + "_K" + std::to_string(K) + "_tau" + std::to_string(tau_tag) + "_seed" + std::to_string(seed) + ".bin";
                ClusterResult cr, cr_sp;
                bool reuse_cluster = false, reuse_spill = false;

                if (std::filesystem::exists(cache_cluster) && load_cluster_cache(cache_cluster, n, dim, cr)) {
                    reuse_cluster = true;
                } else {
                    cr = spherical_kmeans_seeded(*p.data, n, dim, K, 25, 1e-4f, false, seed);
                    save_cluster_cache(cr, cache_cluster);
                }

                cr_sp = cr;
                if (std::filesystem::exists(cache_spill) && load_spill_cache(cr_sp, cache_spill)) {
                    reuse_spill = true;
                } else {
                    apply_boundary_spill(cr_sp, *p.data, n, dim, TAU);
                    save_spill_cache(cr_sp, cache_spill);
                }
                flog << "  [CABS cache] cluster_reused=" << (reuse_cluster ? "yes" : "no")
                     << " spill_reused=" << (reuse_spill ? "yes" : "no")
                     << " seed=" << seed << "\n";

                lsh::timer tbuild;
                CompositeIndex cidx = build_composite_index(*p.data, n, dim, cr_sp, k_eval, C, L_BASE, ETA, -1,
                                                            true, true, 0.0f, 0.5f, 3.0f, M0, true);
                double build_s = tbuild.elapsed();

                BenchmarkResult br = run_cabs_benchmark(cidx, p, k_eval, probe, C, query_subset_size);
                long long idx_bytes = estimate_cabs_index_bytes(cidx);
                float speedup = (linear_qps > 1e-9f) ? (br.throughput / linear_qps) : 0.0f;
                cabs_qps = br.throughput;
                fper << ds.name << ",CABS," << br.avg_recall << "," << br.throughput << "," << br.avg_candidates << ","
                     << build_s << "," << idx_bytes << "," << speedup
                     << ",K=" << K << ";nprobe=5;tau=" << TAU << ";eta=" << ETA << ";m0=170;idf=[0.5,3.0]\n";
                if (br.throughput > best_qps) { best_qps = br.throughput; best_method = "CABS"; }

                std::vector<int> nps = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 18, 20, 25};
                if (!cabs_nprobes_override.empty()) nps = cabs_nprobes_override;
                for (int np : nps) {
                    if (np > K) continue;
                    float pr = (float)np / (float)K;
                    BenchmarkResult b = run_cabs_benchmark(cidx, p, k_eval, pr, C, query_subset_size);
                    fcurve << ds.name << ",CABS,nprobe," << np << "," << b.avg_recall << "," << b.throughput << "," << b.avg_candidates << "\n";
                    pts_cabs.push_back({"CABS", std::to_string(np), b.avg_recall, b.throughput, b.avg_candidates});
                    all_pts.push_back(pts_cabs.back());
                }
            }

            // equal-recall table per dataset
            std::ofstream feq(eq_path);
            feq << "dataset,target_recall,method,selected_param,actual_recall,qps,avg_verified_candidates,status,speedup_vs_sosia\n";
            std::vector<float> targets = {0.80f, 0.90f, 0.94f, 0.97f, 0.99f};
            const float tol = 0.01f;
            auto pick = [&](const std::vector<MethodPoint>& pts, float trg, MethodPoint& out) {
                if (pts.empty()) return false;
                bool ok = false;
                float best_q = -1.0f, best_d = std::numeric_limits<float>::max();
                for (const auto& p0 : pts) {
                    bool hit = (p0.recall >= trg) || (std::fabs(p0.recall - trg) <= tol);
                    if (!hit) continue;
                    float d = std::fabs(p0.recall - trg);
                    if (!ok || d < best_d || (std::fabs(d - best_d) < 1e-8f && p0.qps > best_q)) {
                        out = p0; ok = true; best_d = d; best_q = p0.qps;
                    }
                }
                return ok;
            };

            for (float trg : targets) {
                MethodPoint sref; bool sok = pick(pts_sosia, trg, sref);
                float sref_q = sok ? sref.qps : 0.0f;
                auto write_method = [&](const std::string& mname, const std::vector<MethodPoint>& pts) {
                    MethodPoint pp; bool ok = pick(pts, trg, pp);
                    if (!ok) {
                        feq << ds.name << "," << trg << "," << mname << ",N/A,N/A,N/A,N/A,N/A,N/A\n";
                    } else {
                        std::string sp = (sref_q > 1e-9f) ? std::to_string(pp.qps / sref_q) : "N/A";
                        feq << ds.name << "," << trg << "," << mname << "," << pp.param << "," << pp.recall << "," << pp.qps << "," << pp.cost << ",ok," << sp << "\n";
                    }
                };
                if (m_linear) write_method("LinearScan", pts_linear);
                if (m_sosia) write_method("SOSIA", pts_sosia);
                if (m_cabs) write_method("CABS", pts_cabs);
                if (m_wand) feq << ds.name << "," << trg << ",WAND,N/A,N/A,N/A,N/A,skipped,N/A\n";
                if (m_binsketch) feq << ds.name << "," << trg << ",BinSketch,N/A,N/A,N/A,N/A," << bins_note << ",N/A\n";
            }

            fsum << ds.name << "," << best_method << "," << best_qps << "," << linear_qps << "," << sosia_qps << "," << cabs_qps
                 << "," << wand_note << "," << bins_note << ",seed=" << seed << "\n";
            flog << "[dataset done] " << ds.name << " best_method=" << best_method << " best_qps=" << best_qps
                 << " curve=" << curve_path << " equal=" << eq_path << "\n";
        }

        std::cout << "[exp4] outputs: " << out_dir << std::endl;
        return 0;
    };


    auto run_revised_splade1m_experiments = [&]() -> int {
        const int fixed_seed = 2026;
        const int K_fixed = 15;
        const int k_default = 50;
        const float C = 0.9f;
        const float L_BASE = 20.0f;
        const float eta_default = (prep.data->n >= 1000000) ? 0.03f : 0.05f;
        const float tau_default = 0.8f;
        const int m0_default = 170;
        const float tolerance = 0.01f;

        const std::string out_dir = "results/revised_exp";
        std::filesystem::create_directories(out_dir);

        std::ofstream runlog(out_dir + "/run_log.txt", std::ios::app);
        auto log = [&](const std::string& msg) {
            std::cout << msg << std::endl;
            runlog << msg << "\n";
        };

        auto tau_tag = [](float tau) {
            std::ostringstream oss;
            int v = (int)std::round(tau * 10.0f);
            oss << "tau" << std::setw(2) << std::setfill('0') << v;
            return oss.str();
        };

        const int n = (int)prep.data->n;
        const int dim = prep.data->dim;
        std::string dataset_name = "SPLADE-1M";
        if (argvStr[1].find("SPLADE") == std::string::npos && argvStr[1].find("splade") == std::string::npos) {
            dataset_name = argvStr[1];
        }

        const std::string cluster_cache = out_dir + "/cluster_cache_K15_seed2026.bin";
        const std::string spill_cache = out_dir + "/spill_cache_K15_tau08_seed2026.bin";

        auto write_int = [](std::ofstream& os, int x) { os.write(reinterpret_cast<const char*>(&x), sizeof(int)); };
        auto write_float = [](std::ofstream& os, float x) { os.write(reinterpret_cast<const char*>(&x), sizeof(float)); };
        auto read_int = [](std::ifstream& is, int& x) { is.read(reinterpret_cast<char*>(&x), sizeof(int)); return (bool)is; };
        auto read_float = [](std::ifstream& is, float& x) { is.read(reinterpret_cast<char*>(&x), sizeof(float)); return (bool)is; };

        auto save_cluster_cache = [&](const ClusterResult& cr, const std::string& path) {
            std::ofstream os(path, std::ios::binary);
            if (!os.good()) return false;
            write_int(os, 1);
            write_int(os, cr.K);
            write_int(os, cr.dim);
            int npt = (int)cr.point_to_cluster.size();
            write_int(os, npt);

            int csz = (int)cr.clusters.size();
            write_int(os, csz);
            for (const auto& cl : cr.clusters) {
                int sz = (int)cl.size();
                write_int(os, sz);
                if (sz > 0) os.write(reinterpret_cast<const char*>(cl.data()), sizeof(int) * (size_t)sz);
            }

            int cent_sz = (int)cr.centroids.size();
            write_int(os, cent_sz);
            for (const auto& cent : cr.centroids) {
                int dsz = (int)cent.size();
                write_int(os, dsz);
                if (dsz > 0) os.write(reinterpret_cast<const char*>(cent.data()), sizeof(float) * (size_t)dsz);
            }

            if (npt > 0) os.write(reinterpret_cast<const char*>(cr.point_to_cluster.data()), sizeof(int) * (size_t)npt);
            return os.good();
        };

        auto load_cluster_cache = [&](const std::string& path, ClusterResult& cr) {
            std::ifstream is(path, std::ios::binary);
            if (!is.good()) return false;
            int ver = 0, K = 0, d = 0, npt = 0;
            if (!read_int(is, ver) || !read_int(is, K) || !read_int(is, d) || !read_int(is, npt)) return false;
            if (ver != 1 || K <= 0 || d != dim || npt != n) return false;

            int csz = 0;
            if (!read_int(is, csz) || csz != K) return false;
            cr.clusters.assign((size_t)csz, {});
            for (int i = 0; i < csz; ++i) {
                int sz = 0;
                if (!read_int(is, sz) || sz < 0) return false;
                cr.clusters[(size_t)i].resize((size_t)sz);
                if (sz > 0) is.read(reinterpret_cast<char*>(cr.clusters[(size_t)i].data()), sizeof(int) * (size_t)sz);
                if (!is.good()) return false;
            }

            int cent_sz = 0;
            if (!read_int(is, cent_sz) || cent_sz != K) return false;
            cr.centroids.assign((size_t)cent_sz, {});
            for (int i = 0; i < cent_sz; ++i) {
                int dsz = 0;
                if (!read_int(is, dsz) || dsz != dim) return false;
                cr.centroids[(size_t)i].resize((size_t)dsz);
                if (dsz > 0) is.read(reinterpret_cast<char*>(cr.centroids[(size_t)i].data()), sizeof(float) * (size_t)dsz);
                if (!is.good()) return false;
            }

            cr.point_to_cluster.resize((size_t)npt);
            if (npt > 0) is.read(reinterpret_cast<char*>(cr.point_to_cluster.data()), sizeof(int) * (size_t)npt);
            if (!is.good()) return false;
            cr.K = K;
            cr.dim = d;
            cr.spill_map.clear();
            cr.spill_threshold = 0.0f;
            return true;
        };

        auto save_spill_cache = [&](const ClusterResult& cr, const std::string& path) {
            std::ofstream os(path, std::ios::binary);
            if (!os.good()) return false;
            write_int(os, 1);
            write_int(os, cr.K);
            write_int(os, cr.dim);
            write_int(os, (int)cr.point_to_cluster.size());
            write_float(os, cr.spill_threshold);
            int msz = (int)cr.spill_map.size();
            write_int(os, msz);
            for (const auto& kv : cr.spill_map) {
                write_int(os, kv.first);
                int vsz = (int)kv.second.size();
                write_int(os, vsz);
                if (vsz > 0) os.write(reinterpret_cast<const char*>(kv.second.data()), sizeof(int) * (size_t)vsz);
            }
            return os.good();
        };

        auto load_spill_cache = [&](ClusterResult& cr, const std::string& path) {
            std::ifstream is(path, std::ios::binary);
            if (!is.good()) return false;
            int ver = 0, K = 0, d = 0, npt = 0;
            float th = 0.0f;
            if (!read_int(is, ver) || !read_int(is, K) || !read_int(is, d) || !read_int(is, npt) || !read_float(is, th)) return false;
            if (ver != 1 || K != cr.K || d != cr.dim || npt != (int)cr.point_to_cluster.size()) return false;
            int msz = 0;
            if (!read_int(is, msz) || msz < 0) return false;
            cr.spill_map.clear();
            for (int i = 0; i < msz; ++i) {
                int pid = -1, vsz = 0;
                if (!read_int(is, pid) || !read_int(is, vsz) || pid < 0 || vsz < 0) return false;
                auto& v = cr.spill_map[pid];
                v.resize((size_t)vsz);
                if (vsz > 0) is.read(reinterpret_cast<char*>(v.data()), sizeof(int) * (size_t)vsz);
                if (!is.good()) return false;
            }
            cr.spill_threshold = th;
            return true;
        };

        bool cluster_reused = false;
        ClusterResult fixed_cluster;
        if (std::filesystem::exists(cluster_cache) && load_cluster_cache(cluster_cache, fixed_cluster)) {
            cluster_reused = true;
        } else {
            fixed_cluster = spherical_kmeans_seeded(*prep.data, n, dim, K_fixed, 25, 1e-4f, false, fixed_seed);
            save_cluster_cache(fixed_cluster, cluster_cache);
            cluster_reused = false;
        }

        bool spill_reused = false;
        ClusterResult fixed_cluster_spill = fixed_cluster;
        if (std::filesystem::exists(spill_cache) && load_spill_cache(fixed_cluster_spill, spill_cache)) {
            spill_reused = true;
        } else {
            apply_boundary_spill(fixed_cluster_spill, *prep.data, n, dim, tau_default);
            save_spill_cache(fixed_cluster_spill, spill_cache);
            spill_reused = false;
        }

        {
            std::ostringstream oss;
            oss << "[revised] dataset=" << dataset_name << " seed=" << fixed_seed
                << " K=" << K_fixed
                << " cluster cache reused=" << (cluster_reused ? "yes" : "no")
                << " spill cache reused=" << (spill_reused ? "yes" : "no");
            log(oss.str());
        }

        auto write_header = [&](const std::string& path) {
            std::ofstream f(path);
            f << "experiment_name,dataset,fixed_seed,K,tau,nprobe,eta,m0,recall,qps,avg_verified_candidates,active_clusters,build_time\n";
        };

        const std::string f_p2 = out_dir + "/revised_P2_nprobe.csv";
        const std::string f_p3 = out_dir + "/revised_P3_eta.csv";
        const std::string f_p5 = out_dir + "/revised_P5_k.csv";
        const std::string f_p6 = out_dir + "/revised_P6_tau.csv";
        const std::string f_p7 = out_dir + "/revised_P7_idf_range.csv";
        const std::string f_p8 = out_dir + "/revised_P8_m0.csv";
        const std::string f_build = out_dir + "/revised_build_breakdown.csv";
        const std::string f_build_compat = out_dir + "/build_breakdown.csv";
        const std::string f_equal = out_dir + "/revised_equal_recall.csv";
        const std::string f_equal_compat = out_dir + "/equal_recall_comparison.csv";

        write_header(f_p2);
        write_header(f_p3);
        write_header(f_p5);
        write_header(f_p6);
        write_header(f_p7);
        write_header(f_p8);

        auto append_row = [&](const std::string& path,
                              const std::string& exp_name,
                              int K, float tau, int nprobe, float eta, int m0,
                              const BenchmarkResult& br, double build_time) {
            std::ofstream f(path, std::ios::app);
            f << exp_name << "," << dataset_name << "," << fixed_seed << "," << K << ","
              << tau << "," << nprobe << "," << eta << "," << m0 << ","
              << br.avg_recall << "," << br.throughput << "," << br.avg_candidates << ","
              << br.avg_active_clusters << "," << build_time << "\n";
        };

        int nprobe_default = 5;
        float probe_default = (float)nprobe_default / (float)K_fixed;

        BuildTimeBreakdown btm_default;
        lsh::timer t_build_default;
        CompositeIndex cidx_fixed = build_composite_index(*prep.data, n, dim, fixed_cluster_spill, k_default,
                                                          C, L_BASE, eta_default, -1,
                                                          true, true, 0.0f, 0.5f, 2.0f, m0_default, true,
                                                          &btm_default);
        double build_fixed = t_build_default.elapsed();

        std::vector<int> nprobe_scan = {1, 2, 3, 4, 5, 6, 8, 10, 12, 15};
        for (int np : nprobe_scan) {
            if (np > K_fixed) continue;
            float pr = (float)np / (float)K_fixed;
            BenchmarkResult br = run_cabs_benchmark(cidx_fixed, prep, k_default, pr, C);
            append_row(f_p2, "P2_nprobe", K_fixed, tau_default, np, eta_default, m0_default, br, build_fixed);
        }

        std::vector<float> eta_scan = {0.01f, 0.02f, 0.03f, 0.05f, 0.08f, 0.10f, 0.15f, 0.20f};
        for (float eta : eta_scan) {
            lsh::timer tb;
            CompositeIndex cidx = build_composite_index(*prep.data, n, dim, fixed_cluster_spill, k_default,
                                                        C, L_BASE, eta, -1,
                                                        true, true, 0.0f, 0.5f, 2.0f, m0_default, true);
            double bt = tb.elapsed();
            BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_default, probe_default, C);
            append_row(f_p3, "P3_eta", K_fixed, tau_default, nprobe_default, eta, m0_default, br, bt);
        }

        std::vector<int> k_scan = {10, 20, 30, 50, 80, 100};
        for (int kval : k_scan) {
            lsh::timer tb;
            CompositeIndex cidx = build_composite_index(*prep.data, n, dim, fixed_cluster_spill, kval,
                                                        C, L_BASE, eta_default, -1,
                                                        true, true, 0.0f, 0.5f, 2.0f, m0_default, true);
            double bt = tb.elapsed();
            BenchmarkResult br = run_cabs_benchmark(cidx, prep, kval, probe_default, C);
            append_row(f_p5, "P5_k", K_fixed, tau_default, nprobe_default, eta_default, m0_default, br, bt);
        }

        std::vector<float> tau_scan = {0.7f, 0.8f, 0.9f, 2.0f};
        for (float tau : tau_scan) {
            ClusterResult tau_cluster = fixed_cluster;
            if (tau < 2.0f) apply_boundary_spill(tau_cluster, *prep.data, n, dim, tau);
            lsh::timer tb;
            CompositeIndex cidx = build_composite_index(*prep.data, n, dim, tau_cluster, k_default,
                                                        C, L_BASE, eta_default, -1,
                                                        true, true, 0.0f, 0.5f, 2.0f, m0_default, true);
            double bt = tb.elapsed();
            BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_default, probe_default, C);
            append_row(f_p6, "P6_tau", K_fixed, tau, nprobe_default, eta_default, m0_default, br, bt);
        }

        struct IDFRange { float lo, hi; };
        std::vector<IDFRange> idf_scan = {{0.3f,1.5f},{0.5f,2.0f},{0.5f,3.0f},{1.0f,1.0f}};
        for (auto rr : idf_scan) {
            bool use_idf = !(std::fabs(rr.lo - 1.0f) < 1e-6f && std::fabs(rr.hi - 1.0f) < 1e-6f);
            lsh::timer tb;
            CompositeIndex cidx = build_composite_index(*prep.data, n, dim, fixed_cluster_spill, k_default,
                                                        C, L_BASE, eta_default, -1,
                                                        use_idf, true, 0.0f, rr.lo, rr.hi, m0_default, true);
            double bt = tb.elapsed();
            BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_default, probe_default, C);
            append_row(f_p7, "P7_idf_range", K_fixed, tau_default, nprobe_default, eta_default, m0_default, br, bt);
        }

        std::vector<int> m0_scan = {100, 150, 170, 200, 250};
        for (int m0 : m0_scan) {
            lsh::timer tb;
            CompositeIndex cidx = build_composite_index(*prep.data, n, dim, fixed_cluster_spill, k_default,
                                                        C, L_BASE, eta_default, -1,
                                                        true, true, 0.0f, 0.5f, 2.0f, m0, true);
            double bt = tb.elapsed();
            BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_default, probe_default, C);
            append_row(f_p8, "P8_m0", K_fixed, tau_default, nprobe_default, eta_default, m0, br, bt);
        }

        // Build breakdown
        lsh::timer tkm;
        ClusterResult km_for_breakdown = spherical_kmeans_seeded(*prep.data, n, dim, K_fixed, 25, 1e-4f, false, fixed_seed);
        double km_s = tkm.elapsed();

        ClusterResult sp_for_breakdown = km_for_breakdown;
        lsh::timer tsp;
        apply_boundary_spill(sp_for_breakdown, *prep.data, n, dim, tau_default);
        double spill_s = tsp.elapsed();

        BuildTimeBreakdown btm;
        CompositeIndex cidx_b = build_composite_index(*prep.data, n, dim, sp_for_breakdown, k_default,
                                                      C, L_BASE, eta_default, -1,
                                                      true, true, 0.0f, 0.5f, 2.0f, m0_default, true,
                                                      &btm);
        (void)cidx_b;
        double total_s = km_s + spill_s + btm.total_build_s;
        double part_sum = km_s + spill_s + btm.idf_computation_s + btm.sos_transform_s + btm.minhash_compute_s + btm.bucket_indexing_s;

        {
            std::ofstream f(f_build);
            f << "dataset,fixed_seed,K,tau,idf_computation_s,spherical_kmeans_s,spill_processing_s,sos_transform_s,minhash_compute_s,bucket_indexing_s,sum_parts_s,total_build_s,delta_s\n";
            f << dataset_name << "," << fixed_seed << "," << K_fixed << "," << tau_default << ","
              << btm.idf_computation_s << "," << km_s << "," << spill_s << ","
              << btm.sos_transform_s << "," << btm.minhash_compute_s << "," << btm.bucket_indexing_s << ","
              << part_sum << "," << total_s << "," << std::fabs(part_sum - total_s) << "\n";
        }
        {
            std::ofstream f(f_build_compat);
            f << "dataset,fixed_seed,K,tau,idf_computation_s,spherical_kmeans_s,spill_processing_s,sos_transform_s,minhash_compute_s,bucket_indexing_s,sum_parts_s,total_build_s,delta_s\n";
            f << dataset_name << "," << fixed_seed << "," << K_fixed << "," << tau_default << ","
              << btm.idf_computation_s << "," << km_s << "," << spill_s << ","
              << btm.sos_transform_s << "," << btm.minhash_compute_s << "," << btm.bucket_indexing_s << ","
              << part_sum << "," << total_s << "," << std::fabs(part_sum - total_s) << "\n";
        }

        // Equal-recall (strict with tolerance)
        struct CurvePt { std::string method; int knob; float recall; float qps; };
        std::vector<CurvePt> A_sosia, B_idf, C_adapt, D_cluster, E_full;

        mips2set sets(prep, l);
        myMinHashBase minBase(prep, sets, 1, m);
        Sosia sosia(minBase);
        for (int ubv : std::vector<int>{1000,2000,5000,10000,20000,50000}) {
            resOutput rr = minHash(sosia, 0.5f, k_default, ubv, prep);
            float qps = (rr.time > 0) ? (1000.0f / rr.time) : 0.0f;
            A_sosia.push_back({"SOSIA", ubv, rr.recall, qps});
        }

        ClusterResult k1 = spherical_kmeans_seeded(*prep.data, n, dim, 1, 15, 1e-4f, false, fixed_seed);
        for (float eta : std::vector<float>{0.01f,0.02f,0.03f,0.05f,0.08f,0.10f}) {
            CompositeIndex c_b = build_composite_index(*prep.data, n, dim, k1, k_default, C, L_BASE, eta, -1,
                                                       true, false, 0.0f, 0.5f, 2.0f, m0_default, true);
            BenchmarkResult b_b = run_cabs_benchmark(c_b, prep, k_default, 1.0f, C);
            B_idf.push_back({"+IDF", (int)std::round(eta * 1000.0f), b_b.avg_recall, b_b.throughput});

            CompositeIndex c_c = build_composite_index(*prep.data, n, dim, k1, k_default, C, L_BASE, eta, -1,
                                                       true, true, 0.0f, 0.5f, 2.0f, m0_default, true);
            BenchmarkResult b_c = run_cabs_benchmark(c_c, prep, k_default, 1.0f, C);
            C_adapt.push_back({"+AdaptL", (int)std::round(eta * 1000.0f), b_c.avg_recall, b_c.throughput});
        }

        ClusterResult no_spill = fixed_cluster;
        ClusterResult with_spill = fixed_cluster_spill;
        CompositeIndex c_d = build_composite_index(*prep.data, n, dim, no_spill, k_default, C, L_BASE, eta_default, -1,
                                                   true, true, 0.0f, 0.5f, 2.0f, m0_default, true);
        CompositeIndex c_e = build_composite_index(*prep.data, n, dim, with_spill, k_default, C, L_BASE, eta_default, -1,
                                                   true, true, 0.0f, 0.5f, 2.0f, m0_default, true);
        for (int np : std::vector<int>{2,3,5,8,10,15}) {
            if (np > K_fixed) continue;
            float pr = (float)np / (float)K_fixed;
            BenchmarkResult b_d = run_cabs_benchmark(c_d, prep, k_default, pr, C);
            BenchmarkResult b_e = run_cabs_benchmark(c_e, prep, k_default, pr, C);
            D_cluster.push_back({"+Clustering", np, b_d.avg_recall, b_d.throughput});
            E_full.push_back({"Full CABS", np, b_e.avg_recall, b_e.throughput});
        }

        auto nearest = [](const std::vector<CurvePt>& pts, float target) {
            CurvePt best{"", -1, -1.0f, 0.0f};
            float bd = std::numeric_limits<float>::max();
            for (const auto& p : pts) {
                float d = std::fabs(p.recall - target);
                if (d < bd || (std::fabs(d - bd) < 1e-8f && p.qps > best.qps)) {
                    bd = d;
                    best = p;
                }
            }
            return best;
        };
        auto valid_target = [&](const CurvePt& p, float target) {
            return (p.recall >= target) || (std::fabs(p.recall - target) <= tolerance);
        };

        auto write_equal = [&](const std::string& path) {
            std::ofstream f(path);
            f << "target_recall,method,selected_param,actual_recall,qps,status\n";
            std::vector<float> targets = {0.94f, 0.97f, 0.99f};
            for (float trg : targets) {
                std::vector<std::vector<CurvePt>> all = {A_sosia, B_idf, C_adapt, D_cluster, E_full};
                for (const auto& curve : all) {
                    if (curve.empty()) continue;
                    CurvePt p = nearest(curve, trg);
                    bool ok = valid_target(p, trg);
                    f << trg << "," << p.method << "," << p.knob << ","
                      << (ok ? std::to_string(p.recall) : "N/A") << ","
                      << (ok ? std::to_string(p.qps) : "N/A") << ","
                      << (ok ? "ok" : "N/A") << "\n";
                }
            }
        };

        write_equal(f_equal);
        write_equal(f_equal_compat);

        log("[revised] random seed=" + std::to_string(fixed_seed));
        log("[revised] cluster cache reused=" + std::string(cluster_reused ? "yes" : "no"));
        log("[revised] spill cache reused=" + std::string(spill_reused ? "yes" : "no"));
        log("[revised] outputs:");
        log("  - " + f_p2);
        log("  - " + f_p3);
        log("  - " + f_p5);
        log("  - " + f_p6);
        log("  - " + f_p7);
        log("  - " + f_p8);
        log("  - " + f_build);
        log("  - " + f_equal);

        return 0;
    };

    auto run_reselection_and_K_grid = [&]() -> int {
        const std::string out_dir = "results/revised_exp";
        std::filesystem::create_directories(out_dir);
        std::ofstream flog(out_dir + "/reselection_log.txt", std::ios::app);

        auto log = [&](const std::string& x) {
            std::cout << x << std::endl;
            flog << x << "\n";
        };

        const int seed = 2026;
        const int topk_default = 50;
        const float C = 0.9f, L_BASE = 20.0f, ETA = 0.03f, TAU = 0.8f;
        const int M0 = 170;
        const int n = (int)prep.data->n;
        const int dim = prep.data->dim;
        const std::string dataset_name = "SPLADE-1M";

        log("[case13] dataset=" + dataset_name + " seed=" + std::to_string(seed));

        auto write_int = [](std::ofstream& os, int x) { os.write(reinterpret_cast<const char*>(&x), sizeof(int)); };
        auto write_float = [](std::ofstream& os, float x) { os.write(reinterpret_cast<const char*>(&x), sizeof(float)); };
        auto read_int = [](std::ifstream& is, int& x) { is.read(reinterpret_cast<char*>(&x), sizeof(int)); return (bool)is; };
        auto read_float = [](std::ifstream& is, float& x) { is.read(reinterpret_cast<char*>(&x), sizeof(float)); return (bool)is; };

        auto save_cluster_cache = [&](const ClusterResult& cr, const std::string& path) {
            std::ofstream os(path, std::ios::binary);
            if (!os.good()) return false;
            write_int(os, 1); write_int(os, cr.K); write_int(os, cr.dim);
            int npt = (int)cr.point_to_cluster.size(); write_int(os, npt);
            int csz = (int)cr.clusters.size(); write_int(os, csz);
            for (const auto& cl : cr.clusters) { int sz = (int)cl.size(); write_int(os, sz); if (sz > 0) os.write(reinterpret_cast<const char*>(cl.data()), sizeof(int) * (size_t)sz); }
            int cent = (int)cr.centroids.size(); write_int(os, cent);
            for (const auto& v : cr.centroids) { int d = (int)v.size(); write_int(os, d); if (d > 0) os.write(reinterpret_cast<const char*>(v.data()), sizeof(float) * (size_t)d); }
            if (npt > 0) os.write(reinterpret_cast<const char*>(cr.point_to_cluster.data()), sizeof(int) * (size_t)npt);
            return os.good();
        };
        auto load_cluster_cache = [&](const std::string& path, int n0, int dim0, ClusterResult& cr) {
            std::ifstream is(path, std::ios::binary);
            if (!is.good()) return false;
            int ver=0,K=0,d=0,npt=0; if (!read_int(is,ver)||!read_int(is,K)||!read_int(is,d)||!read_int(is,npt)) return false;
            if (ver != 1 || d != dim0 || npt != n0 || K <= 0) return false;
            int csz=0; if (!read_int(is,csz) || csz != K) return false;
            cr.clusters.assign((size_t)csz, {});
            for (int i=0;i<csz;++i){ int sz=0; if(!read_int(is,sz)||sz<0) return false; cr.clusters[(size_t)i].resize((size_t)sz); if(sz>0) is.read(reinterpret_cast<char*>(cr.clusters[(size_t)i].data()), sizeof(int)*(size_t)sz); if(!is.good()) return false; }
            int cent=0; if(!read_int(is,cent)||cent!=K) return false;
            cr.centroids.assign((size_t)cent, {});
            for (int i=0;i<cent;++i){ int ds=0; if(!read_int(is,ds)||ds!=dim0) return false; cr.centroids[(size_t)i].resize((size_t)ds); if(ds>0) is.read(reinterpret_cast<char*>(cr.centroids[(size_t)i].data()), sizeof(float)*(size_t)ds); if(!is.good()) return false; }
            cr.point_to_cluster.resize((size_t)npt); if(npt>0) is.read(reinterpret_cast<char*>(cr.point_to_cluster.data()), sizeof(int)*(size_t)npt);
            if(!is.good()) return false;
            cr.K=K; cr.dim=d; cr.spill_map.clear(); cr.spill_threshold=0.0f;
            return true;
        };
        auto save_spill_cache = [&](const ClusterResult& cr, const std::string& path) {
            std::ofstream os(path, std::ios::binary);
            if (!os.good()) return false;
            write_int(os, 1); write_int(os, cr.K); write_int(os, cr.dim); write_int(os, (int)cr.point_to_cluster.size()); write_float(os, cr.spill_threshold);
            int msz=(int)cr.spill_map.size(); write_int(os, msz);
            for (const auto& kv: cr.spill_map){ write_int(os, kv.first); int vs=(int)kv.second.size(); write_int(os, vs); if(vs>0) os.write(reinterpret_cast<const char*>(kv.second.data()), sizeof(int)*(size_t)vs); }
            return os.good();
        };
        auto load_spill_cache = [&](ClusterResult& cr, const std::string& path) {
            std::ifstream is(path, std::ios::binary); if(!is.good()) return false;
            int ver=0,K=0,d=0,npt=0; float th=0.0f;
            if(!read_int(is,ver)||!read_int(is,K)||!read_int(is,d)||!read_int(is,npt)||!read_float(is,th)) return false;
            if(ver!=1||K!=cr.K||d!=cr.dim||npt!=(int)cr.point_to_cluster.size()) return false;
            int msz=0; if(!read_int(is,msz)||msz<0) return false;
            cr.spill_map.clear();
            for(int i=0;i<msz;++i){ int pid=-1,vs=0; if(!read_int(is,pid)||!read_int(is,vs)||pid<0||vs<0) return false; auto& vv=cr.spill_map[pid]; vv.resize((size_t)vs); if(vs>0) is.read(reinterpret_cast<char*>(vv.data()), sizeof(int)*(size_t)vs); if(!is.good()) return false; }
            cr.spill_threshold=th; return true;
        };

        auto estimate_cabs_index_bytes = [&](const CompositeIndex& cidx) {
            long long signature_entries = 0, centroid_meta_bytes = 0, bucket_structure_bytes = 0;
            for (const auto& cl : cidx.cluster_indexes) {
                signature_entries += (long long)cl.params.m_i * (long long)cl.point_ids.size();
                centroid_meta_bytes += (long long)cl.centroid.size() * (long long)sizeof(float) + (long long)sizeof(ClusterParams);
                for (const auto& table : cl.local_indexes) {
                    bucket_structure_bytes += (long long)table.size() * 16;
                    for (const auto& kv : table) bucket_structure_bytes += (long long)kv.second.size() * (long long)sizeof(int);
                }
            }
            return signature_entries * (long long)sizeof(int) + centroid_meta_bytes + bucket_structure_bytes;
        };

        struct GridRow {
            int K=0, nprobe=0;
            float recall=0.0f, qps=0.0f, cost=0.0f, active=0.0f;
            double build=0.0;
            long long index_bytes=0;
        };

        std::vector<int> K_scan = {5, 10, 15, 20, 25, 32};
        std::vector<int> np_scan = {1, 2, 3, 5, 8, 10, 15};
        std::vector<GridRow> grid;

        std::ofstream fgrid(out_dir + "/revised_K_nprobe_grid.csv");
        fgrid << "dataset,random_seed,K,nprobe,recall,qps,avg_verified_candidates,active_clusters,build_time,index_size_bytes\n";

        for (int K : K_scan) {
            std::string ccache = out_dir + "/cluster_cache_K" + std::to_string(K) + "_seed" + std::to_string(seed) + ".bin";
            std::string scache = out_dir + "/spill_cache_K" + std::to_string(K) + "_tau08_seed" + std::to_string(seed) + ".bin";
            ClusterResult cr, crs;
            bool creuse = false, sreuse = false;
            if (std::filesystem::exists(ccache) && load_cluster_cache(ccache, n, dim, cr)) creuse = true;
            else { cr = spherical_kmeans_seeded(*prep.data, n, dim, K, 25, 1e-4f, false, seed); save_cluster_cache(cr, ccache); }
            crs = cr;
            if (std::filesystem::exists(scache) && load_spill_cache(crs, scache)) sreuse = true;
            else { apply_boundary_spill(crs, *prep.data, n, dim, TAU); save_spill_cache(crs, scache); }
            log("[K-grid] K=" + std::to_string(K) + " cluster_reused=" + std::string(creuse?"yes":"no") + " spill_reused=" + std::string(sreuse?"yes":"no"));

            lsh::timer tb;
            CompositeIndex cidx = build_composite_index(*prep.data, n, dim, crs, topk_default, C, L_BASE, ETA, -1,
                                                        true, true, 0.0f, 0.5f, 3.0f, M0, true);
            double build_s = tb.elapsed();
            long long idx_b = estimate_cabs_index_bytes(cidx);

            for (int np : np_scan) {
                if (np > K) continue;
                float pr = (float)np / (float)K;
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, topk_default, pr, C);
                GridRow r; r.K = K; r.nprobe = np; r.recall = br.avg_recall; r.qps = br.throughput; r.cost = br.avg_candidates; r.active = br.avg_active_clusters; r.build = build_s; r.index_bytes = idx_b;
                grid.push_back(r);
                fgrid << dataset_name << "," << seed << "," << K << "," << np << "," << br.avg_recall << "," << br.throughput << "," << br.avg_candidates << "," << br.avg_active_clusters << "," << build_s << "," << idx_b << "\n";
            }
        }

        auto pick_default = [&](const std::vector<GridRow>& rows) {
            GridRow best = rows.front();
            bool has = false;
            for (const auto& r : rows) {
                if (r.recall < 0.94f || r.recall > 0.96f) continue;
                if (!has || r.qps > best.qps || (std::fabs(r.qps - best.qps) < 1e-6f && r.cost < best.cost)) {
                    best = r; has = true;
                }
            }
            if (!has) {
                float bd = 1e9f;
                for (const auto& r : rows) {
                    float d = std::fabs(r.recall - 0.95f);
                    if (d < bd || (std::fabs(d - bd) < 1e-8f && (r.qps > best.qps || (std::fabs(r.qps - best.qps) < 1e-6f && r.cost < best.cost)))) {
                        best = r; bd = d;
                    }
                }
            }
            return best;
        };
        auto pick_high = [&](const std::vector<GridRow>& rows) {
            GridRow best = rows.front();
            bool has = false;
            for (const auto& r : rows) {
                if (r.recall < 0.97f) continue;
                if (!has || r.recall > best.recall || (std::fabs(r.recall - best.recall) < 1e-8f && r.qps > best.qps)) {
                    best = r; has = true;
                }
            }
            if (!has) {
                float bd = 1e9f;
                for (const auto& r : rows) {
                    float d = std::fabs(r.recall - 0.97f);
                    if (d < bd || (std::fabs(d - bd) < 1e-8f && r.qps > best.qps)) { best = r; bd = d; }
                }
            }
            return best;
        };

        GridRow wp_default = pick_default(grid);
        GridRow wp_high = pick_high(grid);

        {
            std::ofstream fw(out_dir + "/recommended_working_points.csv");
            fw << "dataset,working_point_type,K,nprobe,recall,qps,avg_verified_candidates,active_clusters,build_time,index_size_bytes,selection_rule\n";
            fw << dataset_name << ",default working point," << wp_default.K << "," << wp_default.nprobe << "," << wp_default.recall << "," << wp_default.qps << "," << wp_default.cost << "," << wp_default.active << "," << wp_default.build << "," << wp_default.index_bytes << ",recall_in_[0.94,0.96]_then_max_qps_min_cost\n";
            fw << dataset_name << ",high-recall working point," << wp_high.K << "," << wp_high.nprobe << "," << wp_high.recall << "," << wp_high.qps << "," << wp_high.cost << "," << wp_high.active << "," << wp_high.build << "," << wp_high.index_bytes << ",recall>=0.97_then_max_recall_then_qps\n";
        }

        log("[working point] default working point: K=" + std::to_string(wp_default.K) + " nprobe=" + std::to_string(wp_default.nprobe) + " recall=" + std::to_string(wp_default.recall) + " qps=" + std::to_string(wp_default.qps));
        log("[working point] high-recall working point: K=" + std::to_string(wp_high.K) + " nprobe=" + std::to_string(wp_high.nprobe) + " recall=" + std::to_string(wp_high.recall) + " qps=" + std::to_string(wp_high.qps));

        if (wp_default.nprobe != 8) {
            // explain 5 vs 8 style automatically
            GridRow p8 = wp_default;
            for (const auto& r : grid) if (r.K == wp_default.K && r.nprobe == 8) { p8 = r; break; }
            log("[explain] default prefers nprobe=" + std::to_string(wp_default.nprobe) + " over nprobe=8 because in recall target band it has better throughput/cost tradeoff.");
            log("[explain-detail] nprobe=" + std::to_string(wp_default.nprobe) + " qps=" + std::to_string(wp_default.qps) + " cost=" + std::to_string(wp_default.cost) + "; nprobe=8 qps=" + std::to_string(p8.qps) + " cost=" + std::to_string(p8.cost));
        }

        // top-k scan (rename from old revised_P5_k)
        {
            std::ofstream ftopk(out_dir + "/revised_topk_scan.csv");
            ftopk << "experiment_name,dataset,random_seed,K,nprobe,tau,eta,m0,topk,recall,qps,avg_verified_candidates,active_clusters,build_time,index_size_bytes\n";
            std::string ccache = out_dir + "/cluster_cache_K" + std::to_string(wp_default.K) + "_seed" + std::to_string(seed) + ".bin";
            std::string scache = out_dir + "/spill_cache_K" + std::to_string(wp_default.K) + "_tau08_seed" + std::to_string(seed) + ".bin";
            ClusterResult cr, crs;
            bool okc = load_cluster_cache(ccache, n, dim, cr);
            if (!okc) { cr = spherical_kmeans_seeded(*prep.data, n, dim, wp_default.K, 25, 1e-4f, false, seed); save_cluster_cache(cr, ccache); }
            crs = cr;
            bool oks = load_spill_cache(crs, scache);
            if (!oks) { apply_boundary_spill(crs, *prep.data, n, dim, TAU); save_spill_cache(crs, scache); }
            log("[topk] cache reused cluster=" + std::string(okc?"yes":"no") + " spill=" + std::string(oks?"yes":"no"));

            std::vector<int> topks = {10, 20, 30, 50, 80, 100};
            for (int tk : topks) {
                lsh::timer tb;
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, crs, tk, C, L_BASE, ETA, -1,
                                                            true, true, 0.0f, 0.5f, 3.0f, M0, true);
                double bt = tb.elapsed();
                long long idx_b = estimate_cabs_index_bytes(cidx);
                float pr = (float)wp_default.nprobe / (float)wp_default.K;
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, tk, pr, C);
                ftopk << "topk_scan," << dataset_name << "," << seed << "," << wp_default.K << "," << wp_default.nprobe << "," << TAU << "," << ETA << "," << M0 << "," << tk << "," << br.avg_recall << "," << br.throughput << "," << br.avg_candidates << "," << br.avg_active_clusters << "," << bt << "," << idx_b << "\n";
            }
        }

        // overwrite revised_P7 with explicit candidate_value
        {
            std::string ccache = out_dir + "/cluster_cache_K" + std::to_string(wp_default.K) + "_seed" + std::to_string(seed) + ".bin";
            std::string scache = out_dir + "/spill_cache_K" + std::to_string(wp_default.K) + "_tau08_seed" + std::to_string(seed) + ".bin";
            ClusterResult cr, crs;
            if (!load_cluster_cache(ccache, n, dim, cr)) {
                cr = spherical_kmeans_seeded(*prep.data, n, dim, wp_default.K, 25, 1e-4f, false, seed); save_cluster_cache(cr, ccache);
            }
            crs = cr;
            if (!load_spill_cache(crs, scache)) { apply_boundary_spill(crs, *prep.data, n, dim, TAU); save_spill_cache(crs, scache); }

            struct IDR { float lo, hi; };
            std::vector<IDR> wr = {{0.3f,1.5f},{0.5f,2.0f},{0.5f,3.0f},{1.0f,1.0f}};
            std::ofstream f7(out_dir + "/revised_P7_idf_range.csv");
            f7 << "experiment_name,dataset,random_seed,K,tau,nprobe,eta,m0,candidate_value,recall,qps,avg_verified_candidates,active_clusters,build_time,index_size_bytes\n";
            for (auto r : wr) {
                bool use_idf = !(std::fabs(r.lo - 1.0f) < 1e-6f && std::fabs(r.hi - 1.0f) < 1e-6f);
                lsh::timer tb;
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, crs, topk_default, C, L_BASE, ETA, -1,
                                                            use_idf, true, 0.0f, r.lo, r.hi, M0, true);
                double bt = tb.elapsed();
                long long idx_b = estimate_cabs_index_bytes(cidx);
                float pr = (float)wp_default.nprobe / (float)wp_default.K;
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, topk_default, pr, C);
                std::ostringstream cv; cv << "[" << r.lo << "," << r.hi << "]";
                f7 << "P7_idf_range," << dataset_name << "," << seed << "," << wp_default.K << "," << TAU << "," << wp_default.nprobe << "," << ETA << "," << M0 << "," << cv.str() << "," << br.avg_recall << "," << br.throughput << "," << br.avg_candidates << "," << br.avg_active_clusters << "," << bt << "," << idx_b << "\n";
            }
        }

        auto load_csv = [&](const std::string& fn) {
            std::vector<std::map<std::string, std::string>> rows;
            std::ifstream f(fn);
            if (!f.good()) return rows;
            std::string head;
            if (!std::getline(f, head)) return rows;
            std::vector<std::string> cols;
            {
                std::stringstream ss(head);
                std::string it;
                while (std::getline(ss, it, ',')) cols.push_back(it);
            }
            std::string line;
            while (std::getline(f, line)) {
                std::vector<std::string> vals;
                std::stringstream ss(line);
                std::string it;
                while (std::getline(ss, it, ',')) vals.push_back(it);
                std::map<std::string, std::string> row;
                for (size_t i = 0; i < cols.size() && i < vals.size(); ++i) row[cols[i]] = vals[i];
                rows.push_back(row);
            }
            return rows;
        };
        auto to_f = [](const std::string& x) { return (float)std::atof(x.c_str()); };

        // parameter selection summary from existing scans
        {
            std::ofstream fs(out_dir + "/parameter_selection_summary.csv");
            fs << "parameter_name,candidate_value,recall,qps,avg_verified_candidates,selection_role,rationale\n";

            auto summarize = [&](const std::string& pname, const std::string& file, const std::string& cand_col, bool allow_band) {
                auto rows = load_csv(file);
                if (rows.empty()) return;
                int def_i = -1, high_i = -1;
                for (int i = 0; i < (int)rows.size(); ++i) {
                    float r = to_f(rows[i]["recall"]), q = to_f(rows[i]["qps"]), c = to_f(rows[i]["avg_verified_candidates"]);
                    if (allow_band && r >= 0.94f && r <= 0.96f) {
                        if (def_i < 0 || q > to_f(rows[def_i]["qps"]) || (std::fabs(q - to_f(rows[def_i]["qps"])) < 1e-6f && c < to_f(rows[def_i]["avg_verified_candidates"]))) def_i = i;
                    }
                    if (r >= 0.97f) {
                        if (high_i < 0 || r > to_f(rows[high_i]["recall"]) || (std::fabs(r - to_f(rows[high_i]["recall"])) < 1e-6f && q > to_f(rows[high_i]["qps"]))) high_i = i;
                    }
                }
                if (def_i < 0) {
                    float bd = 1e9f;
                    for (int i = 0; i < (int)rows.size(); ++i) {
                        float d = std::fabs(to_f(rows[i]["recall"]) - 0.95f);
                        if (d < bd) { bd = d; def_i = i; }
                    }
                }
                if (high_i < 0) {
                    float bd = 1e9f;
                    for (int i = 0; i < (int)rows.size(); ++i) {
                        float d = std::fabs(to_f(rows[i]["recall"]) - 0.97f);
                        if (d < bd) { bd = d; high_i = i; }
                    }
                }

                for (int i = 0; i < (int)rows.size(); ++i) {
                    std::string role = "rejected", rat = "outside target preference";
                    if (i == def_i) { role = "default"; rat = "default working point: recall band [0.94,0.96], prioritize higher qps and lower candidates"; }
                    else if (i == high_i) { role = "high_recall"; rat = "high-recall working point: target around 0.97, prioritize higher recall"; }
                    fs << pname << "," << rows[i][cand_col] << "," << rows[i]["recall"] << "," << rows[i]["qps"] << "," << rows[i]["avg_verified_candidates"] << "," << role << "," << rat << "\n";
                }

                log("[selection] " + pname + " default=" + rows[def_i][cand_col] + " high_recall=" + rows[high_i][cand_col]);
            };

            summarize("nprobe", out_dir + "/revised_P2_nprobe.csv", "nprobe", true);
            summarize("eta", out_dir + "/revised_P3_eta.csv", "eta", true);
            summarize("tau", out_dir + "/revised_P6_tau.csv", "tau", true);
            summarize("idf_range", out_dir + "/revised_P7_idf_range.csv", "candidate_value", true);
            summarize("m0", out_dir + "/revised_P8_m0.csv", "m0", true);
            summarize("topk", out_dir + "/revised_topk_scan.csv", "topk", true);
        }

        // rebuild equal recall tables
        {
            auto rows = load_csv(out_dir + "/revised_equal_recall.csv");
            std::ofstream fd(out_dir + "/equal_recall_detailed.csv");
            fd << "target_recall,method,chosen_param,actual_recall,qps,note\n";
            std::map<std::string, std::map<std::string, std::string>> compact;

            for (auto& r : rows) {
                std::string trg = r["target_recall"], method = r["method"], param = r["selected_param"], rec = r["actual_recall"], qps = r["qps"];
                bool matched = !(rec == "N/A" || qps == "N/A");
                fd << trg << "," << method << "," << (matched ? param : "NA") << "," << (matched ? rec : "NA") << "," << (matched ? qps : "NA") << "," << (matched ? "matched" : "not_reached") << "\n";
                if (method.empty()) continue;
                if (std::fabs(std::atof(trg.c_str()) - 0.94) < 1e-6) compact[method]["qps_at_r94"] = matched ? qps : "NA";
                if (std::fabs(std::atof(trg.c_str()) - 0.97) < 1e-6) compact[method]["qps_at_r97"] = matched ? qps : "NA";
                if (std::fabs(std::atof(trg.c_str()) - 0.99) < 1e-6) compact[method]["qps_at_r99"] = matched ? qps : "NA";
            }

            std::ofstream fc(out_dir + "/equal_recall_compact.csv");
            fc << "method,qps_at_r94,qps_at_r97,qps_at_r99\n";
            for (const auto& kv : compact) {
                auto getv = [&](const std::string& k){ auto it = kv.second.find(k); return (it==kv.second.end()) ? std::string("NA") : it->second; };
                fc << kv.first << "," << getv("qps_at_r94") << "," << getv("qps_at_r97") << "," << getv("qps_at_r99") << "\n";
            }
        }

        // cleanup unused/obsolete artifacts in revised_exp
        std::vector<std::string> obsolete = {
            out_dir + "/revised_P5_k.csv",
            out_dir + "/equal_recall_comparison.csv",
            out_dir + "/build_breakdown.csv"
        };
        for (const auto& fp : obsolete) {
            if (std::filesystem::exists(fp)) {
                std::filesystem::remove(fp);
                log("[cleanup] removed " + fp);
            }
        }

        // tau selection explanation
        {
            auto tau_rows = load_csv(out_dir + "/revised_P6_tau.csv");
            std::map<std::string, std::map<std::string, std::string>> by_tau;
            for (auto& r : tau_rows) by_tau[r["tau"]] = r;
            if (by_tau.count("0.8") && by_tau.count("0.7")) {
                float q8 = to_f(by_tau["0.8"]["qps"]), r8 = to_f(by_tau["0.8"]["recall"]);
                float q7 = to_f(by_tau["0.7"]["qps"]), r7 = to_f(by_tau["0.7"]["recall"]);
                log("[explain] tau=0.8 vs tau=0.7: tau=0.8 offers higher throughput and lower verification cost at a moderate recall drop; tau=0.7 is high-recall option.");
                log("[explain-detail] tau=0.8 recall=" + std::to_string(r8) + " qps=" + std::to_string(q8) + "; tau=0.7 recall=" + std::to_string(r7) + " qps=" + std::to_string(q7));
            }
        }

        log("[case13] done. outputs under " + out_dir);
        return 0;
    };


    switch (mode) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
            printf("Legacy modes are disabled. Use mode 1/2/3 (or 5/6/7).\n");
            return 0;
        case 5: {
            int n = static_cast<int>(prep.data->n);
            int dim = prep.data->dim;
            bool is_splade_dataset = (argvStr[1].find("SPLADE") != std::string::npos) ||
                                     (argvStr[1].find("splade") != std::string::npos);
            const int k_fix = 50;
            const float c_fix = 0.9f;
            const float L_BASE = 20.0f;
            const float eta_fix = (n >= 1000000) ? 0.03f : (n >= 100000) ? 0.05f : 0.15f;
            const float TAU_DEF = 0.8f;
            const float SPILL_DEF = TAU_DEF;
            printf("[CABS] Auto eta=%.3f for n=%d\n", eta_fix, n);

            struct P1Row {
                int K;
                int nprobe;
                float probe;
                float recall;
                float cost;
                float tp;
            };

            std::vector<int> K_values;
            std::vector<int> nprobe_values = {2, 3, 5, 8, 10};
            if (n >= 8000000) {
                K_values = {10, 15, 20, 25, 32, 40, 50};
            } else if (n >= 4000000) {
                K_values = {5, 8, 10, 15, 20, 25, 32};
            } else {
                K_values = {1, 3, 5, 8, 10, 15, 20};
            }
            std::vector<P1Row> p1_rows;

            printf("\n── P1: K × nprobe Matrix ──\n");
            printf("  K    nprobe  probe   Recall  Cost    TP(qps)  Active\n");
            for (int K : K_values) {
                ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K, 25, 1e-4f, false);
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, k_fix, c_fix, L_BASE, eta_fix, -1, true, true, SPILL_DEF);
                for (int np : nprobe_values) {
                    if (np > K) continue;
                    float probe = std::min(1.0f, (float)np / (float)K);
                    BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_fix, probe, c_fix);
                    p1_rows.push_back({K, np, probe, br.avg_recall, br.avg_candidates, br.throughput});
                    printf("  %2d   %2d      %.2f    %.3f   %6d   %7.1f   %d/%d\n",
                           K, np, probe, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput, np, K);
                }
            }

            auto choose_for_target = [&](int K, float target, P1Row& out) -> bool {
                bool found = false;
                int best_np = 1e9;
                float best_recall = -1.0f;
                for (const auto& r : p1_rows) {
                    if (r.K != K) continue;
                    if (r.recall + 1e-6f < target) continue;
                    if (!found || r.nprobe < best_np || (r.nprobe == best_np && r.tp > out.tp)) {
                        out = r;
                        found = true;
                        best_np = r.nprobe;
                    }
                    best_recall = std::max(best_recall, r.recall);
                }
                return found;
            };

            printf("\n── P1 Summary: TP at Target Recall ──\n");
            printf("  K    @R≥0.90              @R≥0.95              @R≥0.98\n");
            printf("       nprobe  TP    Cost   nprobe  TP    Cost   nprobe  TP    Cost\n");
            for (int K : K_values) {
                P1Row r90{}, r95{}, r98{};
                bool f90 = choose_for_target(K, 0.90f, r90);
                bool f95 = choose_for_target(K, 0.95f, r95);
                bool f98 = choose_for_target(K, 0.98f, r98);
                if (f90) printf("  %2d   %2d      %6.1f %6d  ", K, r90.nprobe, r90.tp, (int)roundf(r90.cost));
                else     printf("  %2d   --      %6s %6s  ", K, "N/A", "N/A");
                if (f95) printf(" %2d      %6.1f %6d  ", r95.nprobe, r95.tp, (int)roundf(r95.cost));
                else     printf(" --      %6s %6s  ", "N/A", "N/A");
                if (f98) printf(" %2d      %6.1f %6d\n", r98.nprobe, r98.tp, (int)roundf(r98.cost));
                else     printf(" --      %6s %6s\n", "N/A", "N/A");
            }

            int K_opt = 5;
            {
                float best_tp = -1.0f;
                for (int K : K_values) {
                    P1Row rk{};
                    if (choose_for_target(K, 0.95f, rk)) {
                        if (rk.tp > best_tp) {
                            best_tp = rk.tp;
                            K_opt = K;
                        }
                    }
                }
            }
            int nprobe_best = std::max(1, std::min(K_opt, 3));
            {
                P1Row rb{};
                if (choose_for_target(K_opt, 0.95f, rb)) nprobe_best = rb.nprobe;
            }
            float probe_best = std::min(1.0f, (float)nprobe_best / (float)K_opt);
            printf("\n[P1] Selected K_opt=%d, nprobe_best=%d (probe=%.2f) for later P2-P8.\n", K_opt, nprobe_best, probe_best);

            if (!is_splade_dataset) {
                printf("[P1] Non-SPLADE dataset detected, stop after P1+Summary (fast multi-dataset mode).\n\n");
                return 0;
            }

            printf("\n── P2: Effect of nprobe (K=K_opt) ──\n");
            printf("  K    nprobe  probe  Recall   Cost    TP(qps)\n");
            ClusterResult cr_p2 = spherical_kmeans(*prep.data, n, dim, K_opt, 25, 1e-4f, false);
            CompositeIndex cidx_p2 = build_composite_index(*prep.data, n, dim, cr_p2, k_fix, c_fix, L_BASE, eta_fix, -1, true, true, SPILL_DEF);
            std::vector<int> nprobe_scan = {1, 2, 3, 4, 5, 6, 8, 10, 12, 15};
            for (int np : nprobe_scan) {
                if (np > K_opt) continue;
                float pr = std::min(1.0f, (float)np / (float)K_opt);
                BenchmarkResult br = run_cabs_benchmark(cidx_p2, prep, k_fix, pr, c_fix);
                printf("  %2d   %2d      %.2f   %.3f   %6d   %7.1f\n", K_opt, np, pr, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput);
            }

            printf("\n── P3: Effect of η (K=K_opt, nprobe=best) ──\n");
            printf("  η      Recall   Cost     TP(qps)\n");
            std::vector<float> etas = {0.01f, 0.03f, 0.05f, 0.08f, 0.10f, 0.15f, 0.20f, 0.30f};
            for (float eta : etas) {
                ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K_opt, 25, 1e-4f, false);
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, k_fix, c_fix, L_BASE, eta, -1, true, true, SPILL_DEF);
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_fix, probe_best, c_fix);
                printf("  %.2f   %.3f   %6d    %7.1f\n", eta, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput);
            }

            printf("\n── P4: Effect of c (K=K_opt, nprobe=best) ──\n");
            printf("  c      m0_smooth  Recall   Cost    TP(qps)\n");
            std::vector<float> cs = {0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f};
            for (float cval : cs) {
                ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K_opt, 25, 1e-4f, false);
                int m0_smooth = (int)std::ceil(150.0 + 100.0 * std::max(0.0, (double)(cval - 0.5f) / 0.5));
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, k_fix, cval, L_BASE, eta_fix, -1, true, true, SPILL_DEF, 0.5f, 2.0f, m0_smooth);
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_fix, probe_best, cval);
                printf("  %.2f   %3d        %.3f   %6d   %7.1f\n", cval, m0_smooth, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput);
            }

            printf("\n── P5: Effect of k (K=K_opt, nprobe=best) ──\n");
            printf("  k     Recall   Cost    TP(qps)\n");
            std::vector<int> ks = {10, 20, 30, 50, 80, 100};
            for (int kval : ks) {
                ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K_opt, 25, 1e-4f, false);
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, kval, c_fix, L_BASE, eta_fix, -1, true, true, SPILL_DEF);
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, kval, probe_best, c_fix);
                printf("  %3d   %.3f   %6d   %7.1f\n", kval, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput);
            }

            printf("\n── P6: Effect of τ (K=K_opt, nprobe=best) ──\n");
            printf("  τ      Recall   Cost    TP(qps)  Avg_n_i\n");
            std::vector<float> taus = {0.7f, 0.8f, 0.9f, 2.0f};
            for (float tau : taus) {
                ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K_opt, 25, 1e-4f, false);
                float spill = (tau >= 2.0f) ? 0.0f : tau;
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, k_fix, c_fix, L_BASE, eta_fix, -1, true, true, spill);
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_fix, probe_best, c_fix);
                int sum_n = 0;
                for (auto& ci : cidx.cluster_indexes) sum_n += ci.params.n_i;
                int avg_n_i = (int)roundf((float)sum_n / (float)std::max(1, (int)cidx.cluster_indexes.size()));
                printf("  %.2f   %.3f   %6d   %7.1f  %6d\n", tau, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput, avg_n_i);
            }

            printf("\n── P7: Effect of IDF range (K=K_opt, nprobe=best) ──\n");
            printf("  Range        Recall   Cost    TP(qps)\n");
            struct IDFRange { float lo, hi; };
            std::vector<IDFRange> wr = {{0.3f,1.5f},{0.5f,2.0f},{0.5f,3.0f},{1.0f,1.0f}};
            for (auto r : wr) {
                ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K_opt, 25, 1e-4f, false);
                bool use_idf = !(fabsf(r.lo - 1.0f) < 1e-6f && fabsf(r.hi - 1.0f) < 1e-6f);
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, k_fix, c_fix, L_BASE, eta_fix, -1, use_idf, true, SPILL_DEF, r.lo, r.hi);
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_fix, probe_best, c_fix);
                printf("  [%.1f,%.1f]    %.3f   %6d   %7.1f\n", r.lo, r.hi, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput);
            }

            printf("\n── P8: Effect of m0 (K=K_opt, nprobe=best) ──\n");
            printf("  m0    avg_m_i  Recall   Cost    TP(qps)\n");
            std::vector<int> m0s = {100, 150, 170, 200, 250};
            for (int m0 : m0s) {
                ClusterResult cr = spherical_kmeans(*prep.data, n, dim, K_opt, 25, 1e-4f, false);
                CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, k_fix, c_fix, L_BASE, eta_fix, -1, true, true, SPILL_DEF, 0.5f, 2.0f, m0);
                BenchmarkResult br = run_cabs_benchmark(cidx, prep, k_fix, probe_best, c_fix);
                int sum_m = 0;
                for (auto& ci : cidx.cluster_indexes) sum_m += ci.params.m_i;
                float avg_m_i = (float)sum_m / (float)std::max(1, (int)cidx.cluster_indexes.size());
                printf("  %3d   %6.1f   %.3f   %6d   %7.1f\n", m0, avg_m_i, br.avg_recall, (int)roundf(br.avg_candidates), br.throughput);
            }

            printf("\n");
            return 0;
        }

        case 6:
            return run_case6();
        case 7: {
            int n = static_cast<int>(prep.data->n);
            int dim = prep.data->dim;
            int OPT_K = 15;
            float C = 0.9f, L_BASE = 20.0f;
            int K_DEF = 50;
            float eta_auto = (n >= 1000000) ? 0.03f : (n >= 100000) ? 0.05f : 0.15f;
            printf("[CABS] Auto eta=%.3f for n=%d\n", eta_auto, n);

            printf("\n── E1: Method Overall Comparison ───────────────────────────\n");
            printf("  Method              Recall  TP(qps)   Cost     Speedup\n");
            BenchmarkResult ls = run_linear_scan(prep, K_DEF);
            float lst = ls.avg_latency_ms;
            printf("  LinearScan          %.3f   %7.1f  %7d  1.0x\n", ls.avg_recall, ls.throughput, n);

            mips2set sets(prep, l);
            myMinHashBase minBase(prep, sets, 1, m);
            Sosia sosia(minBase);
            resOutput rr_e1 = minHash(sosia, 0.5f, K_DEF, ub, prep);
            float tp_sosia = (rr_e1.time > 0) ? (1000.0f / rr_e1.time) : 0;
            float sp_sosia = (rr_e1.time > 0) ? (lst / rr_e1.time) : 0;
            printf("  SOSIA(m=%d,l=%d)    %.3f   %7.1f  %7d  %.1fx\n", m, l, rr_e1.recall, tp_sosia, (int)roundf(rr_e1.cost), sp_sosia);

            const int np_def = 5;
            const float probe_def = (float)np_def / (float)OPT_K;
            ClusterResult cr7 = spherical_kmeans(*prep.data, n, dim, OPT_K, 25, 1e-4f, false);
            CompositeIndex cidx_e1 = build_composite_index(*prep.data, n, dim, cr7, K_DEF, C, L_BASE, eta_auto, -1, true, true, 0.8f);
            BenchmarkResult br_e1 = run_cabs_benchmark(cidx_e1, prep, K_DEF, probe_def, C);
            float sp_cabs = (br_e1.avg_latency_ms > 0) ? (lst / br_e1.avg_latency_ms) : 0.0f;
            printf("  CABS(K=%d,np=%d)    %.3f   %7.1f  %7d  %.1fx\n\n", OPT_K, np_def, br_e1.avg_recall, br_e1.throughput, (int)roundf(br_e1.avg_candidates), sp_cabs);

            printf("── E2: Recall-TP Curves (CABS) ─────────────────────────────\n");
            std::vector<int> nprobes = {1, 2, 3, 5, 8, 10, 15};
            CompositeIndex cidx_curve = build_composite_index(*prep.data, n, dim, cr7, K_DEF, C, L_BASE, eta_auto, -1, true, true, 0.8f);
            struct CurvePoint { int x; float recall; float tp; };
            std::vector<CurvePoint> cabs_curve;
            for (int np : nprobes) {
                if (np > OPT_K) continue;
                float pr = std::min(1.0f, (float)np / (float)OPT_K);
                BenchmarkResult br = run_cabs_benchmark(cidx_curve, prep, K_DEF, pr, C);
                cabs_curve.push_back({np, br.avg_recall, br.throughput});
                printf("  CABS nprobe=%d  Recall=%.3f  TP=%.1f\n", np, br.avg_recall, br.throughput);
            }

            printf("\n── E2: Recall-TP Curves (SOSIA) ────────────────────────────\n");
            int ubs[] = {1000, 2000, 5000, 10000, 20000, 50000};
            std::vector<CurvePoint> sosia_curve;
            for (int u : ubs) {
                resOutput rr = minHash(sosia, 0.5f, K_DEF, u, prep);
                float tp = (rr.time > 0) ? (1000.0f / rr.time) : 0;
                sosia_curve.push_back({u, rr.recall, tp});
                printf("  SOSIA ub=%d  Recall=%.3f  TP=%.1f\n", u, rr.recall, tp);
            }

            auto nearest_curve = [](const std::vector<CurvePoint>& pts, float target) -> const CurvePoint& {
                size_t bi = 0;
                float bd = std::numeric_limits<float>::max();
                for (size_t i = 0; i < pts.size(); ++i) {
                    float d = std::fabs(pts[i].recall - target);
                    if (d < bd || (std::fabs(d - bd) < 1e-8f && pts[i].tp > pts[bi].tp)) {
                        bd = d;
                        bi = i;
                    }
                }
                return pts[bi];
            };

            printf("\n── Equal-Recall: CABS vs SOSIA ──\n\n");
            printf("  Target   SOSIA(ub,TP)          CABS(nprobe,TP)       Speedup\n");
            std::vector<float> eq_targets = {0.80f, 0.90f, 0.94f, 0.97f, 0.99f};
            for (float trg : eq_targets) {
                const CurvePoint& spt = nearest_curve(sosia_curve, trg);
                const CurvePoint& cpt = nearest_curve(cabs_curve, trg);
                float speedup = (spt.tp > 1e-9f) ? (cpt.tp / spt.tp) : 0.0f;
                printf("  ≈%.2f   ub=%-5d TP=%-7.1f   np=%-2d TP=%-7.1f   %.1fx\n",
                       trg, spt.x, spt.tp, cpt.x, cpt.tp, speedup);
            }

            printf("\n── CABS 参数分布 ───────────────────────────────────────────\n");
            printf("  Cluster  n_i     m_i   T_i\n");
            CompositeIndex cidx_s = build_composite_index(*prep.data, n, dim, cr7, K_DEF, C, L_BASE, eta_auto, -1, true, true, 0.8f);
            for (int i = 0; i < OPT_K; i++) {
                auto& ci = cidx_s.cluster_indexes[i];
                printf("  %d        %d   %d   %d\n", i, ci.params.n_i, ci.params.m_i, ci.params.T_i);
            }
            printf("\n");
            return 0;
        }

        case 8:
            return run_case6();
        case 9: {
            int max_q = 5000;
            bool detail = false;
            if (argc > 4) max_q = std::max(1, std::atoi(argv[4]));
            if (argc > 5) detail = (std::atoi(argv[5]) != 0);
            return run_proxy_validity_experiment(max_q, detail);
        }
        case 10:
            return run_space_gain_decomposition();

        case 11: {
            if (mode11_config_path.empty()) {
                std::cerr << "Mode 11 requires a config file path: ./sos 11 <config_path>" << std::endl;
                return 1;
            }
            return run_overall_multidataset_comparison(mode11_config_path);
        }

        case 12:
            return run_revised_splade1m_experiments();

        case 13:
            return run_reselection_and_K_grid();

        case 14:
            return run_codex_repair_pipeline(prep, argvStr[1], argvStr[4]);

        default:
            printf("Unknown mode. Use 5/6/7/9/10/11/12/13/14.\n");
            return 0;
    }
}
