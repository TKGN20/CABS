#include "Preprocess.h"
#include "cluster_index.h"
#include "cluster_query.h"
#include "spherical_kmeans.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <unordered_set>

static std::string join_top_ids(const std::vector<int>& v, int k = 10) {
    std::ostringstream oss;
    int n = std::min((int)v.size(), k);
    for (int i = 0; i < n; ++i) {
        if (i) oss << ';';
        oss << v[(size_t)i];
    }
    return oss.str();
}

int main(int argc, char** argv) {
    if (argc < 14) {
        std::cerr << "Usage: cabs_spot_audit <base.csr> <query.csr> <ben> <out.csv> <K> <eta> <tau> <nprobe> <seed> <topk> <qsample> <target_recall> <label>\n";
        return 1;
    }
    const std::string base = argv[1];
    const std::string query = argv[2];
    const std::string ben = argv[3];
    const std::string out_csv = argv[4];
    const int K = std::atoi(argv[5]);
    const float ETA = std::atof(argv[6]);
    const float TAU = std::atof(argv[7]);
    const int nprobe = std::atoi(argv[8]);
    const int seed = std::atoi(argv[9]);
    const int topk = std::atoi(argv[10]);
    const int qsample = std::atoi(argv[11]);
    const float target = std::atof(argv[12]);
    const std::string label = argv[13];

    Preprocess prep(base, query, ben);
    const int n = (int)prep.data->n;
    const int dim = prep.data->dim;
    const float C = 0.9f, L_BASE = 20.0f;
    const int M0 = 170;

    ClusterResult cr = spherical_kmeans_seeded(*prep.data, n, dim, K, 25, 1e-4f, false, seed);
    apply_boundary_spill(cr, *prep.data, n, dim, TAU);
    CompositeIndex cidx = build_composite_index(*prep.data, n, dim, cr, topk, C, L_BASE, ETA, -1, true, true, 0.0f, 0.5f, 3.0f, M0, true);

    int qn = std::min(prep.benchmark.N, (int)prep.queries->n);
    std::vector<int> qids(qn);
    for (int i = 0; i < qn; ++i) qids[(size_t)i] = i;
    std::mt19937 rng(seed);
    std::shuffle(qids.begin(), qids.end(), rng);
    if ((int)qids.size() > qsample) qids.resize((size_t)qsample);

    std::ofstream ofs(out_csv);
    ofs << "query_id,truth_top10,cabs_top10,recall50,full_eval_count\n";

    double recall_sum = 0.0;
    double fe_sum = 0.0;
    double rec_min = 1.0, rec_max = 0.0;

    for (int qi : qids) {
        const size_t qb = prep.queries->indptr[(size_t)qi];
        const size_t qe = prep.queries->indptr[(size_t)qi + 1];
        const int* qind = prep.queries->indices + qb;
        const float* qval = prep.queries->val + qb;
        const int qnnz = (int)(qe - qb);

        size_t full_eval = 0;
        float pr = std::min(1.0f, (float)nprobe / (float)std::max(1, K));
        auto res = query_cabs_with_full_eval(cidx, *prep.data, qind, qval, qnnz, dim, topk, pr, &full_eval);

        std::vector<int> truth_ids;
        truth_ids.reserve((size_t)topk);
        for (int r = 0; r < topk; ++r) truth_ids.push_back(prep.benchmark.indice[qi][r]);

        std::unordered_set<int> gtset;
        gtset.reserve((size_t)topk * 2);
        for (int id : truth_ids) gtset.insert(id);

        int hit = 0;
        std::vector<int> cabs_ids;
        cabs_ids.reserve((size_t)topk);
        for (int r = 0; r < std::min(topk, (int)res.size()); ++r) {
            cabs_ids.push_back(res[(size_t)r].point_id);
            if (gtset.count(res[(size_t)r].point_id)) hit++;
        }
        float rec = (float)hit / (float)std::max(1, topk);

        recall_sum += rec;
        fe_sum += (double)full_eval;
        rec_min = std::min(rec_min, (double)rec);
        rec_max = std::max(rec_max, (double)rec);

        ofs << qi << ",\"" << join_top_ids(truth_ids, 10) << "\",\"" << join_top_ids(cabs_ids, 10)
            << "\"," << std::fixed << std::setprecision(4) << rec
            << "," << full_eval << "\n";
    }
    ofs.close();

    const double avg_rec = recall_sum / std::max(1, (int)qids.size());
    const double avg_fe = fe_sum / std::max(1, (int)qids.size());

    std::string status = "verified";
    if (avg_rec < target - 0.03 || rec_min < target - 0.10) status = "provisional";
    if (avg_rec < target - 0.08) status = "reject";

    std::cout << "[AUDIT] label=" << label
              << " target=" << target
              << " avg_recall50=" << std::fixed << std::setprecision(4) << avg_rec
              << " min=" << rec_min
              << " max=" << rec_max
              << " avg_full_eval=" << std::setprecision(1) << avg_fe
              << " status=" << status
              << " out=" << out_csv << "\n";
    return 0;
}
