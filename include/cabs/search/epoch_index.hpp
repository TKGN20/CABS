#pragma once
#include "cabs/search/query_workspace.hpp"
namespace cabs_index {
class Index {
public:
    Collection& data;Options opts;Expansion expansion;Sketches sketches;Partition partition;
    std::vector<std::vector<Table>> tables;std::vector<uint32_t> cardinal;
    Calibration calibration;size_t B=0;std::vector<CandidateLog> candidate_log;
    std::set<Id> delta_ids;
    std::map<std::string,double> stage_seconds;
    double build_seconds=0,final_seconds=0;uint64_t epoch=0;
    Index(Collection& d,Options o):data(d),opts(std::move(o)){}
    std::unordered_map<Id,uint32_t> probe(int cluster,const std::vector<Slot>& q,QueryStats* stats=nullptr) const {
        std::unordered_map<Id,uint32_t> counts;
        for(auto& table:tables[cluster]) {
            if(stats)++stats->bucket_accesses;
            table.visit(signature(q,table.seed),[&](Id id){if(stats)++stats->entries;
                if(data.records[id].active)++counts[id];});
        }
        return counts;
    }
    void configure_floors(Partition& p,double global) const {
        for(size_t z=0;z<calibration.queries.size();++z)for(auto h:calibration.targets[z]) {
            double j=jaccard(calibration.sketches[z],sketches.sets[h.id]);if(j<=0)continue;
            for(int c:{p.primary[h.id],p.secondary[h.id]})if(c>=0) {
                auto& f=p.clusters[c].floor;f=f?std::min(f,j):j;
            }
        }
        for(auto& c:p.clusters){if(!c.floor)c.floor=global;
            c.m=hash_count(c.floor,opts.delta_h);}
    }
    static uint32_t hash_count(double floor,double delta) {
        if(!(delta>0 && delta<1))throw std::runtime_error("delta_h must lie in (0,1)");
        if(floor<=0 || floor==1)return 1;
        double m=std::ceil(std::log(delta)/std::log1p(-floor));
        if(m>UINT32_MAX || !std::isfinite(m))throw std::runtime_error("Unrepresentable hash count");
        return std::max<uint32_t>(1,m);
    }
    CandidateLog estimate() const {
        CandidateLog l;l.K=partition.clusters.size();l.min_m=UINT32_MAX;uint64_t bytes=sketches.bytes();
        for(auto& c:partition.clusters) {
            l.memberships+=c.ids.size();l.min_m=std::min(l.min_m,c.m);l.max_m=std::max(l.max_m,c.m);
            if(c.floor>0)l.floor=l.floor?std::min(l.floor,c.floor):c.floor;

            bytes+=uint64_t(c.m)*(uint64_t(c.ids.size())*4+uint64_t(std::min<size_t>(c.ids.size(),expansion.offset.back()))*16+sizeof(Table));
            bytes+=c.mu.capacity()*8+c.ids.capacity()*4;
        }
        bytes+=uint64_t(opts.threads)*expansion.offset.back()*16;
        l.estimated_gib=double(bytes)/(1ULL<<30);return l;
    }
    void build_tables(uint64_t domain) {
        auto l=estimate();
        if(l.estimated_gib>opts.max_index_gib)throw std::runtime_error("Resource guard: K="+std::to_string(l.K)+", m_max="+std::to_string(l.max_m)+", conservative build bound="+std::to_string(l.estimated_gib)+" GiB exceeds --max-index-gib; configuration NOT capped or skipped");
        tables.clear();tables.resize(partition.clusters.size());
        for(size_t c=0;c<tables.size();++c) {
            auto& cl=partition.clusters[c];tables[c].resize(cl.m);
            #pragma omp parallel for schedule(dynamic,1)
            for(uint32_t t=0;t<cl.m;++t) {
                auto& table=tables[c][t];table.seed=derive(opts.seed,derive(domain,c),t);
                table.build(cl.ids,sketches,expansion.offset.back());
            }
        }
    }
    double candidate_cost() const {
        double total=0;size_t queries=0;
        for(size_t z=0;z<calibration.queries.size();++z) {
            std::unordered_set<Id> pending;
            for(auto h:calibration.targets[z])if(jaccard(calibration.sketches[z],sketches.sets[h.id])>0)pending.insert(h.id);
            if(pending.empty())continue;

            for(int rep=0;rep<3;++rep) {
                auto left=pending;double start=seconds();auto order=partition.order(calibration.queries[z]);
                for(int c:order) {
                    auto counts=probe(c,calibration.sketches[z]);
                    for(auto it=left.begin();it!=left.end();) {
                        if(partition.primary[*it]==c || partition.secondary[*it]==c)it=left.erase(it);else ++it;
                    }
                    if(left.empty())break;
                }
                if(rep)total+=seconds()-start;
            }++queries;
        }return queries?total/(2*queries):0;
    }
    size_t calibrate_pool() const {
        size_t capacity=opts.kmax;
        for(size_t z=0;z<calibration.queries.size();++z) {
            std::unordered_set<Id> positives;for(auto h:calibration.targets[z])if(jaccard(calibration.sketches[z],sketches.sets[h.id])>0)positives.insert(h.id);
            std::unordered_map<Id,double> all;std::unordered_map<Id,size_t> min_rank;
            for(int c:partition.order(calibration.queries[z])) {
                auto counts=probe(c,calibration.sketches[z]);
                for(auto kv:counts){double j=double(kv.second)/partition.clusters[c].m,v=j/(1+j)*(calibration.sketches[z].size()+cardinal[kv.first]);
                    auto it=all.find(kv.first);if(it==all.end() || v>it->second)all[kv.first]=v;}
                std::vector<std::pair<double,Id>> ranking;ranking.reserve(all.size());
                for(auto kv:all)ranking.push_back({-kv.second,kv.first});
                std::sort(ranking.begin(),ranking.end());
                for(size_t p=0;p<ranking.size();++p)if(positives.count(ranking[p].second)) {
                    auto it=min_rank.find(ranking[p].second);if(it==min_rank.end() || p+1<it->second)min_rank[ranking[p].second]=p+1;
                }
            }
            for(Id id:positives) if(all.count(id))capacity=std::max(capacity,min_rank.at(id));
        }
        return capacity;
    }
    void rebuild(const Calibration& supplied,bool preflight=false) {
        double start=seconds();stage_seconds.clear();tables.clear();++epoch;calibration=supplied;
        expansion.seed=derive(opts.seed,0x454e434f4445ULL,epoch);expansion.scale=0;
        for(Id id:data.active_ids()){Row x=data.row(id);for(size_t p=0;p<x.size;++p)expansion.scale=std::max(expansion.scale,double(x.values[p]));}
        stage_seconds["scaling"]=seconds()-start;
        double stage=seconds();
        expansion.allocate(data.df,data.active,opts.ell,opts.allocation);
        stage_seconds["allocation"]=seconds()-stage;stage=seconds();
        sketches.build(data,expansion);cardinal.assign(data.records.size(),0);
        for(size_t i=0;i<cardinal.size();++i)cardinal[i]=sketches.sets[i].size();
        calibration.sketches.clear();for(size_t z=0;z<calibration.queries.size();++z)
            calibration.sketches.push_back(expansion.encode(calibration.queries[z],calibration.query_ids[z],true));
        stage_seconds["sketch_construction"]=seconds()-stage;
        double calibration_start=seconds();
        double global=0;std::set<Id> distinct;
        for(size_t z=0;z<calibration.queries.size();++z)for(auto h:calibration.targets[z]) {
            if(!data.records.at(h.id).active)throw std::runtime_error("Stale calibration target");
            double j=jaccard(calibration.sketches[z],sketches.sets[h.id]);if(j>0){global=global?std::min(global,j):j;distinct.insert(h.id);}
        }
        std::vector<int> candidates{1};if(!distinct.empty()) {
            for(size_t k=2;k<distinct.size();k*=2)candidates.push_back(k);
            if(distinct.size()>1)candidates.push_back(distinct.size());
        }
        std::cerr<<"encoding_done slots="<<expansion.offset.back()<<" sketch_GiB="<<double(sketches.bytes())/(1ULL<<30)<<" positive_targets="<<distinct.size()<<"\n";
        Partition best;double cost=INFINITY;candidate_log.clear();
        for(int K:candidates) {
            std::cerr<<"calibrate_K="<<K<<"\n";
            stage=seconds();
            partition.build(data,K,derive(opts.seed,0x434c5553544552ULL,mix(epoch)^mix(K)),opts.iterations);
            stage_seconds["partition_all_candidates"]+=seconds()-stage;
            stage_seconds["spherical_all_candidates"]+=partition.spherical_seconds;
            stage_seconds["spillover_all_candidates"]+=partition.spillover_seconds;
            stage=seconds();configure_floors(partition,global);
            stage_seconds["local_m_calibration_all_candidates"]+=seconds()-stage;auto log=estimate();
            candidate_log.push_back(log);
            if(!preflight) {
                stage=seconds();build_tables(derive(0,0x54454d504f524152ULL,mix(epoch)^mix(K)));
                stage_seconds["temporary_hash_construction_all_candidates"]+=seconds()-stage;
                stage=seconds();log.seconds=candidate_cost();
                stage_seconds["K_candidate_cost_measurement"]+=seconds()-stage;
            }
            candidate_log.back()=log;
            std::cerr<<"K="<<K<<" floor="<<log.floor<<" m="<<log.min_m<<":"<<log.max_m<<" build_bound_GiB="<<log.estimated_gib<<" cost_s="<<log.seconds<<"\n";
            if(!preflight && log.seconds<cost){cost=log.seconds;best=partition;}
            tables.clear();
        }
        if(preflight){build_seconds=seconds()-start;return;}
        stage_seconds["K_calibration_including_partitions_hashes"]=seconds()-calibration_start;
        partition=std::move(best);stage=seconds();
        build_tables(derive(0,0x54454d504f524152ULL,mix(epoch)^mix(partition.clusters.size())));
        stage_seconds["selected_temporary_hash_construction"]=seconds()-stage;stage=seconds();
        B=global?calibrate_pool():opts.kmax;
        stage_seconds["B_calibration"]=seconds()-stage;tables.clear();
        stage_seconds["calibration_including_partitions"]=seconds()-calibration_start;
        double final_start=seconds();
        build_tables(derive(0,0x46494e414cULL,epoch));
        final_seconds=seconds()-final_start;stage_seconds["final_Minhash_construction"]=final_seconds;build_seconds=seconds()-start;
        for(auto& r:data.records)if(r.active)r.delta=false;
        delta_ids.clear();
        std::cerr<<"final_ready K="<<tables.size()<<" B="<<B<<" bytes="<<table_bytes()<<" build_s="<<build_seconds<<"\n";
    }
    uint64_t table_bytes() const {uint64_t b=0;for(auto& c:tables)for(auto& t:c)b+=t.bytes();return b;}
    std::vector<Hit> search(Row q,uint64_t query_id,size_t k,QueryStats& stats,bool stopping=true,QueryDiagnostics* diagnostics=nullptr) const {
        return workspace_search<false,true>(*this,q,query_id,k,stats,stopping,diagnostics);
    }

    Id insert(OwnedRow row) {
        Id id=data.append(std::move(row));sketches.sets.resize(data.records.size());cardinal.resize(data.records.size());
        partition.primary.resize(data.records.size(),-1);partition.secondary.resize(data.records.size(),-1);
        Row x=data.row(id);bool out=false;for(size_t p=0;p<x.size;++p)out|=x.values[p]>expansion.scale;
        if(out){data.records[id].delta=true;delta_ids.insert(id);return id;}
        sketches.sets[id]=expansion.encode(x,id);cardinal[id]=sketches.sets[id].size();
        int a=-1,b=-1;double first=-1,second=-1;
        for(size_t c=0;c<partition.clusters.size();++c){double v=dense_dot(x,partition.clusters[c].mu)/data.records[id].length;
            if(v>first){second=first;b=a;first=v;a=c;}else if(v>second){second=v;b=c;}}
        partition.primary[id]=a;
        if(b>=0 && angle(second)<=partition.clusters[b].radius)partition.secondary[id]=b;
        for(int c:{a,partition.secondary[id]})if(c>=0) {
            auto& cl=partition.clusters[c];cl.ids.push_back(id);cl.norm_bound=std::max(cl.norm_bound,outward(data.records[id].length));
            cl.cover=std::max(cl.cover,outward(angle(dense_dot(x,cl.mu)/data.records[id].length)));
            if(!sketches.sets[id].empty())for(auto& table:tables[c])table.additions[signature(sketches.sets[id],table.seed)].push_back(id);
        }return id;
    }
    void erase(Id id){data.erase(id);delta_ids.erase(id);}
    Id update(Id id,OwnedRow row){erase(id);return insert(std::move(row));}
    void refresh(Calibration c) {
        for(size_t z=0;z<c.queries.size();++z)c.targets[z]=exact(data,c.queries[z],opts.kmax);
        rebuild(c);

        for(Id id=0;id<data.records.size();++id)if(!data.records[id].active) {
            sketches.sets[id].clear();sketches.sets[id].shrink_to_fit();
            if(data.records[id].owning){data.records[id].owned={};}
        }
    }
};

}
