#include "cabs/search/epoch_index.hpp"
#include "reference_order_workspace.hpp"
#include <iomanip>
using namespace cabs_index;
struct State {std::vector<int> order;int cluster;double gamma,upper;bool stop;std::vector<std::pair<double,Id>> pool;std::vector<Id> verified;};
static auto tracing(std::vector<State>& out){return [&out](const std::vector<int>& order,size_t,int c,double gamma,double upper,bool stop,const auto& workspace){State s{order,c,gamma,upper,stop,{},{}};s.pool.assign(workspace.pool.begin(),workspace.pool.end());s.verified=workspace.verified;std::sort(s.pool.begin(),s.pool.end());std::sort(s.verified.begin(),s.verified.end());out.push_back(std::move(s));};}
static void same_states(const std::vector<State>& a,const std::vector<State>& b){if(a.size()!=b.size())throw std::runtime_error("trace length mismatch");for(size_t i=0;i<a.size();++i){auto& x=a[i];auto& y=b[i];if(x.order!=y.order || x.cluster!=y.cluster || x.gamma!=y.gamma || x.upper!=y.upper || x.stop!=y.stop || x.pool!=y.pool || x.verified!=y.verified)throw std::runtime_error("per-cluster state mismatch");}}
static void equal(const std::vector<Hit>& a,const std::vector<Hit>& b,const QueryStats& x,const QueryStats& y){
    if(a.size()!=b.size())throw std::runtime_error("size mismatch");
    for(size_t p=0;p<a.size();++p)if(a[p].id!=b[p].id || a[p].score!=b[p].score)throw std::runtime_error("ranked hit mismatch");
    if(x.verified!=y.verified || x.touched!=y.touched || x.entries!=y.entries || x.bucket_accesses!=y.bucket_accesses || x.probed!=y.probed || x.pool_peak!=y.pool_peak || x.delta!=y.delta || x.empty!=y.empty || x.fallback!=y.fallback)throw std::runtime_error("counter mismatch");
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    omp_set_num_threads(4);CSR docs(argv[1]),queries(argv[2]);Collection data(docs);
    Options options;options.ell=24;options.delta_h=.2;options.kmax=100;options.iterations=10;options.threads=4;options.max_index_gib=9;options.seed=20261007;
    std::vector<Id> ids(queries.n);std::iota(ids.begin(),ids.end(),0);std::mt19937_64 rng(derive(options.seed,0x53504c4954ULL));std::shuffle(ids.begin(),ids.end(),rng);
    Calibration cal;for(size_t z=0;z<4;++z){cal.query_ids.push_back(ids[z]);cal.queries.push_back(queries.row(ids[z]));cal.targets.push_back(exact(data,cal.queries.back(),100));}
    Index idx(data,options);idx.rebuild(cal);constexpr int count=200,repeats=4;
    for(int i=0;i<count;++i){Id qid=ids[4+i];Row q=queries.row(qid);QueryStats a,b;QueryDiagnostics da,db;
        std::vector<State> ta,tb;
        auto old=frozen_order::workspace_search<false,true>(idx,q,qid,50,a,true,&da,tracing(ta));auto now=workspace_search<false,true>(idx,q,qid,50,b,true,&db,tracing(tb));equal(old,now,a,b);same_states(ta,tb);
        if(da.proxies!=db.proxies || da.verified!=db.verified)throw std::runtime_error("complete candidate state mismatch");
    }
    std::ofstream record(argv[3]);record<<std::setprecision(17)<<"repeat,query_id,before_seconds,after_seconds\n";
    double before=0,after=0;uint64_t checksum=0;
    for(int r=0;r<repeats;++r)for(int i=0;i<count;++i){Id qid=ids[4+i];Row q=queries.row(qid);QueryStats a,b;std::vector<Hit> old,now;double ta=0,tb=0;
        auto first=[&](){double start=seconds();old=frozen_order::workspace_search<false,true>(idx,q,qid,50,a);ta=seconds()-start;};
        auto second=[&](){double start=seconds();now=idx.search(q,qid,50,b);tb=seconds()-start;};
        if((r+i)%2){first();second();}else{second();first();}equal(old,now,a,b);before+=ta;after+=tb;checksum+=old[0].id+now[0].id;
        record<<r<<','<<qid<<','<<ta<<','<<tb<<'\n';
    }
    record.close();std::cout<<std::setprecision(17)<<"{\"documents\":"<<docs.n<<",\"K\":"<<idx.tables.size()<<",\"B\":"<<idx.B<<",\"queries\":"<<count<<",\"repeats\":"<<repeats<<",\"before_seconds\":"<<before<<",\"after_seconds\":"<<after<<",\"before_QPS\":"<<count*repeats/before<<",\"after_QPS\":"<<count*repeats/after<<",\"speedup\":"<<before/after<<",\"checksum\":"<<checksum<<",\"ranked_hits_scores_counters_complete_candidates_equal\":true}\n";
}
