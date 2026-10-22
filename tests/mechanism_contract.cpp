#include "final_equivalence.hpp"
#include <iomanip>
using namespace cabs_index;

static OwnedRow make_row(int group,int variant) {
    OwnedRow row;
    for(int j=0;j<12;++j) {
        row.ix.push_back(j);
        row.values.push_back(j/4==group?float(.55+.03*((variant+j)%10)):float(.01*((variant+j)%4)));
    }
    return row;
}

static std::set<Slot> as_set(const std::vector<Slot>& x) {return {x.begin(),x.end()};}
static double independent_jaccard(const std::vector<Slot>& a,const std::vector<Slot>& b) {
    auto x=as_set(a),y=as_set(b);size_t intersection=0;
    for(auto slot:x)intersection+=y.count(slot);
    return intersection?double(intersection)/(x.size()+y.size()-intersection):0;
}

static bool coverage(const Index& idx,size_t capacity) {
    for(size_t z=0;z<idx.calibration.queries.size();++z) {
        std::map<Id,double> observed;std::set<Id> ever_admitted;
        for(int c:idx.partition.order(idx.calibration.queries[z])) {
            auto counts=idx.probe(c,idx.calibration.sketches[z]);
            for(auto count:counts) {
                double j=double(count.second)/idx.partition.clusters[c].m;
                double score=j/(1+j)*(idx.calibration.sketches[z].size()+idx.cardinal[count.first]);
                auto it=observed.find(count.first);
                if(it==observed.end() || it->second<score)observed[count.first]=score;
            }
            std::vector<std::pair<double,Id>> ranked;
            for(auto score:observed)ranked.push_back({-score.second,score.first});
            std::sort(ranked.begin(),ranked.end());
            for(size_t p=0;p<std::min(capacity,ranked.size());++p)ever_admitted.insert(ranked[p].second);
        }
        for(auto target:idx.calibration.targets[z])
            if(observed.count(target.id) && independent_jaccard(idx.calibration.sketches[z],idx.sketches.sets[target.id])>0 && !ever_admitted.count(target.id))return false;
    }
    return true;
}

int main() {
    omp_set_num_threads(2);
    Collection data(12);
    for(int group=0;group<3;++group)for(int v=0;v<24;++v)data.append(make_row(group,v));
    for(int group=0;group<3;++group) {
        OwnedRow boundary;
        for(int j=0;j<12;++j){boundary.ix.push_back(j);boundary.values.push_back(j/4==group || j/4==(group+1)%3?.6f:.01f);}
        data.append(std::move(boundary));
    }
    Options options;options.ell=8;options.kmax=6;options.threads=2;options.iterations=30;options.delta_h=.2;options.max_index_gib=1;
    std::vector<OwnedRow> queries;
    for(int g=0;g<3;++g)queries.push_back(make_row(g,5));
    Index idx(data,options);
    idx.epoch=1;idx.expansion.seed=derive(options.seed,0x454e434f4445ULL,idx.epoch);
    for(Id id:data.active_ids())for(size_t p=0;p<data.row(id).size;++p)idx.expansion.scale=std::max(idx.expansion.scale,double(data.row(id).values[p]));
    idx.expansion.allocate(data.df,data.active,options.ell);
    idx.sketches.build(data,idx.expansion);
    for(const auto& sketch:idx.sketches.sets)idx.cardinal.push_back(sketch.size());
    idx.partition.build(data,3,183701,options.iterations);
    require(idx.partition.clusters.size()==3,"fixture must exercise multiple clusters");
    idx.calibration.query_ids={900};idx.calibration.queries={queries[0].row()};
    idx.calibration.targets={exact(data,queries[0].row(),options.kmax)};
    idx.calibration.sketches={idx.expansion.encode(queries[0].row(),900,true)};
    double global=0;
    for(auto target:idx.calibration.targets[0]) {
        double j=independent_jaccard(idx.calibration.sketches[0],idx.sketches.sets[target.id]);
        if(j>0)global=global?std::min(global,j):j;
    }
    require(global>0,"fixture needs positive calibration overlap");
    idx.configure_floors(idx.partition,global);
    size_t fallback_clusters=0,secondary_memberships=0;
    for(size_t c=0;c<idx.partition.clusters.size();++c) {
        const auto& cluster=idx.partition.clusters[c];double expected=0;
        for(auto target:idx.calibration.targets[0])
            if(idx.partition.primary[target.id]==int(c) || idx.partition.secondary[target.id]==int(c)) {
                double j=independent_jaccard(idx.calibration.sketches[0],idx.sketches.sets[target.id]);
                if(j>0)expected=expected?std::min(expected,j):j;
            }
        if(!expected){expected=global;++fallback_clusters;}
        require(cluster.floor==expected,"local floor and global fallback");
        require(std::pow(1-expected,cluster.m)<=options.delta_h+1e-14,"hash count satisfies calibrated floor");
        if(cluster.m>1)require(std::pow(1-expected,cluster.m-1)>options.delta_h,"hash count is minimal");
        for(Id id:cluster.ids) {
            bool primary=idx.partition.primary[id]==int(c);
            require(primary || idx.partition.secondary[id]==int(c),"membership must be primary or secondary");
            if(!primary) {
                ++secondary_memberships;
                require(angle(dense_dot(data.row(id),cluster.mu)/data.records[id].length)<=cluster.radius,"secondary membership uses frozen radius");
            }
        }
    }
    require(fallback_clusters>0 && secondary_memberships>0,"fixture must exercise fallback and spillover");
    uint64_t temporary=derive(0,0x54454d504f524152ULL,mix(idx.epoch)^mix(3));
    idx.build_tables(temporary);idx.B=idx.calibrate_pool();
    size_t brute=options.kmax;while(!coverage(idx,brute) && brute<data.active)++brute;
    require(coverage(idx,brute) && brute==idx.B,"B matches exhaustive prefix-admission search");
    std::set<uint64_t> old_seeds;
    for(const auto& cluster:idx.tables)for(const auto& table:cluster)old_seeds.insert(table.seed);
    idx.build_tables(derive(0,0x46494e414cULL,idx.epoch));
    size_t checked_signatures=0;
    for(size_t c=0;c<idx.tables.size();++c)for(const auto& table:idx.tables[c]) {
        require(!old_seeds.count(table.seed),"temporary and final hashes have distinct realized seeds");
        std::map<Slot,std::vector<Id>> expected;
        for(Id id:idx.partition.clusters[c].ids) {
            Slot key=signature(idx.sketches.sets[id],table.seed);
            if(key!=null_slot)expected[key].push_back(id);
            ++checked_signatures;
        }
        for(const auto& bucket:expected) {
            std::vector<Id> actual;table.visit(bucket.first,[&](Id id){actual.push_back(id);});
            require(actual==bucket.second,"built buckets match direct signatures and member order");
        }
        std::vector<Slot> keys;for(const auto& bucket:expected)keys.push_back(bucket.first);
        require(keys==table.keys,"all and only nonempty signature buckets are retained");
    }
    size_t checked_queries=0;
    for(int g=0;g<3;++g)for(int v=0;v<12;++v) {
        OwnedRow q=make_row(g,v);
        for(bool stopping:{false,true})check_query(idx,q.row(),1000+g*12+v,6,stopping);
        for(size_t c=0;c<idx.tables.size();++c)for(Id id:idx.partition.clusters[c].ids)
            require(dot(q.row(),data.row(id))<=idx.partition.upper(q.row(),c),"bound covers expanded members");
        ++checked_queries;
    }
    auto buckets=idx.table_bytes();idx.release_sketch_cache();
    require(idx.table_bytes()==buckets && idx.sketches.bytes()==0,"cache release preserves buckets");
    for(int g=0;g<3;++g)check_query(idx,queries[g].row(),2000+g,6,true);
    Id inserted=idx.insert(make_row(1,1));Id updated=idx.update(inserted,make_row(2,2));
    require(updated>inserted && !data.records[inserted].active,"updates never reactivate internal identifiers");
    idx.erase(0);
    for(int g=0;g<3;++g) {
        QueryStats stats;auto result=idx.search(queries[g].row(),3000+g,6,stats);
        for(auto hit:result)require(data.records[hit.id].active && hit.id!=inserted && hit.id!=0,"deleted and old versions cannot be returned");
        check_query(idx,queries[g].row(),3000+g,6,true);
    }
    idx.refresh(idx.calibration);
    for(const auto& cluster:idx.partition.clusters)for(Id id:cluster.ids)require(data.records[id].active,"refresh retains only active bucket memberships");
    std::cout<<"{\"status\":\"PASS\",\"fixture_clusters\":3,\"fallback_clusters\":"<<fallback_clusters
             <<",\"secondary_memberships\":"<<secondary_memberships<<",\"direct_signatures_checked\":"<<checked_signatures
             <<",\"query_reference_checks\":"<<2*checked_queries+6<<",\"exhaustive_B\":"<<brute<<"}\n";
}
