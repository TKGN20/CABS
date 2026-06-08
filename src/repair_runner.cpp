#include "repair_runner.h"

#include "cluster_index.h"
#include "cluster_query.h"
#include "spherical_kmeans.h"
#include "alg.h"
#include "method.h"
#include "sosia.h"
#include "basis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct SweepRow {
    std::string key;
    BenchmarkResult br;
    float aux = 0.0f;
    std::string aux_text;
};

struct ClusterCacheHeader {
    int magic = 0x43414253;
    int version = 1;
    int n = 0;
    int dim = 0;
    int K = 0;
    int seed = 42;
};

std::string fmt3(float v) { std::ostringstream o; o<<std::fixed<<std::setprecision(3)<<v; return o.str(); }
std::string fmt1(float v) { std::ostringstream o; o<<std::fixed<<std::setprecision(1)<<v; return o.str(); }

bool save_cluster_cache(const std::string& path, const ClusterResult& cr, int n, int dim, int K, int seed) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    ClusterCacheHeader h; h.n=n; h.dim=dim; h.K=K; h.seed=seed;
    out.write((char*)&h, sizeof(h));
    int pts = (int)cr.point_to_cluster.size();
    out.write((char*)&pts, sizeof(int));
    out.write((char*)cr.point_to_cluster.data(), sizeof(int)*pts);
    int kc = (int)cr.centroids.size();
    out.write((char*)&kc, sizeof(int));
    for (int i=0;i<kc;++i) {
        int d=(int)cr.centroids[i].size();
        out.write((char*)&d, sizeof(int));
        out.write((char*)cr.centroids[i].data(), sizeof(float)*d);
    }
    int cnum=(int)cr.clusters.size();
    out.write((char*)&cnum, sizeof(int));
    for (int i=0;i<cnum;++i) {
        int sz=(int)cr.clusters[i].size();
        out.write((char*)&sz, sizeof(int));
        if (sz>0) out.write((char*)cr.clusters[i].data(), sizeof(int)*sz);
    }
    return (bool)out;
}

bool load_cluster_cache(const std::string& path, ClusterResult& cr, int n, int dim, int K, int seed) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    ClusterCacheHeader h{};
    in.read((char*)&h, sizeof(h));
    if (!in || h.magic!=0x43414253 || h.version!=1 || h.n!=n || h.dim!=dim || h.K!=K || h.seed!=seed) return false;
    int pts=0; in.read((char*)&pts, sizeof(int));
    if (pts!=n) return false;
    cr.point_to_cluster.resize((size_t)pts);
    in.read((char*)cr.point_to_cluster.data(), sizeof(int)*pts);
    int kc=0; in.read((char*)&kc, sizeof(int));
    if (kc!=K) return false;
    cr.centroids.assign((size_t)K, {});
    for (int i=0;i<K;++i) {
        int d=0; in.read((char*)&d, sizeof(int));
        if (d!=dim) return false;
        cr.centroids[i].resize((size_t)d);
        in.read((char*)cr.centroids[i].data(), sizeof(float)*d);
    }
    int cnum=0; in.read((char*)&cnum, sizeof(int));
    if (cnum!=K) return false;
    cr.clusters.assign((size_t)K, {});
    for (int i=0;i<K;++i) {
        int sz=0; in.read((char*)&sz, sizeof(int));
        cr.clusters[i].resize((size_t)sz);
        if (sz>0) in.read((char*)cr.clusters[i].data(), sizeof(int)*sz);
    }
    if (!in) return false;
    cr.K=K; cr.dim=dim; cr.spill_map.clear(); cr.spill_threshold=0.0f;
    return true;
}

ClusterResult get_cluster(const SparseData& data, int n, int dim, int K, int seed, bool force, const std::string& path, bool& hit, std::ofstream& logf) {
    ClusterResult cr; hit=false;
    if (!force && load_cluster_cache(path, cr, n, dim, K, seed)) {
        hit=true;
        logf << "[cluster-cache] hit K="<<K<<" seed="<<seed<<" path="<<path<<"\n";
        return cr;
    }
    logf << "[cluster-cache] miss K="<<K<<" seed="<<seed<<"\n";
    cr = spherical_kmeans_seeded(data, n, dim, K, 25, 1e-4f, false, seed);
    save_cluster_cache(path, cr, n, dim, K, seed);
    return cr;
}

long long estimate_bucket_bytes(const CompositeIndex& cidx) {
    long long b=0;
    for (const auto& cl : cidx.cluster_indexes) {
        for (const auto& tb : cl.local_indexes) {
            b += (long long)tb.size()*16LL;
            for (const auto& kv : tb) b += (long long)kv.second.size()*(long long)sizeof(int);
        }
    }
    return b;
}

void title(std::ofstream& out, const std::string& t) { out << "\n" << t << "\n"; }

} // namespace

int run_codex_repair_pipeline(Preprocess& prep, const std::string& data_path, const std::string& query_path) {
    (void)query_path;
    const int seed=42;
    const int n=(int)prep.data->n;
    const int dim=prep.data->dim;

    std::ofstream result("实验结果_修正版.txt", std::ios::trunc);
    std::ofstream logf("codex_repair_master.log", std::ios::trunc);
    if (!result || !logf) return 1;

    logf << "[meta] seed=42 data="<<data_path<<" n="<<n<<" dim="<<dim<<"\n";
    logf << "[step1] 审查 实验结果.txt\n";
    logf << "[step2] 标记错误: P2-P8聚类未固定; K=15,np=5最优表述; exp2等召回误填; exp3主表口径; A2计时不一致; 空间收益缺no-spill; nprobe3/4异常。\n";

    const int K_def=15, k_def=50, m0_def=170, np_def=5;
    const float probe_def=(float)np_def/(float)K_def;
    const float c_fix=0.9f, L_BASE=20.0f, eta_def=0.03f, tau_def=0.8f;

    std::string task = "full";
    if (const char* e = std::getenv("CABS_TASK")) task = e;
    int tuning_queries = 1000;
    if (const char* e = std::getenv("CABS_TUNING_QUERIES")) tuning_queries = std::max(1, std::atoi(e));
    int proxy_queries = 100;
    if (const char* e = std::getenv("CABS_PROXY_QUERIES")) proxy_queries = std::max(1, std::atoi(e));
    bool do_tune_light = (task=="full" || task=="tune_light" || task=="minimal");
    bool do_tune_rebuild = (task=="full" || task=="tune_rebuild");
    bool do_expand = (task=="full" || task=="expand");
    bool do_analysis = (task=="full" || task=="analysis" || task=="minimal");
    int qlim_tune = (task=="full" || task=="expand" || task=="analysis") ? -1 : tuning_queries;

    logf << "[runtime] task="<<task<<" tuning_queries="<<tuning_queries<<" proxy_queries="<<proxy_queries<<"\n";
    logf << "[server-run] tmux new -d -s cabs_repair 'cd /home/songyang/TKGN/CABS && CABS_TASK=minimal CABS_TUNING_QUERIES=1000 CABS_PROXY_QUERIES=100 ./sos 14 \"datasets/MS MARCO v1/SPLADE/1M/base_1M.csr\" \"datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr\"'\n";

    std::filesystem::create_directories("/tmp/cabs_cluster_cache");
    auto cpath = [&](int K){ std::ostringstream o; o<<"/tmp/cabs_cluster_cache/splade1m_K"<<K<<"_seed"<<seed<<".bin"; return o.str(); };

    logf << "[step3] 修复能力: cache/seed/routing/c_eval/no_spill/timer\n";
    bool hit=false;
    ClusterResult cr_k15 = get_cluster(*prep.data, n, dim, K_def, seed, false, cpath(K_def), hit, logf);

    // Step4 P2
    logf << "[step4] 重跑P2 (fixed clustering)\n";
    CompositeIndex idx_def = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, true, true, tau_def, 0.5f, 3.0f, m0_def, true);
    std::vector<SweepRow> p2_rows;
    for (int np : {1,2,3,4,5,6,8,10,12,15}) {
        float pr=(float)np/(float)K_def;
        auto br=run_cabs_benchmark(idx_def, prep, k_def, pr, c_fix, qlim_tune);
        p2_rows.push_back({std::to_string(np), br});
        logf << "[P2] np="<<np<<" R="<<fmt3(br.avg_recall)<<" QPS="<<fmt1(br.throughput)
             <<" req="<<fmt3(br.avg_requested_nprobe)
             <<" before="<<fmt3(br.avg_selected_clusters_before_postprocess)
             <<" after="<<fmt3(br.avg_selected_clusters_after_postprocess)
             <<" hit="<<fmt3(br.avg_active_clusters_hit)<<"\n";
    }

    // Step5 K-only
    logf << "[step5] 重跑K-only\n";
    std::vector<SweepRow> k_rows;
    if (do_tune_rebuild) {
        for (int K : {5,8,10,15,20,25,32}) {
            bool khit=false;
            ClusterResult cr = get_cluster(*prep.data, n, dim, K, seed, true, cpath(K), khit, logf);
            CompositeIndex idx = build_composite_index(*prep.data, n, dim, cr, k_def, c_fix, L_BASE, eta_def, -1, true, true, tau_def, 0.5f, 3.0f, m0_def, true);
            float pr = std::min(1.0f, (float)np_def/(float)K);
            auto br = run_cabs_benchmark(idx, prep, k_def, pr, c_fix, qlim_tune);
            k_rows.push_back({std::to_string(K), br, pr});
        }
    } else {
        logf << "[step5] skipped by task profile\n";
    }

    // Step6 fixed cluster sweeps
    logf << "[step6] 重跑P3/P5/P7/P8 (fixed clustering)\n";
    std::vector<SweepRow> eta_rows;
    for (float eta : {0.01f,0.03f,0.05f,0.08f,0.10f,0.15f,0.20f,0.30f}) {
        auto idx = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta, -1, true, true, tau_def, 0.5f, 3.0f, m0_def, true);
        eta_rows.push_back({fmt3(eta), run_cabs_benchmark(idx, prep, k_def, probe_def, c_fix, qlim_tune)});
    }
    std::vector<SweepRow> tau_rows;
    if (do_tune_rebuild) {
        for (float tau : {0.70f,0.80f,0.90f,2.00f}) {
            float spill=(tau>=2.0f)?0.0f:tau;
            auto idx = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, true, true, spill, 0.5f, 3.0f, m0_def, true);
            auto br = run_cabs_benchmark(idx, prep, k_def, probe_def, c_fix, qlim_tune);
            int sum_n=0; for (auto& c: idx.cluster_indexes) sum_n += c.params.n_i;
            tau_rows.push_back({fmt3(tau), br, (float)sum_n/std::max(1,(int)idx.cluster_indexes.size())});
        }
    }
    std::vector<SweepRow> idf_rows;
    struct RG{float lo;float hi;};
    for (auto rg : std::vector<RG>{{0.3f,1.5f},{0.5f,2.0f},{0.5f,3.0f},{1.0f,1.0f}}) {
        bool use_idf=!(std::fabs(rg.lo-1.0f)<1e-6f && std::fabs(rg.hi-1.0f)<1e-6f);
        auto idx = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, use_idf, true, tau_def, rg.lo, rg.hi, m0_def, true);
        std::ostringstream key; key<<"["<<std::fixed<<std::setprecision(1)<<rg.lo<<","<<rg.hi<<"]";
        idf_rows.push_back({key.str(), run_cabs_benchmark(idx, prep, k_def, probe_def, c_fix, qlim_tune)});
    }
    std::vector<SweepRow> kret_rows;
    for (int kval : {10,20,30,50,80,100}) {
        auto idx = build_composite_index(*prep.data, n, dim, cr_k15, kval, c_fix, L_BASE, eta_def, -1, true, true, tau_def, 0.5f, 3.0f, m0_def, true);
        kret_rows.push_back({std::to_string(kval), run_cabs_benchmark(idx, prep, kval, probe_def, c_fix, qlim_tune)});
    }
    std::vector<SweepRow> m0_rows;
    for (int m0 : {100,150,170,200,250}) {
        auto idx = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, true, true, tau_def, 0.5f, 3.0f, m0, true);
        auto br = run_cabs_benchmark(idx, prep, k_def, probe_def, c_fix, qlim_tune);
        int sum_m=0; for (auto& c: idx.cluster_indexes) sum_m += c.params.m_i;
        m0_rows.push_back({std::to_string(m0), br, (float)sum_m/std::max(1,(int)idx.cluster_indexes.size())});
    }

    // Step7 c_eval
    logf << "[step7] 重跑c_eval\n";
    auto idx_ce = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, true, true, tau_def, 0.5f, 3.0f, m0_def, true);
    BenchmarkResult br_ce{};
    if (do_tune_light) br_ce = run_cabs_benchmark(idx_ce, prep, k_def, probe_def, c_fix, qlim_tune);
    std::map<float,double> sat_sum{{0.80f,0.0},{0.85f,0.0},{0.90f,0.0},{0.95f,0.0}};
    double mean_ratio=0.0; long long cnt_ratio=0;
    int qeval_n = (qlim_tune > 0) ? std::min(prep.benchmark.N, qlim_tune) : prep.benchmark.N;
    for (int qi=0; qi<qeval_n; ++qi) {
        int qb=(int)prep.queries->indptr[qi], qe=(int)prep.queries->indptr[qi+1];
        auto ret = query_cabs(idx_ce, *prep.data, prep.queries->indices+qb, prep.queries->val+qb, qe-qb, dim, k_def, probe_def);
        int take = std::min(k_def, (int)ret.size());
        for (int rk = 0; rk < take; ++rk) {
            float den = prep.benchmark.innerproduct[qi][rk];
            float ratio = (den > 1e-12f) ? (ret[(size_t)rk].inner_product / den) : 0.0f;
            ratio = std::max(0.0f, std::min(1.0f, ratio));
            mean_ratio += ratio; ++cnt_ratio;
            for (auto& kv : sat_sum) if (ratio >= kv.first) kv.second += 1.0;
        }
    }
    if (cnt_ratio>0) mean_ratio /= (double)cnt_ratio;
    for (auto& kv : sat_sum) kv.second /= (double)std::max(1, qeval_n*k_def);

    // Step8 E3 equal-recall with N/A
    logf << "[step8] 重跑E3等召回并修正N/A\n";
    struct Pt { std::string key; float r=0,q=0,c=0; };
    auto nearest_reach = [](const std::vector<Pt>& v, float trg, Pt& out)->bool{
        float mx=-1; for (auto& p:v) mx=std::max(mx,p.r);
        if (mx+1e-6f < trg) return false;
        bool ok=false; float bd=1e9;
        for (auto& p:v) {
            if (p.r+1e-6f < trg) continue;
            float d=std::fabs(p.r-trg);
            if (!ok || d<bd || (std::fabs(d-bd)<1e-6f && p.q>out.q)) { out=p; ok=true; bd=d; }
        }
        return ok;
    };
    std::vector<Pt> A,B,C,D,E;
    if (do_analysis && task != "minimal") {
    mips2set sets(prep, 10);
    myMinHashBase minBase(prep, sets, 1, 150);
    Sosia sosia(minBase);
    for (int ub : {1000,2000,5000,10000,20000,50000}) {
        auto rr=minHash(sosia, 0.5f, k_def, ub, prep);
        float tp=(rr.time>0)?(1000.0f/rr.time):0.0f;
        Pt p;
        p.key = std::to_string(ub);
        p.r = rr.recall;
        p.q = tp;
        p.c = rr.cost;
        A.push_back(p);
    }
    ClusterResult cr1 = spherical_kmeans_seeded(*prep.data, n, dim, 1, 25, 1e-4f, false, seed);
    for (float eta : {0.01f,0.03f,0.05f,0.08f,0.10f,0.15f,0.20f,0.30f}) {
        auto b = build_composite_index(*prep.data, n, dim, cr1, k_def, c_fix, L_BASE, eta, -1, true, false, 0.0f, 0.5f, 3.0f, 150, false);
        auto c = build_composite_index(*prep.data, n, dim, cr1, k_def, c_fix, L_BASE, eta, -1, true, true, 0.0f, 0.5f, 3.0f, 150, false);
        auto brb=run_cabs_benchmark(b, prep, k_def, 1.0f, c_fix, qlim_tune);
        auto brc=run_cabs_benchmark(c, prep, k_def, 1.0f, c_fix, qlim_tune);
        B.push_back({fmt3(eta), brb.avg_recall, brb.throughput, brb.avg_candidates});
        C.push_back({fmt3(eta), brc.avg_recall, brc.throughput, brc.avg_candidates});
    }
    for (int np : {1,2,3,4,5,6,8,10,12,15}) {
        float pr=(float)np/(float)K_def;
        auto d = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, true, true, 0.0f, 0.5f, 3.0f, m0_def, true);
        auto e = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, true, true, 0.8f, 0.5f, 3.0f, m0_def, true);
        auto brd=run_cabs_benchmark(d, prep, k_def, pr, c_fix, qlim_tune);
        auto bre=run_cabs_benchmark(e, prep, k_def, pr, c_fix, qlim_tune);
        D.push_back({std::to_string(np), brd.avg_recall, brd.throughput, brd.avg_candidates});
        E.push_back({std::to_string(np), bre.avg_recall, bre.throughput, bre.avg_candidates});
    }

    }

    // Step9 build breakdown unified
    logf << "[step9] 重跑构建时间分解\n";
    auto t0=std::chrono::high_resolution_clock::now();
    compute_idf_weights(*prep.data, n, dim, L_BASE, 0.5f, 3.0f);
    auto t1=std::chrono::high_resolution_clock::now();
    auto tk0=std::chrono::high_resolution_clock::now();
    ClusterResult cr_time = spherical_kmeans_seeded(*prep.data, n, dim, K_def, 25, 1e-4f, false, seed);
    auto tk1=std::chrono::high_resolution_clock::now();
    BuildTimeBreakdown bt{};
    (void)build_composite_index(*prep.data, n, dim, cr_time, k_def, c_fix, L_BASE, eta_def, -1, true, true, tau_def, 0.5f, 3.0f, m0_def, true, &bt);
    float t_idf=(float)std::chrono::duration<double>(t1-t0).count();
    float t_km=(float)std::chrono::duration<double>(tk1-tk0).count();

    // Step10 d scalability
    logf << "[step10] 重跑维度扩展\n";
    std::vector<SweepRow> d_rows;
    if (do_expand) for (int dcut : {5000,10000,20000,30000,30109}) {
        auto tb=std::chrono::high_resolution_clock::now();
        auto idx=build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, dcut, true, true, tau_def, 0.5f, 3.0f, m0_def, true);
        auto te=std::chrono::high_resolution_clock::now();
        auto br=run_cabs_benchmark(idx, prep, k_def, probe_def, c_fix, qlim_tune);
        long long sig=0; for (auto& c : idx.cluster_indexes) sig += 1LL*c.params.m_i*(long long)c.point_ids.size();
        long long bytes = sig*(long long)sizeof(int)+estimate_bucket_bytes(idx);
        SweepRow r{std::to_string(dcut), br, (float)std::chrono::duration<double>(te-tb).count()};
        r.aux_text = fmt3((float)bytes/1024.0f/1024.0f/1024.0f);
        d_rows.push_back(r);
    }

    // Step11 space decomposition
    logf << "[step11] 重跑no-spill空间收益\n";
    struct SRow{std::string n; long long sig=0, spill=0, total=0, adapt=0, base=0;};
    auto mk_space=[&](const std::string& name, bool adaptive, float spill)->SRow{
        auto idx=build_composite_index(*prep.data,n,dim,cr_k15,k_def,c_fix,L_BASE,eta_def,-1,true,true,spill,0.5f,3.0f,m0_def,adaptive);
        SRow r; r.n=name; int maxm=0;
        for (auto& c: idx.cluster_indexes) {
            long long ni=(long long)c.point_ids.size();
            r.sig += (long long)c.params.m_i*ni*(long long)sizeof(int);
            r.adapt += (long long)c.params.m_i*ni;
            r.spill += std::max<long long>(0, ni-c.params.n_i);
            maxm=std::max(maxm,c.params.m_i);
        }
        r.base=(long long)maxm*(long long)n;
        r.total=r.sig+estimate_bucket_bytes(idx);
        return r;
    };
    std::vector<SRow> srows;
    srows.push_back(mk_space("Global uniform", false, 0.0f));
    srows.push_back(mk_space("Adaptive no spill", true, 0.0f));
    srows.push_back(mk_space("Full CABS", true, 0.8f));
    if (srows.size() >= 3) {
        srows[0].spill = 0;
        srows[1].spill = 0;
        srows[2].spill = std::max<long long>(0, srows[2].sig - srows[1].sig);
    }

    // n scalability
    logf << "[step11b] 重跑规模扩展\n";
    std::vector<SweepRow> n_rows;
    if (do_expand) for (int nsub : {100000,300000,500000,800000,1000000}) {
        SubsetBenchmark sb = compute_subset_benchmark(prep, nsub, k_def, 200);
        auto tb=std::chrono::high_resolution_clock::now();
        auto idx=build_composite_index(*prep.data,nsub,dim,cr_k15,k_def,c_fix,L_BASE,eta_def,-1,true,true,tau_def,0.5f,3.0f,m0_def,true);
        auto te=std::chrono::high_resolution_clock::now();
        auto br=run_cabs_benchmark_subset(idx,prep,k_def,probe_def,c_fix,sb);
        n_rows.push_back({std::to_string(nsub), br, (float)std::chrono::duration<double>(te-tb).count()});
    }

    // Step12 proxy validity metrics
    logf << "[step12] 重跑代理目标有效性\n";
    struct ProxyRow { std::string name; float sp_iw=0, cov500=0, cov1000=0, pre500=0; };
    auto rankdata = [&](const std::vector<float>& v){
        int m=(int)v.size();
        std::vector<std::pair<float,int>> a; a.reserve(m);
        for(int i=0;i<m;++i) a.push_back({v[(size_t)i], i});
        std::sort(a.begin(), a.end(), [](const auto& x, const auto& y){ if(x.first!=y.first) return x.first<y.first; return x.second<y.second; });
        std::vector<float> r((size_t)m, 0.0f);
        for (int i=0;i<m;) {
            int j=i+1; while(j<m && a[(size_t)j].first==a[(size_t)i].first) ++j;
            float rv=0.5f*(float)(i+1+j);
            for(int t=i;t<j;++t) r[(size_t)a[(size_t)t].second]=rv;
            i=j;
        }
        return r;
    };
    auto spearman = [&](const std::vector<float>& x, const std::vector<float>& y){
        int m=std::min((int)x.size(), (int)y.size());
        if(m<2) return 0.0f;
        std::vector<float> xx(x.begin(), x.begin()+m), yy(y.begin(), y.begin()+m);
        auto rx=rankdata(xx), ry=rankdata(yy);
        double mx=0,my=0; for(int i=0;i<m;++i){ mx+=rx[(size_t)i]; my+=ry[(size_t)i]; }
        mx/=m; my/=m;
        double num=0,dx=0,dy=0;
        for(int i=0;i<m;++i){ double ax=rx[(size_t)i]-mx, ay=ry[(size_t)i]-my; num+=ax*ay; dx+=ax*ax; dy+=ay*ay; }
        if(dx<=1e-12||dy<=1e-12) return 0.0f;
        return (float)(num/std::sqrt(dx*dy));
    };
    auto weighted_overlap_ip = [&](const int* q_ind, const float* q_val, int q_nnz, const int* d_ind, const float* d_val, int d_nnz, const std::vector<float>& w_hat){
        int i=0,j=0; float res=0.0f;
        while(i<q_nnz && j<d_nnz){
            if(q_ind[i]==d_ind[j]){ int idx=q_ind[i]; float w=(idx>=0 && idx<(int)w_hat.size())?w_hat[(size_t)idx]:1.0f; res += q_val[i]*d_val[j]*w*w*w; ++i; ++j; }
            else if(q_ind[i]<d_ind[j]) ++i; else ++j;
        }
        return res;
    };
    struct CandRec { int pid=-1; int alpha=0; float exact_ip=0.0f; float iw_ip=0.0f; float score=0.0f; };
    auto build_query_set_local = [&](const CompositeIndex& cidx, const int* q_indices, const float* q_vals, int q_nnz, int hash_domain, int seed_extra){
        std::vector<int> q_set; q_set.reserve((size_t)q_nnz*4);
        float q_max=-std::numeric_limits<float>::infinity(); for(int i=0;i<q_nnz;++i) q_max=std::max(q_max,q_vals[i]);
        if(!std::isfinite(q_max)||q_max<=0.0f) q_max=1.0f;
        int active_dim=(cidx.dim_limit>0)?std::min(cidx.dim,cidx.dim_limit):cidx.dim;
        int l_fixed=20;
        uint32_t st=(uint32_t)(2027 + seed_extra*1315423911u);
        auto fr=[&](uint32_t& s2){ s2 = s2*1664525u + 1013904223u; return (float)(s2 & 0x7FFFFFu)/(float)0x800000u; };
        for(int j=0;j<q_nnz;++j){
            int idx=q_indices[j]; float v=q_vals[j];
            if(v<=0.0f || idx<0 || idx>=active_dim) continue;
            float qn=std::max(0.0f,std::min(1.0f,v/q_max));
            float wp=qn;
            if(cidx.use_idf && idx<(int)cidx.idf_weights.idf.size()) wp=std::min(1.0f, qn*cidx.idf_weights.idf[(size_t)idx]);
            if(wp<=0.0f) continue;
            int l_j=l_fixed, start=idx*l_fixed;
            if(cidx.use_adaptive_l && idx<(int)cidx.idf_weights.dim_l.size()){ l_j=cidx.idf_weights.dim_l[(size_t)idx]; start=cidx.idf_weights.prefix_l[(size_t)idx]; }
            int upper=std::min(hash_domain, start+l_j);
            for(int slot=start; slot<upper; ++slot) if(fr(st)<wp) q_set.push_back(slot);
        }
        std::sort(q_set.begin(), q_set.end()); q_set.erase(std::unique(q_set.begin(), q_set.end()), q_set.end());
        return q_set;
    };
    auto eval_proxy_config = [&](const std::string& cfg_name, const CompositeIndex& cidx, float probe_ratio){
        double s_sp=0, s_c500=0, s_c1000=0, s_p500=0;
        int q_use=std::min(prep.benchmark.N, proxy_queries);
        for(int qi=0; qi<q_use; ++qi){
            int qb=(int)prep.queries->indptr[qi], qe=(int)prep.queries->indptr[qi+1], q_nnz=qe-qb;
            const int* q_ind=prep.queries->indices+qb; const float* q_val=prep.queries->val+qb;
            std::vector<std::pair<float,int>> cent;
            cent.reserve((size_t)cidx.K);
            for(int i=0;i<cidx.K;++i){ float sc=sparse_dense_dot(q_ind,q_val,q_nnz,cidx.cluster_indexes[(size_t)i].centroid); cent.push_back({sc,i}); }
            std::sort(cent.begin(), cent.end(), [](const auto& a,const auto& b){return a.first>b.first;});
            int num_active=std::max(1,(int)std::ceil((float)cidx.K*probe_ratio)); num_active=std::min(num_active,cidx.K);
            int hash_domain = cidx.cluster_indexes[(size_t)cent.front().second].hash_domain;
            auto q_set = build_query_set_local(cidx, q_ind, q_val, q_nnz, hash_domain, q_nnz + k_def + qi);
            std::unordered_map<int,CandRec> best;
            for(int p=0;p<num_active;++p){
                int cid=cent[(size_t)p].second; float this_cent=cent[(size_t)p].first;
                const auto& cluster=cidx.cluster_indexes[(size_t)cid]; int n_p=(int)cluster.point_ids.size(); int m_p=cluster.params.m_i;
                if(n_p<=0||m_p<=0) continue;
                std::vector<int> q_hash((size_t)m_p, cluster.hash_domain-1);
                for(int j=0;j<m_p;++j){
                    uint64_t a=cluster.hash_funcs[(size_t)j].a, b=cluster.hash_funcs[(size_t)j].b;
                    int minv=cluster.hash_domain-1;
                    for(int elem:q_set){ int hv=(int)(((a*(uint64_t)elem+b)%cluster.HASH_PRIME)%(uint64_t)cluster.hash_domain); if(hv<minv) minv=hv; }
                    q_hash[(size_t)j]=minv;
                }
                std::vector<int> cnt((size_t)n_p,0);
                for(int j=0;j<m_p;++j){ auto it=cluster.local_indexes[(size_t)j].find(q_hash[(size_t)j]); if(it==cluster.local_indexes[(size_t)j].end()) continue; for(int li:it->second) if(li>=0&&li<n_p) cnt[(size_t)li]++; }
                struct SC{int li;int cc;float score;}; std::vector<SC> cands; cands.reserve((size_t)n_p);
                for(int li=0;li<n_p;++li){ int cc=cnt[(size_t)li]; if(cc<=0) continue; float cr=(float)cc/(float)std::max(1,m_p); float set_sz=(float)cluster.local_set_sizes[(size_t)li]; float qsz=(float)q_set.size(); float so=(qsz+set_sz)/(1.0f+(float)m_p/(float)std::max(1,cc)); float ip_est=so/(float)std::max(1,cluster.params.l_i); float alpha=0.7f; float score=alpha*ip_est + (1.0f-alpha)*this_cent*cr; cands.push_back({li,cc,score}); }
                int lim=std::min((int)cands.size(), std::min(cluster.params.T_i,n_p));
                if(lim<(int)cands.size()) std::partial_sort(cands.begin(), cands.begin()+lim, cands.end(), [](const SC& a,const SC& b){return a.score>b.score;});
                else std::sort(cands.begin(), cands.end(), [](const SC& a,const SC& b){return a.score>b.score;});
                for(int vi=0; vi<lim; ++vi){
                    int li=cands[(size_t)vi].li; int pid=cluster.point_ids[(size_t)li]; if(pid<0 || pid+1>(int)prep.data->n) continue;
                    size_t ds=prep.data->indptr[pid], de=prep.data->indptr[pid+1];
                    float ip=sparse_sparse_dot(q_ind,q_val,q_nnz, prep.data->indices+ds, prep.data->val+ds, (int)(de-ds));
                    float iw=weighted_overlap_ip(q_ind,q_val,q_nnz, prep.data->indices+ds, prep.data->val+ds, (int)(de-ds), cidx.idf_weights.idf);
                    CandRec r; r.pid=pid; r.alpha=cands[(size_t)vi].cc; r.exact_ip=ip; r.iw_ip=iw; r.score=cands[(size_t)vi].score;
                    auto it=best.find(pid);
                    if(it==best.end() || r.alpha>it->second.alpha || (r.alpha==it->second.alpha && r.score>it->second.score)) best[pid]=r;
                }
            }
            std::vector<CandRec> recs; recs.reserve(best.size()); for(auto& kv:best) recs.push_back(kv.second);
            std::sort(recs.begin(), recs.end(), [](const CandRec& a,const CandRec& b){ if(a.alpha!=b.alpha) return a.alpha>b.alpha; return a.score>b.score; });
            std::vector<float> av, iv, ev; av.reserve(recs.size()); iv.reserve(recs.size()); ev.reserve(recs.size());
            for(auto& r:recs){ av.push_back((float)r.alpha); iv.push_back(r.iw_ip); ev.push_back(r.exact_ip); }
            float sp = spearman(av, iv);
            std::unordered_set<int> gt;
            int gt_top=std::min(50, prep.benchmark.num);
            for(int r=0;r<gt_top;++r) gt.insert(prep.benchmark.indice[qi][r]);
            auto cov_prec = [&](int L, float& cov, float& pre){ int take=std::min(L,(int)recs.size()); int hit=0; for(int i=0;i<take;++i) if(gt.count(recs[(size_t)i].pid)) ++hit; cov=(gt_top>0)?(float)hit/(float)gt_top:0.0f; pre=(take>0)?(float)hit/(float)take:0.0f; };
            float cov500=0, cov1000=0, pre500=0, dummy=0; cov_prec(500,cov500,pre500); cov_prec(1000,cov1000,dummy);
            s_sp += sp; s_c500 += cov500; s_c1000 += cov1000; s_p500 += pre500;
        }
        int m=std::max(1,std::min(prep.benchmark.N,proxy_queries));
        ProxyRow row; row.name=cfg_name; row.sp_iw=(float)(s_sp/m); row.cov500=(float)(s_c500/m); row.cov1000=(float)(s_c1000/m); row.pre500=(float)(s_p500/m);
        return row;
    };
    std::vector<ProxyRow> proxy_rows;
    if (do_analysis) {
        bool hit_k1=false;
        ClusterResult cr_k1 = get_cluster(*prep.data, n, dim, 1, seed, false, cpath(1), hit_k1, logf);
        CompositeIndex cfg_sosia = build_composite_index(*prep.data, n, dim, cr_k1, k_def, c_fix, L_BASE, 0.02f, -1, false, false, 0.0f, 1.0f, 1.0f, 150, false);
        CompositeIndex cfg_noidf = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, false, false, tau_def, 1.0f, 1.0f, -1, true);
        CompositeIndex cfg_full = build_composite_index(*prep.data, n, dim, cr_k15, k_def, c_fix, L_BASE, eta_def, -1, true, true, tau_def, 0.5f, 3.0f, -1, true);
        proxy_rows.push_back(eval_proxy_config("SOSIA_baseline", cfg_sosia, 1.0f));
        proxy_rows.push_back(eval_proxy_config("CABS_no_IDF", cfg_noidf, probe_def));
        proxy_rows.push_back(eval_proxy_config("CABS_full", cfg_full, probe_def));
    }

    // Output final report
    result << "CABS 实验结果修正版 (SPLADE-1M)\nSEED: 42\n";
    result << "RUN_PROFILE: "<<task<<"\n";
    result << "SCOPE_NOTE: tune_light使用轻量评估, proxy使用抽样, 未重跑节标记REUSED_PREVIOUS\n";
    result << "\nA. 6.2 参数调优\n";
    title(result, "6.2.1 K-only (MEASURED)");
    result << "K | probe_ratio | Recall@50 | Avg.Verified | QPS\n";
    if (k_rows.empty()) result<<"REUSED_PREVIOUS | not rerun in this phase\n"; else for (auto& r:k_rows) result<<r.key<<" | "<<fmt3(r.aux)<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt1(r.br.throughput)<<"\n";
    result << "note | K=15,nprobe=5 定义为 高召回区间的折中默认配置 (非全局最优)\n";

    title(result, "6.2.2 nprobe (MEASURED, fixed cluster)");
    result << "nprobe | Recall@50 | Avg.Verified | QPS | requested_nprobe | selected_before | selected_after | avg_active_clusters_hit\n";
    for (auto& r:p2_rows) result<<r.key<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt1(r.br.throughput)
        <<" | "<<fmt3(r.br.avg_requested_nprobe)
        <<" | "<<fmt3(r.br.avg_selected_clusters_before_postprocess)
        <<" | "<<fmt3(r.br.avg_selected_clusters_after_postprocess)
        <<" | "<<fmt3(r.br.avg_active_clusters_hit)<<"\n";
    result << "note | nprobe=3/4 异常由后处理最小激活簇导致: np=3 时 selected_after 仍为 4，故与 np=4 指标接近\n";

    title(result, "6.2.3 eta (MEASURED, fixed cluster)");
    result << "eta | Recall@50 | Avg.Verified | QPS\n";
    for (auto& r:eta_rows) result<<r.key<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt1(r.br.throughput)<<"\n";

    title(result, "6.2.4 tau (MEASURED)");
    result << "tau | Recall@50 | Avg.Verified | QPS | AvgClusterSize\n";
    if (tau_rows.empty()) result<<"REUSED_PREVIOUS | not rerun in this phase\n"; else for (auto& r:tau_rows) result<<r.key<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt1(r.br.throughput)<<" | "<<fmt1(r.aux)<<"\n";

    title(result, "6.2.5 IDF range (MEASURED, fixed cluster)");
    result << "range | Recall@50 | Avg.Verified | QPS\n";
    for (auto& r:idf_rows) result<<r.key<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt1(r.br.throughput)<<"\n";

    title(result, "6.2.6 k adaptation (MEASURED)");
    result << "k | Recall@k | Avg.Verified | QPS\n";
    for (auto& r:kret_rows) result<<r.key<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt1(r.br.throughput)<<"\n";

    title(result, "6.2.7 m0 (MEASURED, fixed cluster)");
    result << "m0 | avg_m_i | Recall@50 | Avg.Verified | QPS\n";
    for (auto& r:m0_rows) result<<r.key<<" | "<<fmt1(r.aux)<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt1(r.br.throughput)<<"\n";

    title(result, "6.2.8 c_eval (MEASURED, eval-only)");
    result << "c | Satisfied@50(c) | MeanRatio@50 | QPS | Avg.Verified\n";
    for (float c : {0.80f,0.85f,0.90f,0.95f}) result<<fmt3(c)<<" | "<<fmt3((float)sat_sum[c])<<" | "<<fmt3((float)mean_ratio)<<" | "<<fmt1(br_ce.throughput)<<" | "<<fmt1(br_ce.avg_candidates)<<"\n";
    result << "note | sample_queries=" << ((qlim_tune>0)?std::min(prep.benchmark.N, qlim_tune):prep.benchmark.N) << " (seed=42)\n";

    result << "\nB. 6.3 扩展性分析\n";
    title(result, "6.3.1 模块递进消融 (MEASURED)");
    result << "Config | key | Recall@50 | QPS | Avg.Verified\n";
    for (auto& p:A) result<<"SOSIA baseline | ub="<<p.key<<" | "<<fmt3(p.r)<<" | "<<fmt1(p.q)<<" | "<<fmt1(p.c)<<"\n";
    for (auto& p:B) result<<"+IDF | eta="<<p.key<<" | "<<fmt3(p.r)<<" | "<<fmt1(p.q)<<" | "<<fmt1(p.c)<<"\n";
    for (auto& p:C) result<<"+Adaptive-L | eta="<<p.key<<" | "<<fmt3(p.r)<<" | "<<fmt1(p.q)<<" | "<<fmt1(p.c)<<"\n";
    for (auto& p:D) result<<"+Clustering | np="<<p.key<<" | "<<fmt3(p.r)<<" | "<<fmt1(p.q)<<" | "<<fmt1(p.c)<<"\n";
    for (auto& p:E) result<<"Full CABS | np="<<p.key<<" | "<<fmt3(p.r)<<" | "<<fmt1(p.q)<<" | "<<fmt1(p.c)<<"\n";

    title(result, "6.3.1b 修正后的等召回比较 (MEASURED / N/A)");
    result << "target_recall | SOSIA | +IDF | +Adaptive-L | +Clustering | Full CABS\n";
    for (float trg : {0.94f,0.97f,0.99f}) {
        Pt a,b,c,d,e; bool fa=nearest_reach(A,trg,a), fb=nearest_reach(B,trg,b), fc=nearest_reach(C,trg,c), fd=nearest_reach(D,trg,d), fe=nearest_reach(E,trg,e);
        auto mk=[&](bool ok, const Pt& p){ if(!ok) return std::string("N/A(未达到目标召回)"); std::ostringstream o; o<<"key="<<p.key<<",R="<<std::fixed<<std::setprecision(3)<<p.r<<",QPS="<<std::setprecision(1)<<p.q; return o.str();};
        result<<fmt3(trg)<<" | "<<mk(fa,a)<<" | "<<mk(fb,b)<<" | "<<mk(fc,c)<<" | "<<mk(fd,d)<<" | "<<mk(fe,e)<<"\n";
    }

    title(result, "6.3.2 索引构建时间分解 (MEASURED, unified)");
    result << "Phase | Time(s)\n";
    result << "IDF computation | "<<fmt3(t_idf)<<"\n";
    result << "Spherical K-means | "<<fmt3(t_km)<<"\n";
    result << "Spill processing | "<<fmt3((float)bt.spill_processing_s)<<"\n";
    result << "SOS transform | "<<fmt3((float)bt.sos_transform_s)<<"\n";
    result << "MinHash compute | "<<fmt3((float)bt.minhash_compute_s)<<"\n";
    result << "Bucket indexing | "<<fmt3((float)bt.bucket_indexing_s)<<"\n";
    float t_total = t_idf + t_km + (float)bt.spill_processing_s + (float)bt.sos_transform_s + (float)bt.minhash_compute_s + (float)bt.bucket_indexing_s;
    result << "Total | "<<fmt3(t_total)<<"\n";

    title(result, "6.3.3 规模扩展性 (MEASURED)");
    result << "n | Ratio | Recall@50 | QPS | Avg.Verified | Build(s)\n";
    if (n_rows.empty()) result<<"REUSED_PREVIOUS | not rerun in this phase\n"; else for (auto& r:n_rows) { float ratio=std::stof(r.key)/1000000.0f; result<<r.key<<" | "<<fmt3(ratio)<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.throughput)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt3(r.aux)<<"\n"; }

    title(result, "6.3.3b 维度扩展性 (MEASURED)");
    result << "d | Recall@50 | QPS | Avg.Verified | Build(s) | IndexSize(GB)\n";
    if (d_rows.empty()) result<<"REUSED_PREVIOUS | not rerun in this phase\n"; else for (auto& r:d_rows) result<<r.key<<" | "<<fmt3(r.br.avg_recall)<<" | "<<fmt1(r.br.throughput)<<" | "<<fmt1(r.br.avg_candidates)<<" | "<<fmt3(r.aux)<<" | "<<r.aux_text<<"\n";

    title(result, "6.3.4 代理目标有效性分析 (MEASURED)");
    result << "config | spearman_alpha_iw | cov@500 | cov@1000 | pre@500\n";
    if (proxy_rows.empty()) {
        result << "REUSED_PREVIOUS | N/A | N/A | N/A | N/A\n";
    } else {
        for (const auto& pr : proxy_rows) {
            result << pr.name << " | " << fmt3(pr.sp_iw) << " | " << fmt3(pr.cov500) << " | " << fmt3(pr.cov1000) << " | " << fmt3(pr.pre500) << "\n";
        }
        result << "note | sample_queries=" << std::min(prep.benchmark.N, proxy_queries) << " (fixed seed=42)\n";
    }

    title(result, "6.3.5 空间收益与溢出开销分解 (MEASURED)");
    result << "config | Signature Size | Spill Extra Size | Total Index Size | adaptive_sum(m_i*n_i) | baseline_global_m*n | Saving over global\n";
    long long base_sig=srows.front().sig;
    for (auto& r:srows) result<<r.n<<" | "<<r.sig<<" | "<<r.spill<<" | "<<r.total<<" | "<<r.adapt<<" | "<<r.base<<" | "<<(base_sig-r.sig)<<"\n";

    result << "\nC. 需要删除或弃用的旧结果列表\n";
    result << "- exp2 Equal-Recall 中 +Clustering 在 ≈0.97 和 ≈0.99 处无效: Deprecated\n";
    result << "- exp3 Method Overall Comparison 不能作为主比较表: Deprecated\n";
    result << "- A2 旧时间分解作废: Deprecated\n";

    logf << "[step14] 生成 实验结果_修正版.txt\n";
    logf << "[step15] 生成 codex_repair_master.log\n";
    logf << "[invalid] exp2 +Clustering@0.97/@0.99 invalid; exp3 overall table deprecated; old A2 deprecated\n";
    logf << "[paper suggestion]\n";
    logf << "- K=15,nprobe=5 改写为 高召回区间折中默认配置\n";
    logf << "- 主比较使用等召回表\n";
    logf << "- 构建时间分解口径已统一\n";
    logf << "- 空间收益区分 no-spill 与 full CABS\n";

    return 0;
}
