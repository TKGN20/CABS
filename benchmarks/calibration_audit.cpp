#include "final_equivalence.hpp"
#include <iomanip>
using namespace cabs_index;

int main(int argc,char** argv) {
    if(argc!=5){std::cerr<<"usage: cabs_calibration_audit BASE_CSR QUERIES_CSR OUTPUT_DIRECTORY EVALUATION_QUERIES\n";return 2;}
    CSR docs(argv[1]),queries(argv[2]);Collection data(docs);
    if(docs.d!=queries.d)throw std::runtime_error("data/query dimensions differ");
    int count=std::stoi(argv[4]);
    if(count<1 || uint64_t(count)+4>queries.n || data.active<100)throw std::runtime_error("invalid audit size");
    Options options;options.ell=24;options.delta_h=.2;options.kmax=100;options.iterations=10;options.threads=4;options.max_index_gib=9;options.seed=20261007;
    omp_set_num_threads(options.threads);
    Index idx(data,options);idx.epoch=1;idx.expansion.seed=derive(options.seed,0x454e434f4445ULL,idx.epoch);
    std::vector<Id> ids(queries.n);std::iota(ids.begin(),ids.end(),0);
    std::mt19937_64 rng(derive(options.seed,0x53504c4954ULL));std::shuffle(ids.begin(),ids.end(),rng);
    for(size_t z=0;z<4;++z){idx.calibration.query_ids.push_back(ids[z]);idx.calibration.queries.push_back(queries.row(ids[z]));idx.calibration.targets.push_back(exact(data,queries.row(ids[z]),100));}
    std::vector<std::vector<Hit>> truth;
    for(int z=0;z<count;++z)truth.push_back(exact(data,queries.row(ids[4+z]),50));
    for(Id id:data.active_ids())for(size_t p=0;p<data.row(id).size;++p)idx.expansion.scale=std::max(idx.expansion.scale,double(data.row(id).values[p]));
    idx.expansion.allocate(data.df,data.active,24);idx.sketches.build(data,idx.expansion);
    for(const auto& sketch:idx.sketches.sets)idx.cardinal.push_back(sketch.size());
    for(size_t z=0;z<4;++z)idx.calibration.sketches.push_back(idx.expansion.encode(idx.calibration.queries[z],ids[z],true));
    double global=0;std::set<Id> distinct;
    for(size_t z=0;z<4;++z)for(auto hit:idx.calibration.targets[z]) {
        double j=jaccard(idx.calibration.sketches[z],idx.sketches.sets[hit.id]);
        if(j>0){global=global?std::min(global,j):j;distinct.insert(hit.id);}
    }
    std::vector<int> candidates{1};
    if(!distinct.empty()){for(size_t k=2;k<distinct.size();k*=2)candidates.push_back(k);if(distinct.size()>1)candidates.push_back(distinct.size());}
    std::string out=argv[3];
    std::ofstream candidate(out+"/candidates_full_cost.csv"),times(out+"/full_queries.csv"),profile(out+"/profile.csv"),targets(out+"/pool_targets.csv");
    if(!candidate || !times || !profile || !targets)throw std::runtime_error("cannot open audit output");
    for(auto stream:{&candidate,&times,&profile,&targets})*stream<<std::setprecision(17);
    candidate<<"K,B,min_m,max_m,proxy_calibration_seconds,full_calibration_mean_seconds,full_evaluation_mean_seconds,evaluation_recall,mean_verified,mean_probed,mean_entries\n";
    times<<"K,split,repeat,query_id,seconds,verified,touched,probed,bucket_accesses,entries,pool_peak,recall\n";
    profile<<"K,query_id,sos,routing,minhash,lookup,scan,pool,verification,dot_only_nested,ordering,fallback,diagnostic,instrumented_search_seconds\n";
    targets<<"K,query_id,target_id,positive_overlap,touched,minimum_prefix_rank\n";
    for(int K:candidates) {
        omp_set_num_threads(options.threads);
        std::cerr<<"audit_K="<<K<<"\n";
        idx.partition.build(data,K,derive(options.seed,0x434c5553544552ULL,mix(idx.epoch)^mix(K)),options.iterations);
        idx.configure_floors(idx.partition,global);
        idx.build_tables(derive(0,0x54454d504f524152ULL,mix(idx.epoch)^mix(K)));
        double proxy=idx.candidate_cost();idx.B=global?idx.calibrate_pool():options.kmax;
        for(size_t z=0;z<4;++z) {
            std::map<Id,double> all;std::map<Id,size_t> min_rank;
            for(int c:idx.partition.order(idx.calibration.queries[z])) {
                auto counts=idx.probe(c,idx.calibration.sketches[z]);
                for(auto kv:counts){double j=double(kv.second)/idx.partition.clusters[c].m,v=j/(1+j)*(idx.calibration.sketches[z].size()+idx.cardinal[kv.first]);auto it=all.find(kv.first);if(it==all.end() || it->second<v)all[kv.first]=v;}
                std::vector<std::pair<double,Id>> ranked;for(auto kv:all)ranked.push_back({-kv.second,kv.first});std::sort(ranked.begin(),ranked.end());
                for(size_t p=0;p<ranked.size();++p){Id id=ranked[p].second;auto it=min_rank.find(id);if(it==min_rank.end() || p+1<it->second)min_rank[id]=p+1;}
            }
            for(auto target:idx.calibration.targets[z])targets<<K<<','<<ids[z]<<','<<target.id<<','<<(jaccard(idx.calibration.sketches[z],idx.sketches.sets[target.id])>0)<<','<<all.count(target.id)<<','<<(min_rank.count(target.id)?min_rank.at(target.id):0)<<'\n';
        }
        omp_set_num_threads(1);
        for(int z=0;z<count+4;++z){QueryStats stats;idx.search(queries.row(ids[z]),ids[z],50,stats);}
        double cal_seconds=0,eval_seconds=0,recall=0,verified=0,probed=0,entries=0;
        for(int rep=0;rep<2;++rep)for(int z=0;z<count+4;++z) {
            Id qid=ids[z];QueryStats stats;double start=seconds();auto hits=idx.search(queries.row(qid),qid,50,stats);double elapsed=seconds()-start;
            double overlap=0;std::set<Id> relevant;
            const auto& reference=z<4?idx.calibration.targets[z]:truth[z-4];
            for(size_t p=0;p<std::min<size_t>(50,reference.size());++p)relevant.insert(reference[p].id);
            for(auto hit:hits)overlap+=relevant.count(hit.id)/50.0;
            if(z>=4) {
                eval_seconds+=elapsed;recall+=overlap;verified+=stats.verified;probed+=stats.probed;entries+=stats.entries;
            }else cal_seconds+=elapsed;
            times<<K<<','<<(z<4?"calibration":"evaluation")<<','<<rep<<','<<qid<<','<<elapsed<<','<<stats.verified<<','<<stats.touched<<','<<stats.probed<<','<<stats.bucket_accesses<<','<<stats.entries<<','<<stats.pool_peak<<','<<overlap<<'\n';
        }
        for(int z=0;z<count;++z) {
            Id qid=ids[4+z];QueryStats normal_stats,profile_stats;WorkspaceTimes tm;
            auto normal=idx.search(queries.row(qid),qid,50,normal_stats);
            auto instrumented=workspace_search<true,true>(idx,queries.row(qid),qid,50,profile_stats,true,nullptr,NoWorkspaceTrace{},&tm);
            same_hits(normal,instrumented);same_stats(normal_stats,profile_stats);
            profile<<K<<','<<qid<<','<<tm.sos<<','<<tm.routing<<','<<tm.minhash<<','<<tm.lookup<<','<<tm.scan<<','<<tm.pool<<','<<tm.verification<<','<<tm.dot_only<<','<<tm.ordering<<','<<tm.fallback<<','<<tm.diagnostic<<','<<profile_stats.time<<'\n';
        }
        auto log=idx.estimate();double n=2*count;
        candidate<<K<<','<<idx.B<<','<<log.min_m<<','<<log.max_m<<','<<proxy<<','<<cal_seconds/8<<','<<eval_seconds/n<<','<<recall/n<<','<<verified/n<<','<<probed/n<<','<<entries/n<<'\n';
        candidate.flush();times.flush();profile.flush();targets.flush();idx.tables.clear();
    }
    std::cout<<"audit_completed temporary_indexes_only=true profile_timing_excluded_from_full_queries=true\n";
}
