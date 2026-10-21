#pragma once
#include "cabs.hpp"
#include <memory>
using namespace cabs_index;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}

static void reference_update(Pool& p,const std::unordered_map<Id,uint32_t>& counts,size_t m,size_t size,const std::vector<uint32_t>& cardinal){
    for(auto kv:counts){double j=double(kv.second)/m,v=j/(1+j)*(size+cardinal[kv.first]);auto it=p.observed.find(kv.first);
        if(it!=p.observed.end() && it->second>=v)continue;
        if(it!=p.observed.end())p.ordered.erase({-it->second,kv.first});
        p.observed[kv.first]=v;p.ordered.insert({-v,kv.first});}
    while(p.ordered.size()>p.capacity)p.ordered.erase(std::prev(p.ordered.end()));
}
struct Step {int cluster;double gamma,suffix;bool stop;std::vector<std::pair<double,Id>> pool;std::vector<Id> C;};
struct Trace {std::vector<int> order;std::vector<Step> steps;size_t verification_checks=0;};


template<class PoolType=Pool>
static std::vector<Hit> reference_search(const Index& idx,Row q,uint64_t qid,size_t k,QueryStats& s,
    QueryDiagnostics* diagnostics=nullptr,Trace* trace=nullptr,bool incremental=false,bool stopping=true){
    if(k<1 || k>size_t(idx.opts.kmax))throw std::runtime_error("k outside configured range");
    double start=seconds();auto qs=idx.expansion.encode(q,qid,true);
    if(qs.empty()){s.empty=true;s.verified=idx.data.active;auto out=exact(idx.data,q,k);s.time=seconds()-start;return out;}
    PoolType pool(idx.B);std::unordered_set<Id> verified;std::unordered_set<Id> admission_storage;
    auto admissions=incremental?&admission_storage:nullptr;
    std::set<std::pair<double,Id>> top;
    auto verify=[&](Id id){if(!verified.insert(id).second)return;top.insert({-dot(q,idx.data.row(id)),id});if(top.size()>k)top.erase(std::prev(top.end()));};
    for(Id id:idx.delta_ids)if(idx.data.records[id].active){verify(id);++s.delta;}
    auto order=idx.partition.order(q);std::vector<double> suffix(order.size()+1,-INFINITY);
    for(size_t t=order.size();t-->0;)suffix[t]=std::max(suffix[t+1],idx.partition.upper(q,order[t]));
    if(trace)trace->order=order;
    for(size_t t=0;t<order.size();++t){
        double gamma=top.size()<k?-INFINITY:-std::prev(top.end())->first;bool stop=stopping && suffix[t]<gamma;
        if(stop){if(trace)trace->steps.push_back({order[t],gamma,suffix[t],true,{},{}});break;}
        int c=order[t];++s.probed;auto counts=idx.probe(c,qs,&s);
        if(incremental)pool.update(counts,idx.partition.clusters[c].m,qs.size(),idx.cardinal,admissions);
        else reference_update(pool,counts,idx.partition.clusters[c].m,qs.size(),idx.cardinal);
        s.pool_peak=std::max(s.pool_peak,pool.ordered.size());
        if(incremental){for(Id id:*admissions)verify(id);}else{for(auto kv:pool.ordered)verify(kv.second);}
        if(trace){trace->verification_checks+=incremental?admissions->size():pool.ordered.size();
            Step step{c,gamma,suffix[t],false,{pool.ordered.begin(),pool.ordered.end()},{verified.begin(),verified.end()}};
            std::sort(step.C.begin(),step.C.end());trace->steps.push_back(std::move(step));}
    }
    s.touched=pool.observed.size();
    if(verified.size()<k){s.fallback=true;for(Id id=0;id<idx.data.records.size();++id)if(idx.data.records[id].active)verify(id);}
    s.verified=verified.size();std::vector<Hit> out;for(auto kv:top)out.push_back({kv.second,-kv.first});s.time=seconds()-start;
    if(diagnostics){diagnostics->proxies=std::move(pool.observed);diagnostics->verified=std::move(verified);}return out;
}
static void same_hits(const std::vector<Hit>& a,const std::vector<Hit>& b){require(a.size()==b.size(),"result size mismatch");for(size_t p=0;p<a.size();++p)require(a[p].id==b[p].id && a[p].score==b[p].score,"IDs/scores/order mismatch");}
static void same_stats(const QueryStats& a,const QueryStats& b){require(a.verified==b.verified && a.touched==b.touched && a.entries==b.entries && a.bucket_accesses==b.bucket_accesses && a.probed==b.probed && a.pool_peak==b.pool_peak && a.delta==b.delta && a.empty==b.empty && a.fallback==b.fallback,"semantic counters mismatch");}
static void same_trace(const Trace& a,const Trace& b){require(a.order==b.order && a.steps.size()==b.steps.size(),"routing/stopping trace mismatch");for(size_t p=0;p<a.steps.size();++p){auto& x=a.steps[p];auto& y=b.steps[p];require(x.cluster==y.cluster && x.gamma==y.gamma && x.suffix==y.suffix && x.stop==y.stop && x.pool==y.pool && x.C==y.C,"cluster pool/C/gamma/suffix mismatch");}}
template<bool Dense=false>
static std::vector<Hit> workspace_reference(const Index& idx,Row q,Id qid,size_t k,QueryStats& stats,QueryDiagnostics* diagnostics,Trace* trace,bool stop){
    auto hook=[&](const std::vector<int>& order,size_t,int cluster,double gamma,double suffix,bool stopping,const QueryWorkspace& w){
        trace->order=order;
        Step step{cluster,gamma,suffix,stopping,{},{}};
        if(!stopping){step.pool.assign(w.pool.begin(),w.pool.end());std::sort(step.pool.begin(),step.pool.end());step.C=w.verified;std::sort(step.C.begin(),step.C.end());
            for(Id id:w.admissions)trace->verification_checks+=w.member_stamp[id]==w.generation;}
        trace->steps.push_back(std::move(step));
    };
    return workspace_search<false,Dense>(idx,q,qid,k,stats,stop,diagnostics,hook);
}
static void check_query(const Index& idx,Row q,Id qid,size_t k,bool stop,Trace* before_out=nullptr,Trace* after_out=nullptr){
    QueryStats a,b,c;QueryDiagnostics da,db,dc;Trace ta,tb;
    auto ra=reference_search<Pool>(idx,q,qid,k,a,&da,&ta,false,stop),rb=workspace_reference<true>(idx,q,qid,k,b,&db,&tb,stop),rc=idx.search(q,qid,k,c,stop,&dc);
    same_hits(ra,rb);same_hits(ra,rc);same_stats(a,b);same_stats(a,c);same_trace(ta,tb);
    require(da.proxies==db.proxies && da.proxies==dc.proxies && da.verified==db.verified && da.verified==dc.verified,"complete proxy/C mismatch");
    if(before_out)*before_out=std::move(ta);
    if(after_out)*after_out=std::move(tb);
}
