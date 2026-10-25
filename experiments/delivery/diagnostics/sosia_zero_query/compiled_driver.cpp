#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <omp.h>
#include "sosia.h"

int mips2set::l=10;
std::uint64_t mips2set::seed=20261007;
struct Cleanup {std::function<void()> fn;~Cleanup(){fn();}};
struct Hit {int id;double score;};
static bool order(const Hit& a,const Hit& b){return a.score==b.score?a.id<b.id:a.score>b.score;}
static double now(){return omp_get_wtime();}
static std::vector<int> read_ids(const std::string& path,size_t n){
    std::ifstream f(path);if(!f)throw std::runtime_error("Cannot open explicit IDs");
    std::vector<int> ids;std::set<int> seen;std::string line;
    while(std::getline(f,line)){size_t end=0;auto v=std::stoll(line,&end);if(end!=line.size() || v<0 || size_t(v)>=n || !seen.insert(int(v)).second)throw std::runtime_error("Invalid original query ID");ids.push_back(int(v));}
    if(ids.empty())throw std::runtime_error("Empty measured ID set");return ids;
}
static double dot(SparseData* data,int id,SparseData* queries,int qid){
    auto a=data->indptr[id],ae=data->indptr[id+1],b=queries->indptr[qid],be=queries->indptr[qid+1];double value=0;
    while(a<ae && b<be){int i=data->indices[a],j=queries->indices[b];if(i==j){value+=double(data->val[a])*double(queries->val[b]);++a;++b;}else if(i<j)++a;else ++b;}return value;
}
int main(int argc,char** argv){
    try{
        if(argc!=11)throw std::runtime_error("Expected data queries measured_ids out seed ell m budget k repeats");
        int ell=std::stoi(argv[6]),m=std::stoi(argv[7]),budget=std::stoi(argv[8]),k=std::stoi(argv[9]),repeats=std::stoi(argv[10]);
        if(ell<1 || m<1 || k<1 || budget<k || repeats<1)throw std::runtime_error("Invalid SOSIA parameters");
        omp_set_dynamic(0);omp_set_num_threads(1);mips2set::setSeed(std::stoull(argv[5]));double start=now();
        Preprocess prep(argv[1],argv[2],"",false);Cleanup data_guard{[&](){delete prep.data;delete prep.queries;}};
        if(prep.data->dim!=prep.queries->dim || k>int(prep.data->n))throw std::runtime_error("Invalid dimension or k");
        auto ids=read_ids(argv[3],prep.queries->n);mips2set transformed(prep,ell);Cleanup sketch_guard{[&](){delete[] transformed.transSets;}};
        Sosia index(prep,transformed,1,m);Cleanup hash_guard{[&](){clear_2d_array(index.hashFuns,index.s);clear_2d_array(index.hashvals,index.n);}};
        double build=now()-start;
        for(size_t i=0;i<std::min<size_t>(5,ids.size());++i){
            Query native(ids[i],.5f,k,prep,budget-k),candidate(ids[i],.5f,k,prep,budget-k);
            index.knnSearch2(native);index.candidateSearch(candidate);
            std::set<int> admitted;for(auto h:candidate.res)admitted.insert(h.id);
            if(native.cost!=candidate.cost)throw std::runtime_error("Native/candidate budget parity failure");
            for(auto h:native.res)if(!admitted.count(h.id))throw std::runtime_error("Native result outside exposed candidate set");
        }
        auto search=[&](int qid){
            std::vector<Hit> hits;
            {Query query(qid,.5f,k,prep,budget-k);index.candidateSearch(query);hits.reserve(query.res.size());for(auto native:query.res)hits.push_back({native.id,dot(prep.data,native.id,prep.queries,qid)});}
            std::sort(hits.begin(),hits.end(),order);if(hits.size()<size_t(k))throw std::runtime_error("SOSIA underfilled candidate set");hits.resize(k);return hits;
        };
        for(size_t i=0;i<std::min<size_t>(5,ids.size());++i)search(ids[i]);
        std::string out=argv[4];std::ofstream results(out+"/retrieved.csv"),times(out+"/queries.csv"),meta(out+"/summary.json");
        results<<std::setprecision(17)<<"query_id,rank,identifier,score\n";times<<std::setprecision(17)<<"repeat,query_id,seconds\n";
        for(int repeat=0;repeat<repeats;++repeat)for(int qid:ids){start=now();auto hits=search(qid);double elapsed=now()-start;times<<repeat<<','<<qid<<','<<elapsed<<'\n';if(!repeat)for(size_t rank=0;rank<hits.size();++rank)results<<qid<<','<<rank+1<<','<<hits[rank].id<<','<<hits[rank].score<<'\n';}
        meta<<std::setprecision(17)<<"{\"method\":\"SOSIA\",\"build_seconds_including_loading\":"<<build<<",\"search_threads\":1,\"original_query_ids\":true,\"exact_binary64_reranking_in_timer\":true,\"candidate_policy\":\"upstream SOSIA proxy heap and budget cap\",\"IS_status\":\"NOT_YET_VALIDATED\"}\n";
        results.flush();times.flush();meta.flush();if(!results || !times || !meta)throw std::runtime_error("Output write failure");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
