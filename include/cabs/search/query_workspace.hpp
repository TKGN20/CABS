#pragma once
#include "cabs/calibration/configuration.hpp"
namespace cabs_index {
struct WorkspaceTimes {
    double sos=0,routing=0,minhash=0,lookup=0,scan=0,pool=0,verification=0,dot_only=0,ordering=0,fallback=0,diagnostic=0;
    size_t checks=0;
};
// Largest full (-proxy,ID) pair at root. Dense positions eliminate lookups.
struct IndexedPoolHeap {
    using Value=std::pair<double,Id>;
    std::vector<Value> values;std::vector<size_t> position;
    void ensure(size_t n){position.resize(n);}
    size_t size()const{return values.size();}
    void clear(){values.clear();}
    auto begin()const{return values.begin();}auto end()const{return values.end();}
    const Value& worst()const{return values.front();}
    void swap_nodes(size_t a,size_t b){std::swap(values[a],values[b]);position[values[a].second]=a;position[values[b].second]=b;}
    void up(size_t p){while(p){size_t parent=(p-1)/2;if(!(values[parent]<values[p]))break;swap_nodes(parent,p);p=parent;}}
    void down(size_t p){while(2*p+1<size()){size_t child=2*p+1;if(child+1<size() && values[child]<values[child+1])++child;if(!(values[p]<values[child]))break;swap_nodes(p,child);p=child;}}
    void insert(Value v){position[v.second]=size();values.push_back(v);up(size()-1);}
    void erase(Value v){size_t at=position[v.second];if(at>=size() || values[at]!=v)throw std::logic_error("indexed pool stale position");remove(at);}
    void remove(size_t at){if(at+1==size()){values.pop_back();return;}values[at]=values.back();values.pop_back();position[values[at].second]=at;if(at && values[(at-1)/2]<values[at])up(at);else down(at);}
    void pop_worst(){remove(0);}
};
struct QueryWorkspace {
    uint64_t generation=0,cluster_generation=0;size_t admission_checks=0,admitted_unique=0;
    std::vector<uint64_t> observed_stamp,verified_stamp,member_stamp,count_stamp;
    std::vector<double> proxy;
    std::vector<uint32_t> collisions;
    std::vector<float> query_values;std::vector<int> query_coordinates;int query_max=-1;
    std::vector<Id> counted,touched,verified,admissions;
    IndexedPoolHeap pool;std::set<std::pair<double,Id>> top;
    void begin(size_t n){
        observed_stamp.resize(n);verified_stamp.resize(n);member_stamp.resize(n);count_stamp.resize(n);
        proxy.resize(n);collisions.resize(n);pool.ensure(n);
        if(++generation==0){std::fill(observed_stamp.begin(),observed_stamp.end(),0);std::fill(verified_stamp.begin(),verified_stamp.end(),0);std::fill(member_stamp.begin(),member_stamp.end(),0);generation=1;}
        admission_checks=0;admitted_unique=0;counted.clear();touched.clear();verified.clear();admissions.clear();pool.clear();top.clear();
    }
    void prepare_query(Row q,size_t dim){
        for(int coordinate:query_coordinates)query_values[coordinate]=0;
        query_values.resize(dim,0);query_coordinates.clear();query_max=-1;
        for(size_t p=0;p<q.size;++p){query_values[q.indices[p]]=q.values[p];query_coordinates.push_back(q.indices[p]);query_max=q.indices[p];}
    }
    double exact_dense_dot(Row document)const{
        double score=0;
        for(size_t p=0;p<document.size;++p){int coordinate=document.indices[p];if(coordinate>query_max)break;
            float value=query_values[coordinate];if(value!=0)score+=double(value)*document.values[p];}
        return score;
    }
    void cluster(){counted.clear();admissions.clear();if(++cluster_generation==0){std::fill(count_stamp.begin(),count_stamp.end(),0);cluster_generation=1;}}
    void collide(Id id){if(count_stamp[id]!=cluster_generation){count_stamp[id]=cluster_generation;collisions[id]=0;counted.push_back(id);}++collisions[id];}
    void update(size_t B,size_t m,size_t query_size,const std::vector<uint32_t>& cardinal){
        for(Id id:counted){
            double j=double(collisions[id])/m,v=j/(1+j)*(query_size+cardinal[id]);
            bool seen=observed_stamp[id]==generation;
            if(seen && proxy[id]>=v)continue;
            bool member=member_stamp[id]==generation;
            if(member)pool.erase({-proxy[id],id});
            if(!seen){observed_stamp[id]=generation;touched.push_back(id);}
            proxy[id]=v;
            std::pair<double,Id> candidate{-v,id};
            if(pool.size()==B){auto worst=pool.worst();if(!(candidate<worst))continue;member_stamp[worst.second]=0;pool.pop_worst();}
            pool.insert(candidate);member_stamp[id]=generation;
            if(!member)admissions.push_back(id);
        }
    }
};
struct NoWorkspaceTrace {
    void operator()(const std::vector<int>&,size_t,int,double,double,bool,const QueryWorkspace&)const{}
};
inline QueryWorkspace& thread_workspace(){static thread_local QueryWorkspace workspace;return workspace;}
// One independent workspace per OS thread. No data/query pointers are cached.
// Query generation isolates indexes, repeated IDs, refresh and growing fresh IDs.
template<bool Profile=false,bool Dense=false,class IndexType,class Hook=NoWorkspaceTrace>
std::vector<Hit> workspace_search(const IndexType& idx,Row q,uint64_t qid,size_t k,QueryStats& s,
    bool stopping=true,QueryDiagnostics* diagnostics=nullptr,Hook hook={},WorkspaceTimes* timing=nullptr){
    if(k<1 || k>size_t(idx.opts.kmax))throw std::runtime_error("k outside configured range");
    auto tick=[](){if constexpr(Profile)return seconds();else return 0.0;};
    WorkspaceTimes discard;auto& tm=timing?*timing:discard;
    double start=seconds(),a=tick();auto qs=idx.expansion.encode(q,qid,true);if constexpr(Profile)tm.sos+=seconds()-a;
    if(qs.empty()){a=tick();s.empty=true;s.verified=idx.data.active;auto out=exact(idx.data,q,k);if constexpr(Profile)tm.fallback+=seconds()-a;s.time=seconds()-start;return out;}
    auto& w=thread_workspace();w.begin(idx.data.records.size());if constexpr(Dense)w.prepare_query(q,idx.data.dim);
    auto verify=[&](Id id){
        if(w.verified_stamp[id]==w.generation)return;
        w.verified_stamp[id]=w.generation;w.verified.push_back(id);
        double d=tick(),score;
        if constexpr(Dense)score=w.exact_dense_dot(idx.data.row(id));else score=dot(q,idx.data.row(id));if constexpr(Profile)tm.dot_only+=seconds()-d;
        std::pair<double,Id> candidate{-score,id};
        if constexpr(Dense){if(w.top.size()==k && !(candidate<*std::prev(w.top.end())))return;}
        w.top.insert(candidate);if(w.top.size()>k)w.top.erase(std::prev(w.top.end()));
    };
    a=tick();for(Id id:idx.delta_ids)if(idx.data.records[id].active){verify(id);++s.delta;}if constexpr(Profile)tm.verification+=seconds()-a;
    a=tick();auto order=idx.partition.order(q);std::vector<double> suffix(order.size()+1,-INFINITY);
    for(size_t t=order.size();t-->0;)suffix[t]=std::max(suffix[t+1],idx.partition.upper(q,order[t]));
    if constexpr(Profile)tm.routing+=seconds()-a;
    for(size_t t=0;t<order.size();++t){
        double gamma=w.top.size()<k?-INFINITY:-std::prev(w.top.end())->first;bool stop=stopping && suffix[t]<gamma;int c=order[t];
        if(stop){hook(order,t,c,gamma,suffix[t],true,w);break;}
        ++s.probed;w.cluster();
        for(const auto& table:idx.tables[c]){
            ++s.bucket_accesses;a=tick();Slot key=signature(qs,table.seed);if constexpr(Profile)tm.minhash+=seconds()-a;
            a=tick();size_t lo=0,hi=0;const std::vector<Id>* extra=nullptr;
            if(key!=null_slot){auto it=std::lower_bound(table.keys.begin(),table.keys.end(),key);
                if(it!=table.keys.end() && *it==key){size_t p=it-table.keys.begin();lo=table.offsets[p];hi=table.offsets[p+1];}
                auto it2=table.additions.find(key);if(it2!=table.additions.end())extra=&it2->second;}
            if constexpr(Profile)tm.lookup+=seconds()-a;
            a=tick();auto visit=[&](Id id){++s.entries;if(idx.data.records[id].active)w.collide(id);};
            for(size_t p=lo;p<hi;++p)visit(table.ids[p]);
            if(extra)for(Id id:*extra)visit(id);
            if constexpr(Profile)tm.scan+=seconds()-a;
        }
        a=tick();w.update(idx.B,idx.partition.clusters[c].m,qs.size(),idx.cardinal);if constexpr(Profile)tm.pool+=seconds()-a;
        s.pool_peak=std::max(s.pool_peak,w.pool.size());
        a=tick();for(Id id:w.admissions)if(w.member_stamp[id]==w.generation){++w.admission_checks;if constexpr(Profile)++tm.checks;verify(id);}if constexpr(Profile)tm.verification+=seconds()-a;
        hook(order,t,c,gamma,suffix[t],false,w);
    }
    w.admitted_unique=w.verified.size()-s.delta;
    s.touched=w.touched.size();
    if(w.verified.size()<k){s.fallback=true;a=tick();for(Id id=0;id<idx.data.records.size();++id)if(idx.data.records[id].active)verify(id);if constexpr(Profile)tm.fallback+=seconds()-a;}
    s.verified=w.verified.size();a=tick();std::vector<Hit> out;for(auto kv:w.top)out.push_back({kv.second,-kv.first});if constexpr(Profile)tm.ordering+=seconds()-a;
    if(diagnostics){a=tick();diagnostics->proxies.clear();diagnostics->verified.clear();for(Id id:w.touched)diagnostics->proxies.emplace(id,w.proxy[id]);diagnostics->verified.insert(w.verified.begin(),w.verified.end());if constexpr(Profile)tm.diagnostic+=seconds()-a;}
    // Charge tree destruction to this query; retained arrays contain no pointers.
    w.pool.clear();w.top.clear();s.time=seconds()-start;return out;
}
} // namespace cabs_index
