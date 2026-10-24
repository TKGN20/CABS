#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <omp.h>
#include <nlohmann/json.hpp>
#include "vsag/vsag.h"
#ifndef CABS_SINDI_REPAIRED_NATIVE
#define CABS_SINDI_REPAIRED_NATIVE 0
#endif
#ifndef CABS_SINDI_SPACE_INVENTORY
#define CABS_SINDI_SPACE_INVENTORY 0
#endif

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct SentinelAllocator:vsag::Allocator{
    std::map<void*,size_t> sizes;unsigned char fill=255;
    std::string Name() override{return "explicit-unwritten-result-sentinels";}
    void* Allocate(size_t size) override{void* p=std::malloc(std::max<size_t>(1,size));if(!p)throw std::bad_alloc();std::memset(p,fill,std::max<size_t>(1,size));sizes[p]=size;return p;}
    void Deallocate(void* p) override{if(p){sizes.erase(p);std::free(p);}}
    void* Reallocate(void* p,size_t size) override{if(!p)return Allocate(size);auto found=sizes.find(p);require(found!=sizes.end(),"Unknown allocation");size_t old=found->second;void* next=Allocate(size);std::memcpy(next,p,std::min(old,size));Deallocate(p);return next;}
};
struct CSR{
    uint64_t n=0,d=0,nnz=0;std::vector<uint64_t> offsets;std::vector<uint32_t> coordinates;std::vector<float> values;std::vector<vsag::SparseVector> rows;
    explicit CSR(const char* path){
        std::ifstream f(path,std::ios::binary);require(bool(f),"Cannot read original CSR");
        f.read(reinterpret_cast<char*>(&n),8);f.read(reinterpret_cast<char*>(&d),8);f.read(reinterpret_cast<char*>(&nnz),8);
        require(bool(f) && n>0 && n<=uint64_t(std::numeric_limits<int>::max()) && d>0 && d<=uint64_t(std::numeric_limits<uint32_t>::max()),"Invalid CSR header");
        f.seekg(0,std::ios::end);uint64_t bytes=uint64_t(f.tellg());require(n<(bytes-24)/8 && nnz<=(bytes-24)/8 && bytes==24+8*(n+1)+8*nnz,"CSR length mismatch");f.seekg(24);
        offsets.resize(n+1);coordinates.resize(nnz);values.resize(nnz);rows.resize(n);
        f.read(reinterpret_cast<char*>(offsets.data()),8*(n+1));f.read(reinterpret_cast<char*>(coordinates.data()),4*nnz);f.read(reinterpret_cast<char*>(values.data()),4*nnz);require(bool(f) && offsets.front()==0 && offsets.back()==nnz,"Invalid CSR offsets");
        for(size_t i=0;i<n;++i){require(offsets[i]<offsets[i+1] && offsets[i+1]<=nnz && offsets[i+1]-offsets[i]<=std::numeric_limits<uint32_t>::max(),"Invalid/empty sparse row");for(auto j=offsets[i];j<offsets[i+1];++j)require(coordinates[j]<d && (j==offsets[i] || coordinates[j-1]<coordinates[j]) && std::isfinite(values[j]) && values[j]>=0,"Invalid coordinate/value");rows[i].dim_=uint32_t(offsets[i+1]-offsets[i]);rows[i].ids_=coordinates.data()+offsets[i];rows[i].vals_=values.data()+offsets[i];}
    }
};
static double dot(const vsag::SparseVector& a,const vsag::SparseVector& b){size_t i=0,j=0;double total=0;while(i<a.dim_ && j<b.dim_){if(a.ids_[i]==b.ids_[j]){total+=double(a.vals_[i])*double(b.vals_[j]);++i;++j;}else if(a.ids_[i]<b.ids_[j])++i;else ++j;}return total;}
#include "sindi_space.hpp"
struct SearchResult{std::vector<std::pair<int64_t,double>> hits;std::vector<std::pair<int64_t,float>> native;size_t unwritten=0,duplicates=0;};
int main(int argc,char** argv){
    try{
        require(argc==12,"Expected data queries measured_ids out lambda alpha beta gamma k repeats candidate-api-v1");
        int lambda=std::stoi(argv[5]),gamma=std::stoi(argv[8]),k=std::stoi(argv[9]),repeats=std::stoi(argv[10]);
        bool native_reorder=std::string(argv[11])=="candidate-api-v1";require(native_reorder || (CABS_SINDI_REPAIRED_NATIVE && std::string(argv[11])=="candidate-no-reorder-diagnostic-v1"),"Explicit candidate API policy argument required");
        float alpha=std::stof(argv[6]),beta=std::stof(argv[7]);require(lambda>0 && k>0 && gamma>=k && repeats>0 && std::isfinite(alpha) && alpha>0 && alpha<=1 && std::isfinite(beta) && beta>0 && beta<=1,"Invalid SINDI configuration");
        omp_set_dynamic(0);omp_set_num_threads(1);double start=omp_get_wtime();CSR docs(argv[1]),queries(argv[2]);require(docs.d==queries.d && uint64_t(gamma)<=docs.n,"Dimension or gamma exceeds collection");
        uint32_t observed_max=*std::max_element(docs.coordinates.begin(),docs.coordinates.end());if(!CABS_SINDI_REPAIRED_NATIVE)for(auto coordinate:queries.coordinates)require(coordinate<=observed_max,"Native SINDI cannot safely address unseen trailing dimensions");
        std::ifstream idsfile(argv[3]);require(bool(idsfile),"Cannot read original query IDs");std::vector<int64_t> ids;std::set<int64_t> seen;int64_t id;while(idsfile>>id){require(id>=0 && uint64_t(id)<queries.n && seen.insert(id).second,"Invalid original query ID");ids.push_back(id);}require(idsfile.eof() && !ids.empty(),"Invalid/empty measured IDs");
        SentinelAllocator allocator;if(const char* pattern=std::getenv("CABS_SINDI_SENTINEL_BYTE")){int v=std::stoi(pattern);require(v==255 || v==170,"Unsupported diagnostic sentinel byte");allocator.fill=static_cast<unsigned char>(v);}int64_t unwritten_id;uint32_t unwritten_score;std::memset(&unwritten_id,allocator.fill,sizeof(unwritten_id));std::memset(&unwritten_score,allocator.fill,sizeof(unwritten_score));vsag::init();nlohmann::json build_parameters={{"dtype","float32"},{"metric_type","ip"},{"dim",docs.d},{"sindi",{{"lambda",lambda},{"alpha",alpha},{"use_reorder",native_reorder}}}};
        bool custom_allocator=!CABS_SINDI_REPAIRED_NATIVE || std::getenv("CABS_SINDI_SENTINEL_BYTE")!=nullptr;auto index=vsag::Factory::CreateIndex("sindi",build_parameters.dump(),custom_allocator?&allocator:nullptr).value();auto base=vsag::Dataset::Make();base->SparseVectors(docs.rows.data())->NumElements(docs.n)->Dim(docs.d)->Owner(false);require(index->Build(base).has_value(),"Native SINDI build failed");double build=omp_get_wtime()-start;
        std::string parameters=nlohmann::json{{"sindi",{{"num_threads",1},{"beta",beta},{"gamma",gamma}}}}.dump();
        auto search=[&](int64_t qid){
            auto query=vsag::Dataset::Make();query->SparseVectors(&queries.rows[qid])->NumElements(1)->Owner(false);auto result=index->KnnSearch(query,gamma,parameters).value();SearchResult found;std::set<int64_t> unique;
            for(int slot=0;slot<gamma;++slot){int64_t did=result->GetIds()[slot];float value=result->GetDistances()[slot];if(did==unwritten_id || did==-1){uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));require(did==-1?std::isnan(value):bits==unwritten_score,"Unwritten ID without unwritten score");++found.unwritten;continue;}require(did>=0 && uint64_t(did)<docs.n && std::isfinite(value),"Invalid native candidate output");found.native.emplace_back(did,value);if(unique.insert(did).second)found.hits.emplace_back(did,dot(docs.rows[did],queries.rows[qid]));else ++found.duplicates;}
            std::sort(found.hits.begin(),found.hits.end(),[](const auto& a,const auto& b){return a.second==b.second?a.first<b.first:a.second>b.second;});return found;
        };
        std::string out=argv[4];std::ofstream results(out+"/retrieved.csv"),times(out+"/queries.csv"),candidates(out+"/native_candidates.csv"),references(out+"/native_reference.csv"),counts(out+"/candidate_counts.csv");
        results<<std::setprecision(17)<<"query_id,rank,identifier,score\n";times<<std::setprecision(17)<<"repeat,query_id,seconds\n";candidates<<std::setprecision(17)<<"query_id,slot,identifier,native_float_score\n";references<<std::setprecision(17)<<"query_id,rank,identifier,native_float_score\n";counts<<"query_id,exposed_slots,unique_candidates,unwritten_slots,duplicate_slots\n";
        for(int64_t qid:ids){auto exposed=search(qid);auto query=vsag::Dataset::Make();query->SparseVectors(&queries.rows[qid])->NumElements(1)->Owner(false);auto native=index->KnnSearch(query,k,parameters).value();std::set<int64_t> admitted;for(auto h:exposed.hits)admitted.insert(h.first);for(int rank=0;rank<k;++rank){auto did=native->GetIds()[rank];if(did==unwritten_id || did==-1)continue;require(admitted.count(did),"Native result outside exposed gamma candidates");references<<qid<<','<<rank+1<<','<<did<<','<<native->GetDistances()[rank]<<'\n';}for(size_t slot=0;slot<exposed.native.size();++slot)candidates<<qid<<','<<slot<<','<<exposed.native[slot].first<<','<<exposed.native[slot].second<<'\n';counts<<qid<<','<<exposed.native.size()<<','<<exposed.hits.size()<<','<<exposed.unwritten<<','<<exposed.duplicates<<'\n';}
        for(int repeat=0;repeat<repeats;++repeat)for(int64_t qid:ids){start=omp_get_wtime();auto hits=search(qid);require(hits.hits.size()>=size_t(k),"SINDI underfilled unique candidate set");hits.hits.resize(k);double elapsed=omp_get_wtime()-start;times<<repeat<<','<<qid<<','<<elapsed<<'\n';if(!repeat)for(size_t rank=0;rank<hits.hits.size();++rank)results<<qid<<','<<rank+1<<','<<hits.hits[rank].first<<','<<hits.hits[rank].second<<'\n';}
        for(auto stream:{&results,&times,&candidates,&references,&counts}){stream->flush();require(bool(*stream),"Output write failed");}
        nlohmann::json summary={{"method","SINDI"},{"build_seconds_including_loading",build},{"search_threads",1},{"native_reranking",native_reorder},{"index_randomness","deterministic native construction; no stochastic seed introduced"},{"candidate_policy","native gamma pool exposed through KnnSearch(k=gamma); native gamma unchanged"},{"exact_binary64_reranking_in_timer",true},{"allocation_policy",custom_allocator?"Diagnostic initialized custom allocator; cost included":"Native allocator and explicit native result initialization"},{"sentinel_byte",allocator.fill},{"disclosed_native_correctness_repairs",bool(CABS_SINDI_REPAIRED_NATIVE)},{"IS_status","NOT_YET_VALIDATED"}};
#if CABS_SINDI_SPACE_INVENTORY
        auto inventory=sindi_space_inventory(*index,docs);
        std::ofstream space(out+"/index_space.json");space<<inventory.dump(2)<<'\n';space.flush();require(bool(space),"Space inventory write failed");
        summary["IS_status"]=inventory["status"];summary["IS_bytes"]=inventory["index_plus_vectors_bytes"];
        if(std::getenv("CABS_SINDI_SPACE_ROUNDTRIP")){
            auto before=nlohmann::json::parse(index->GetStats());
            std::vector<SearchResult> expected;for(auto qid:ids)expected.push_back(search(qid));
            {std::ofstream serialized(out+"/native_index.bin",std::ios::binary);require(index->Serialize(serialized).has_value(),"Native serialization failed");serialized.flush();require(bool(serialized),"Native serialization write failed");}
            auto original=index;
            index=vsag::Factory::CreateIndex("sindi",build_parameters.dump(),custom_allocator?&allocator:nullptr).value();
            std::ifstream serialized(out+"/native_index.bin",std::ios::binary);require(index->Deserialize(serialized).has_value() && bool(serialized),"Native deserialization failed");
            for(size_t q=0;q<ids.size();++q){auto actual=search(ids[q]);require(actual.hits==expected[q].hits && actual.native==expected[q].native && actual.unwritten==expected[q].unwritten && actual.duplicates==expected[q].duplicates,"Native roundtrip changed candidate/ranked output");}
            auto after=nlohmann::json::parse(index->GetStats());
            std::ofstream roundtrip(out+"/roundtrip.json");roundtrip<<nlohmann::json{{"before",before},{"after",after},{"ranked_and_native_outputs_equal",true},{"scope","Untimed optional serialize/reload diagnostic; serialized size is not IS"}}.dump(2)<<'\n';roundtrip.flush();require(bool(roundtrip),"Roundtrip proof write failed");index=original;
        }
#else
        require(std::getenv("CABS_SINDI_SPACE_ROUNDTRIP")==nullptr,"Roundtrip diagnostic requires registered space introspection");
#endif
        std::ofstream meta(out+"/summary.json");meta<<std::setprecision(17)<<summary.dump(2)<<'\n';meta.flush();require(bool(meta),"Summary write failed");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
