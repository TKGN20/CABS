#include "cabs.hpp"
#include "final_equivalence.hpp"
#include "cabs/reporting.hpp"
#include <iomanip>
#include <sstream>
using namespace cabs_index;

static OwnedRow owned(std::initializer_list<std::pair<int,float>> entries) {
    OwnedRow x;for(auto p:entries){x.ix.push_back(p.first);x.values.push_back(p.second);}return x;
}
static OwnedRow copy_row(Row x,double factor=1) {
    OwnedRow v;v.ix.assign(x.indices,x.indices+x.size);v.values.assign(x.values,x.values+x.size);
    for(float& value:v.values)value*=factor;
    return v;
}
static void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error("TEST FAILED: "+message);}
static void selftest() {
    QueryWorkspace scoring;
    for(auto q:{owned({}),owned({{0,0.f},{3,1.f}}),owned({{1,std::numeric_limits<float>::denorm_min()},{5,std::numeric_limits<float>::max()}})}) {
        scoring.prepare_query(q.row(),8);
        for(auto x:{owned({}),owned({{0,1.f},{2,3.f},{3,4.f},{7,5.f}}),owned({{1,std::numeric_limits<float>::denorm_min()},{5,std::numeric_limits<float>::max()}})})
            check(scoring.exact_dense_dot(x.row())==dot(q.row(),x.row()),"dense scoring preserves original double accumulation");
    }
    Expansion a;a.allocate({5,4,3,2,1,1},5,3);
    check(a.lengths==std::vector<uint32_t>({2,2,3,3,4,4}),"Fig.1 largest-remainder allocation");
    for(auto rule:{"idf","uniform","inverse"}){
        for(int ell:{1,2,20}){a.allocate({5,0,3,2,1,1},5,ell,rule);check(a.offset.back()==uint32_t(6*ell),"equal capacity");check(*std::min_element(a.lengths.begin(),a.lengths.end())>=1,"one slot minimum");}
    }
    check(Index::hash_count(.5,.1)==4 && Index::hash_count(.6,.1)==3 && Index::hash_count(.7,.1)==2,"Fig.3 hash counts");
    check(Index::hash_count(1,.1)==1 && signature({},123)==null_slot,"null and floor-one cases");
    check(Index::hash_count(.01,.05)==299,"small similarity floors must not cap m_i");
    std::set<uint64_t> seeds;for(int c=0;c<10;++c)for(int t=0;t<10;++t)seeds.insert(derive(123,derive(999,c),t));
    check(seeds.size()==100,"different cluster/table seed domains");
    Pool pool(3);std::vector<uint32_t> cards{8,6,12,8,10,18};
    pool.update({{0,3},{1,2},{2,1},{3,1}},4,5,cards);
    std::set<Id> first;for(auto p:pool.ordered)first.insert(p.second);
    check(first==std::set<Id>({0,1,2}),"first global pool");
    pool.update({{2,2},{4,2},{5,1}},3,5,cards);
    std::set<Id> second;for(auto p:pool.ordered)second.insert(p.second);
    check(second==std::set<Id>({2,4,5}),"Fig.4 global pool across clusters");
    Collection data(6);
    for(int i=0;i<18;++i){auto x=owned({{i%2,float(.7+i*.01)},{2+i%4,float(.3+i*.015)}});data.append(std::move(x));}
    std::vector<OwnedRow> queries{owned({{0,1},{2,.7}}),owned({{1,1},{3,.9}})};
    Calibration cal;for(size_t i=0;i<queries.size();++i){cal.query_ids.push_back(i);cal.queries.push_back(queries[i].row());cal.targets.push_back(exact(data,queries[i].row(),2));}
    Options o;o.ell=6;o.kmax=2;o.iterations=30;o.max_index_gib=1;o.delta_h=.5;o.threads=2;
    Index idx(data,o);idx.rebuild(cal);
    auto admitted=[&](size_t B){
        bool success=true;
        for(size_t z=0;z<idx.calibration.queries.size();++z){Pool p(B);std::set<Id> seen,touched;
            for(int c:idx.partition.order(idx.calibration.queries[z])){auto count=idx.probe(c,idx.calibration.sketches[z]);for(auto kv:count)touched.insert(kv.first);
                p.update(count,idx.partition.clusters[c].m,idx.calibration.sketches[z].size(),idx.cardinal);for(auto kv:p.ordered)seen.insert(kv.second);}
            for(auto target:idx.calibration.targets[z])if(touched.count(target.id) && jaccard(idx.calibration.sketches[z],idx.sketches.sets[target.id])>0)success &= seen.count(target.id)>0;
        }return success;
    };

    idx.build_tables(derive(0,0x54454d504f524152ULL,mix(idx.epoch)^mix(idx.partition.clusters.size())));
    check(admitted(idx.B),"calibration B admits all touched positive targets");
    if(idx.B>size_t(o.kmax))check(!admitted(idx.B-1),"calibration chooses minimum B");
    idx.build_tables(derive(0,0x46494e414cULL,idx.epoch));
    for(auto& c:idx.partition.clusters)for(Id id:c.ids)
        check(idx.partition.primary[id]>=0,"primary assignment");
    for(auto& q:queries)for(size_t c=0;c<idx.tables.size();++c)for(Id id:idx.partition.clusters[c].ids)
        check(dot(q.row(),data.row(id))<=idx.partition.upper(q.row(),c),"safe angular bound");
    QueryStats before;auto result=idx.search(queries[0].row(),0,2,before);
    check(result.size()==2 && before.pool_peak<=idx.B,"search result size and pool limit");
    idx.release_sketch_cache();check(idx.sketches.bytes()==0,"released sketch cache has zero retained payload");
    QueryStats released;auto released_result=idx.search(queries[0].row(),0,2,released);same_hits(result,released_result);same_stats(before,released);
    check_query(idx,queries[0].row(),0,2,true);
    bool stale_rebuild_rejected=false;try{idx.build_tables(123);}catch(const std::runtime_error&){stale_rebuild_rejected=true;}check(stale_rebuild_rejected,"partial hash reconstruction requires a full sketch cache");
    bool stale_floor_rejected=false;try{idx.configure_floors(idx.partition,.5);}catch(const std::runtime_error&){stale_floor_rejected=true;}check(stale_floor_rejected,"floor calibration requires a full sketch cache");
    bool stale_pool_rejected=false;try{idx.calibrate_pool();}catch(const std::runtime_error&){stale_pool_rejected=true;}check(stale_pool_rejected,"pool calibration requires a full sketch cache");
    auto saved_norm=idx.partition.clusters[0].norm_bound,saved_cover=idx.partition.clusters[0].cover;
    idx.erase(0);check(idx.partition.clusters[0].norm_bound==saved_norm && idx.partition.clusters[0].cover==saved_cover,"deletion cannot shrink bounds");
    Id in=idx.insert(owned({{0,.5},{2,.25}}));check(!data.records[in].delta,"in-range insertion");
    Id delta=idx.insert(owned({{0,50},{2,50}}));check(data.records[delta].delta,"out-of-range insertion");
    QueryStats s;auto out=idx.search(queries[0].row(),0,2,s);check(s.delta==1 && out[0].id==delta,"delta verified before stopping");
    Id updated=idx.update(in,owned({{0,.4},{2,.2}}));check(updated>in && !data.records[in].active,"fresh update identifier");
    idx.erase(delta);QueryStats deleted;out=idx.search(queries[0].row(),0,2,deleted);for(auto h:out)check(h.id!=delta && h.id!=in && h.id!=0,"tombstone filtering");
    idx.refresh(cal);for(auto& cluster:idx.partition.clusters)for(Id id:cluster.ids)check(data.records[id].active,"refresh removes bucket tombstones");
    OwnedRow zero=owned({{0,0}});QueryStats empty;out=idx.search(zero.row(),999,2,empty);check(empty.empty && empty.verified==data.active,"empty sketch exact fallback");

    for(size_t i=0;i<queries.size();++i){QueryStats s1,s2;auto r1=idx.search(queries[i].row(),i,2,s1,true),r2=idx.search(queries[i].row(),i,2,s2,false);
        check(r1.size()==r2.size(),"stop/no-stop size");for(size_t j=0;j<r1.size();++j)check(r1[j].id==r2[j].id,"stopping preserves reached-pool top-k in toy case");}
    std::mt19937_64 test_rng(713);
    for(int t=0;t<100;++t){OwnedRow q;for(int j=0;j<6;++j){q.ix.push_back(j);q.values.push_back((test_rng()%1000)/1000.0f);}
        for(size_t c=0;c<idx.tables.size();++c)for(Id id:idx.partition.clusters[c].ids)check(dot(q.row(),data.row(id))<=idx.partition.upper(q.row(),c),"random-query angular bound");
        QueryStats s1,s2;auto r1=idx.search(q.row(),t,2,s1,true),r2=idx.search(q.row(),t,2,s2,false);
        for(size_t j=0;j<r1.size();++j)check(r1[j].id==r2[j].id,"random-query stop/no-stop equality");
    }
    Calibration zero_cal;zero_cal.query_ids={999};zero_cal.queries={zero.row()};zero_cal.targets={exact(data,zero.row(),2)};
    Index no_positive(data,o);no_positive.rebuild(zero_cal);
    check(no_positive.tables.size()==1 && no_positive.partition.clusters[0].m==1 && no_positive.B==2,"no positive calibration targets fallback");
    std::cout<<"All semantic self-tests passed\n";
}
struct Args {
    std::unordered_map<std::string,std::string> values;
    explicit Args(int argc,char** argv){
        const std::set<std::string> known={"--data","--queries","--out","--ell","--delta-h","--kmax","--iterations","--threads","--max-index-gib","--seed","--allocation","--limit","--calibration-queries","--eval-queries","--k","--repeats","--ks","--diagnostic-queries","--dynamic-operations","--ground-truth","--benchmark-exact","--equivalence-queries","--warmup-queries","--keep-sketch-cache"};
        for(int i=2;i<argc;++i){std::string key=argv[i];if(!known.count(key) || i+1>=argc)throw std::runtime_error("Unknown option or missing value: "+key);values[key]=argv[++i];}
    }
    std::string get(const std::string& key,const std::string& fallback)const{auto it=values.find(key);return it==values.end()?fallback:it->second;}
    int integer(const std::string& key,int fallback)const{return std::stoi(get(key,std::to_string(fallback)));}
    double real(const std::string& key,double fallback)const{return std::stod(get(key,std::to_string(fallback)));}
};
static void write_candidates(const Index& idx,const std::string& path) {
    std::ofstream f(path);f<<std::setprecision(17)<<"K,min_positive_jaccard,min_m,max_m,memberships,conservative_build_gib,calibration_cost_s\n";
    for(auto l:idx.candidate_log)f<<l.K<<','<<l.floor<<','<<l.min_m<<','<<l.max_m<<','<<l.memberships<<','<<l.estimated_gib<<','<<l.seconds<<'\n';
}
static int experiment(const std::string& command,const Args& args) {
    double load_start=seconds();
    CSR docs(args.get("--data","datasets/SPLADE/base_1M.csr")),queries(args.get("--queries","datasets/SPLADE/queries.dev.csr"));
    double load_seconds=seconds()-load_start;
    if(docs.d!=queries.d)throw std::runtime_error("Query/data dimensions differ");
    Options options;options.ell=args.integer("--ell",24);options.delta_h=args.real("--delta-h",.2);
    options.kmax=args.integer("--kmax",50);options.iterations=args.integer("--iterations",15);
    options.threads=args.integer("--threads",4);options.max_index_gib=args.real("--max-index-gib",7);
    options.seed=std::stoull(args.get("--seed","20261007"));options.allocation=args.get("--allocation","idf");
    if(options.allocation!="idf" && options.allocation!="uniform" && options.allocation!="inverse")throw std::runtime_error("Unknown allocation rule");
    if(options.threads<1 || options.iterations<1 || options.kmax<1 || options.ell<1 || !(options.delta_h>0 && options.delta_h<1))throw std::runtime_error("Invalid parameters");
    omp_set_num_threads(options.threads);
    double df_start=seconds();
    Collection collection(docs,std::stoull(args.get("--limit","0")));
    double df_seconds=seconds()-df_start;
    std::cerr<<"validated n="<<collection.active<<" dim="<<docs.d<<" nnz="<<docs.nnz<<"\n";
    if(command=="inspect") {
        Expansion e;e.allocate(collection.df,collection.active,options.ell,options.allocation);double maximum=0;
        for(Id id:collection.active_ids()){Row x=collection.row(id);for(size_t p=0;p<x.size;++p)maximum=std::max(maximum,double(x.values[p]));}
        std::cout<<"n="<<collection.active<<" d="<<docs.d<<" full_csr_nnz="<<docs.nnz<<" query_n="<<queries.n<<" scale="<<maximum<<" slots="<<e.offset.back()<<" l_min="<<*std::min_element(e.lengths.begin(),e.lengths.end())<<" l_max="<<*std::max_element(e.lengths.begin(),e.lengths.end())<<'\n';return 0;
    }
    int nc=args.integer("--calibration-queries",4),ne=args.integer("--eval-queries",100),k=args.integer("--k",50),repeats=args.integer("--repeats",3);
    if(nc<1 || ne<1 || uint64_t(nc)+ne>queries.n || k<1 || k>options.kmax || options.kmax>int(collection.active) || repeats<1)throw std::runtime_error("Invalid query split/k/repetitions");
    std::vector<Id> query_ids(queries.n);std::iota(query_ids.begin(),query_ids.end(),0);
    std::mt19937_64 rng(derive(options.seed,0x53504c4954ULL));std::shuffle(query_ids.begin(),query_ids.end(),rng);query_ids.resize(nc+ne);
    std::vector<std::vector<Hit>> truth(query_ids.size());double gt_start=seconds(),calibration_gt_seconds=0;
    std::string cached_gt=args.get("--ground-truth","");
    if(cached_gt.empty()) {
        double calibration_start=seconds();
        #pragma omp parallel for schedule(dynamic,1)
        for(int i=0;i<nc;++i)truth[i]=exact(collection,queries.row(query_ids[i]),options.kmax);
        calibration_gt_seconds=seconds()-calibration_start;
        #pragma omp parallel for schedule(dynamic,1)
        for(size_t i=nc;i<query_ids.size();++i)truth[i]=exact(collection,queries.row(query_ids[i]),options.kmax);
    } else {
        std::ifstream input(cached_gt);if(!input)throw std::runtime_error("Cannot load ground truth");
        std::string line;std::getline(input,line);
        if(line!="query_id,rank,identifier,score")throw std::runtime_error("Bad ground truth header");
        std::unordered_map<Id,size_t> position;for(size_t z=0;z<query_ids.size();++z)position[query_ids[z]]=z;
        while(std::getline(input,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream fields(line);
            Id qid,id;size_t rank;double score;
            if(!(fields>>qid>>rank>>id>>score) || id>=collection.records.size() || !std::isfinite(score))throw std::runtime_error("Invalid ground truth entry");
            if(!position.count(qid))continue;
            auto& target=truth[position.at(qid)];
            if(rank!=target.size()+1 || rank>size_t(options.kmax))throw std::runtime_error("Bad ground truth rank");
            double expected=dot(queries.row(qid),collection.row(id));
            if(std::abs(score-expected)>1e-9*std::max(1.0,std::abs(expected)))throw std::runtime_error("Ground truth score mismatch");
            if(!target.empty() && !better(target.back(),{id,score}))throw std::runtime_error("Ground truth sorting/duplicate error");
            target.push_back({id,score});
        }
        for(const auto& target:truth)if(target.size()!=size_t(options.kmax))throw std::runtime_error("Incomplete ground truth cache");
    }
    double ground_truth_seconds=seconds()-gt_start;std::cerr<<"exact_ground_truth_s="<<ground_truth_seconds<<"\n";
    std::string outdir=args.get("--out","results/current");
    if(access(outdir.c_str(),W_OK)!=0)throw std::runtime_error("Output directory must exist and be writable; use scripts/reproduce.sh");
    std::ofstream split(outdir+"/query_split.csv");split<<"role,query_id\n";
    std::ofstream gt(outdir+"/ground_truth.csv");gt<<std::setprecision(17)<<"query_id,rank,identifier,score\n";
    for(size_t z=0;z<query_ids.size();++z){split<<(z<size_t(nc)?"calibration":"evaluation")<<','<<query_ids[z]<<'\n';for(size_t p=0;p<truth[z].size();++p)gt<<query_ids[z]<<','<<p+1<<','<<truth[z][p].id<<','<<truth[z][p].score<<'\n';}
    split.flush();gt.flush();check(bool(split) && bool(gt),"GT/split output failure");
    Calibration cal;for(int i=0;i<nc;++i){cal.query_ids.push_back(query_ids[i]);cal.queries.push_back(queries.row(query_ids[i]));cal.targets.push_back(truth[i]);}
    Index index(collection,options);
    try{index.rebuild(cal,command=="preflight");}catch(...){write_candidates(index,outdir+"/candidate_configurations.csv");throw;}
    write_candidates(index,outdir+"/candidate_configurations.csv");
    if(command=="preflight")return 0;
    int keep_cache=args.integer("--keep-sketch-cache",0);if(keep_cache!=0 && keep_cache!=1)throw std::runtime_error("keep-sketch-cache must be 0 or 1");
    if(command=="run" && !keep_cache){
        auto cache_before=retained_index_bytes(index);uint64_t bytes_before=0;for(auto kv:cache_before)bytes_before+=kv.second;
        uint64_t buckets_before=index.table_bytes();
        double release_start=seconds();index.release_sketch_cache();double release_seconds=seconds()-release_start;
        index.stage_seconds["sketch_cache_release"]=release_seconds;index.build_seconds+=release_seconds;
        auto cache_after=retained_index_bytes(index);uint64_t bytes_after=0;for(auto kv:cache_after)bytes_after+=kv.second;
        std::ofstream receipt(outdir+"/sketch_cache_release.json");receipt<<std::setprecision(17)<<"{\"before_IS_bytes\":"<<bytes_before+docs.bytes<<",\"after_IS_bytes\":"<<bytes_after+docs.bytes<<",\"released_sketch_payload_bytes\":"<<cache_before.at("cached_sketches")<<",\"retained_sketch_payload_bytes\":"<<index.sketches.bytes()<<",\"bucket_payload_before\":"<<buckets_before<<",\"bucket_payload_after\":"<<index.table_bytes()<<",\"cardinality_count\":"<<index.cardinal.size()<<",\"K\":"<<index.tables.size()<<",\"B\":"<<index.B<<",\"release_seconds\":"<<release_seconds<<",\"release_included_in_IT\":true}\n";
    }
    std::ofstream equivalence(outdir+"/equivalence.csv");equivalence<<"query_id,exact_score_ID_order_equal,proxy_C_equal,per_cluster_pool_C_gamma_equal,routing_stopping_equal,all_semantic_counters_equal\n";
    for(int i=0;i<std::min(ne,args.integer("--equivalence-queries",100));++i){Id qid=query_ids[nc+i];check_query(index,queries.row(qid),qid,k,true);equivalence<<qid<<",1,1,1,1,1\n";equivalence.flush();}
    std::cerr<<"UNTIMED_EQUIVALENCE_PASS queries="<<std::min(ne,args.integer("--equivalence-queries",100))<<" original_vs_final full_Pool_C_proxies_routes_stops\n";
    std::ofstream records(outdir+"/queries.csv"),results(outdir+"/retrieved.csv");
    records<<std::setprecision(17)<<"k,repeat,query_id,recall,seconds,verified,touched,matched_entries,probed,pool_peak,delta,empty,fallback,bucket_accesses,admission_checks,admitted_unique\n";
    results<<std::setprecision(17)<<"k,query_id,rank,identifier,score\n";
    std::vector<int> ks;std::stringstream ks_input(args.get("--ks",std::to_string(k)));std::string token;
    while(std::getline(ks_input,token,',')){int value=std::stoi(token);if(value<1 || value>options.kmax)throw std::runtime_error("ks outside configured range");ks.push_back(value);}
    if(std::find(ks.begin(),ks.end(),k)==ks.end())ks.push_back(k);
    std::sort(ks.begin(),ks.end());ks.erase(std::unique(ks.begin(),ks.end()),ks.end());
    std::ofstream overview(outdir+"/metrics.csv"),linear(outdir+"/exact_baseline.csv");
    overview<<std::setprecision(17)<<"method,k,queries,repeats,recall,qps,mean_verified\n";
    linear<<std::setprecision(17)<<"k,repeat,query_id,seconds\n";
    if(args.integer("--benchmark-exact",1))for(int current_k:ks){
        for(int i=0;i<std::min(ne,5);++i)exact(collection,queries.row(query_ids[nc+i]),current_k);
        double elapsed=0;
        for(int r=0;r<repeats;++r)for(int i=0;i<ne;++i){double start=seconds();exact(collection,queries.row(query_ids[nc+i]),current_k);double time=seconds()-start;
            elapsed+=time;linear<<current_k<<','<<r<<','<<query_ids[nc+i]<<','<<time<<'\n';}
        overview<<"ExactScan,"<<current_k<<','<<ne<<','<<repeats<<",1,"<<ne*repeats/elapsed<<','<<collection.active<<'\n';
    }

    double total=0,recall_total=0,verification_total=0;
    for(int current_k:ks){
    for(int i=0;i<std::min(ne,args.integer("--warmup-queries",5));++i){QueryStats s;index.search(queries.row(query_ids[nc+i]),query_ids[nc+i],current_k,s);}
    double current_time=0,current_recall=0,current_verification=0;
    for(int r=0;r<repeats;++r)for(int i=0;i<ne;++i) {
        size_t z=nc+i;QueryStats s;double query_start=seconds();auto hits=index.search(queries.row(query_ids[z]),query_ids[z],current_k,s);s.time=seconds()-query_start;
        std::unordered_set<Id> unique;for(size_t p=0;p<hits.size();++p){
            check(collection.records[hits[p].id].active && unique.insert(hits[p].id).second,"active unique result IDs");
            check(hits[p].score==dot(queries.row(query_ids[z]),collection.row(hits[p].id)),"original inner product");
            if(p)check(better(hits[p-1],hits[p]),"deterministic exact score ranking");
        }
        std::unordered_set<Id> ids;for(int p=0;p<current_k;++p)ids.insert(truth[z][p].id);
        size_t matches=0;for(auto hit:hits)matches+=ids.count(hit.id);double recall=double(matches)/current_k;
        current_time+=s.time;current_recall+=recall;current_verification+=s.verified;
        records<<current_k<<','<<r<<','<<query_ids[z]<<','<<recall<<','<<s.time<<','<<s.verified<<','<<s.touched<<','<<s.entries<<','<<s.probed<<','<<s.pool_peak<<','<<s.delta<<','<<s.empty<<','<<s.fallback<<','<<s.bucket_accesses<<','<<(s.empty?0:thread_workspace().admission_checks)<<','<<(s.empty?0:thread_workspace().admitted_unique)<<'\n';
        if(r==0)for(size_t p=0;p<hits.size();++p)results<<current_k<<','<<query_ids[z]<<','<<p+1<<','<<hits[p].id<<','<<hits[p].score<<'\n';
        if((i+1)%100==0 || i+1==ne){records.flush();results.flush();std::cerr<<"QUERY_PROGRESS k="<<current_k<<" repeat="<<r<<" completed="<<i+1<<"/"<<ne<<"\n";}
    }
    if(current_k==k){total=current_time;recall_total=current_recall;verification_total=current_verification;}
    overview<<"CABS,"<<current_k<<','<<ne<<','<<repeats<<','<<current_recall/(ne*repeats)<<','<<ne*repeats/current_time<<','<<current_verification/(ne*repeats)<<'\n';
    }

    std::ofstream diagnostics(outdir+"/candidate_diagnostics.csv");diagnostics<<std::setprecision(17)<<"query_id,identifier,max_proxy,exact_score,verified,topk_target\n";
    for(int i=0;i<std::min(ne,args.integer("--diagnostic-queries",10));++i){size_t z=nc+i;QueryStats s;QueryDiagnostics d;
        index.search(queries.row(query_ids[z]),query_ids[z],k,s,true,&d);std::unordered_set<Id> targets;for(int p=0;p<k;++p)targets.insert(truth[z][p].id);
        for(auto kv:d.proxies)diagnostics<<query_ids[z]<<','<<kv.first<<','<<kv.second<<','<<dot(queries.row(query_ids[z]),collection.row(kv.first))<<','<<d.verified.count(kv.first)<<','<<targets.count(kv.first)<<'\n';
    }
    std::ofstream automatic(outdir+"/automatic_configuration.json");
    automatic<<std::setprecision(17)<<"{\n\"K\":"<<index.tables.size()<<",\n\"B\":"<<index.B<<",\n\"stage_seconds\":{";
    bool comma=false;for(auto kv:index.stage_seconds){if(comma)automatic<<',';automatic<<'"'<<kv.first<<"\":"<<kv.second;comma=true;}
    automatic<<",\"df_statistics\":"<<df_seconds<<",\"dataset_loading_validation\":"<<load_seconds<<"},\n\"clusters\":[";
    std::vector<size_t> primary_sizes(index.tables.size());for(int c:index.partition.primary)if(c>=0)++primary_sizes[c];
    for(size_t c=0;c<index.tables.size();++c){if(c)automatic<<',';auto& cl=index.partition.clusters[c];
        automatic<<"{\"id\":"<<c<<",\"primary_size\":"<<primary_sizes[c]<<",\"expanded_size\":"<<cl.ids.size()<<",\"m\":"<<cl.m<<",\"floor\":"<<cl.floor<<",\"construction_radius\":"<<cl.radius<<",\"norm_bound\":"<<cl.norm_bound<<'}';}
    automatic<<"]}\n";
    uint64_t doc_nnz=0;double doc_min=INFINITY,doc_max=0,query_min=INFINITY,query_max=0;
    for(Id id:collection.active_ids()){Row x=collection.row(id);doc_nnz+=x.size;for(size_t p=0;p<x.size;++p){doc_min=std::min(doc_min,double(x.values[p]));doc_max=std::max(doc_max,double(x.values[p]));}}
    for(uint64_t p=0;p<queries.nnz;++p){query_min=std::min(query_min,double(queries.val[p]));query_max=std::max(query_max,double(queries.val[p]));}
    std::ofstream statistics(outdir+"/dataset_statistics.json");
    statistics<<std::setprecision(17)<<"{\"n\":"<<collection.active<<",\"q\":"<<queries.n<<",\"d\":"<<docs.d<<",\"avg_doc_nnz\":"<<double(doc_nnz)/collection.active<<",\"avg_query_nnz\":"<<double(queries.nnz)/queries.n<<",\"doc_value_min\":"<<doc_min<<",\"doc_value_max\":"<<doc_max<<",\"query_value_min\":"<<query_min<<",\"query_value_max\":"<<query_max<<"}\n";
    std::ofstream f(outdir+"/summary.json");double count=ne*repeats;
    auto memory=retained_index_bytes(index);uint64_t index_bytes=0;for(const auto& entry:memory)index_bytes+=entry.second;
    std::ofstream space(outdir+"/index_space.json");space<<"{\"definition\":\"retained capacity-based structure payload; excludes allocator/node overhead, shared original CSR and query scratch\",\"components_bytes\":{";
    bool memory_comma=false;for(const auto& entry:memory){if(memory_comma)space<<',';space<<'"'<<entry.first<<"\":"<<entry.second;memory_comma=true;}
    space<<"},\"index_structure_bytes\":"<<index_bytes<<",\"original_vector_bytes\":"<<docs.bytes<<",\"index_plus_vectors_bytes\":"<<index_bytes+docs.bytes<<"}\n";
    const char* dataset=collection.active==1000000 && docs.n==1000000?"SPLADE-1M":collection.active==100000 && docs.n==100000?"SPLADE-0.1M":"CUSTOM";
    f<<std::setprecision(17)<<"{\n\"status\":\"measured\",\n\"dataset\":\""<<dataset<<"\",\n\"n\":"<<collection.active<<",\n\"dimension\":"<<docs.d<<",\n\"ell\":"<<options.ell<<",\n\"delta_h\":"<<options.delta_h<<",\n\"seed\":"<<options.seed<<",\n\"K\":"<<index.tables.size()<<",\n\"B\":"<<index.B<<",\n\"k\":"<<k<<",\n\"calibration_queries\":"<<nc<<",\n\"evaluation_queries\":"<<ne<<",\n\"repeats\":"<<repeats<<",\n\"recall\":"<<recall_total/count<<",\n\"qps\":"<<count/total<<",\n\"mean_verified\":"<<verification_total/count<<",\n\"build_seconds_including_calibration\":"<<index.build_seconds<<",\n\"construction_seconds\":"<<load_seconds+df_seconds+calibration_gt_seconds+index.build_seconds<<",\n\"calibration_ground_truth_seconds\":"<<calibration_gt_seconds<<",\n\"final_index_seconds\":"<<index.final_seconds<<",\n\"ground_truth_seconds_excluded\":"<<ground_truth_seconds-calibration_gt_seconds<<",\n\"bucket_payload_bytes\":"<<index.table_bytes()<<",\n\"cached_sketch_bytes\":"<<index.sketches.bytes()<<",\n\"index_structure_bytes\":"<<index_bytes<<",\n\"original_vector_bytes\":"<<docs.bytes<<",\n\"hash_model\":\"keyed_pseudorandom_rank_not_exact_minwise\",\n\"query_execution\":\"sequential\"\n}\n";
    std::cout<<"MEASURED recall="<<recall_total/count<<" QPS="<<count/total<<" mean_verified="<<verification_total/count<<"\n";
    if(command=="dynamic") {
        std::ofstream dynamics(outdir+"/dynamic.csv");dynamics<<"operation,elapsed_seconds,active,query_id,recall,verified,delta\n";
        auto checkpoint=[&](const std::string& operation,double elapsed) {
            for(int i=0;i<std::min(ne,10);++i){Id qid=query_ids[nc+i];Row q=queries.row(qid);auto target=exact(collection,q,k);QueryStats s;auto hits=index.search(q,qid,k,s);std::unordered_set<Id> wanted;for(auto h:target)wanted.insert(h.id);size_t match=0;for(auto h:hits)match+=wanted.count(h.id);
                dynamics<<operation<<','<<elapsed<<','<<collection.active<<','<<qid<<','<<double(match)/k<<','<<s.verified<<','<<s.delta<<'\n';}
        };
        size_t operations=args.integer("--dynamic-operations",100);if(!operations)throw std::runtime_error("Dynamic operations must be positive");
        std::vector<Id> inserted;double t=seconds();for(size_t i=0;i<operations;++i)inserted.push_back(index.insert(copy_row(collection.row(i),.9)));checkpoint("insert_in_range",seconds()-t);
        t=seconds();Id delta=index.insert(copy_row(collection.row(0),1000));checkpoint("insert_out_of_range",seconds()-t);
        t=seconds();Id updated=index.update(inserted[0],copy_row(collection.row(1),.8));checkpoint("update_fresh_id",seconds()-t);
        t=seconds();index.erase(delta);for(size_t i=1;i<inserted.size();++i)index.erase(inserted[i]);index.erase(updated);checkpoint("delete_main_and_delta",seconds()-t);
        t=seconds();index.refresh(cal);checkpoint("refresh",seconds()-t);
    }
    return 0;
}
int main(int argc,char** argv) {
    try {
        if(argc<2){std::cerr<<"Usage: cabs selftest|inspect|preflight|run|dynamic [--key value]\n";return 2;}
        std::string command=argv[1];if(command=="selftest"){omp_set_num_threads(2);selftest();return 0;}
        if(command!="inspect" && command!="preflight" && command!="run" && command!="dynamic")throw std::runtime_error("Unknown command");
        return experiment(command,Args(argc,argv));
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
