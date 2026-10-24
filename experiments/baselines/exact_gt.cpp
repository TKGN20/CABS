#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct CSR{
    uint64_t n=0,d=0,nnz=0;std::vector<uint64_t> offsets;std::vector<uint32_t> ids;std::vector<float> values;
    explicit CSR(const char* path){
        std::ifstream f(path,std::ios::binary);require(bool(f),"Cannot open original CSR");f.read(reinterpret_cast<char*>(&n),8);f.read(reinterpret_cast<char*>(&d),8);f.read(reinterpret_cast<char*>(&nnz),8);
        require(bool(f) && n>0 && n<=std::numeric_limits<uint32_t>::max() && d>0 && d<=std::numeric_limits<int32_t>::max(),"Invalid CSR header");
        f.seekg(0,std::ios::end);auto bytes=uint64_t(f.tellg());require(bytes>=32 && n<(bytes-24)/8 && nnz<=(bytes-24)/8 && bytes==24+8*(n+1)+8*nnz,"CSR size mismatch");f.seekg(24);
        offsets.resize(n+1);ids.resize(nnz);values.resize(nnz);f.read(reinterpret_cast<char*>(offsets.data()),8*(n+1));f.read(reinterpret_cast<char*>(ids.data()),4*nnz);f.read(reinterpret_cast<char*>(values.data()),4*nnz);require(bool(f) && offsets.front()==0 && offsets.back()==nnz,"Invalid CSR pointers");
        for(uint64_t row=0;row<n;++row){require(offsets[row]<=offsets[row+1] && offsets[row+1]<=nnz,"Nonmonotone CSR pointers");for(uint64_t j=offsets[row];j<offsets[row+1];++j)require(ids[j]<d && (j==offsets[row] || ids[j-1]<ids[j]) && std::isfinite(values[j]) && values[j]>=0,"Invalid sparse coordinate/value");}
    }
};
static double seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char** argv){
    try{
        require(argc==6,"Expected base.csr queries.csr original_query_ids.txt k fresh_output_directory");
        double start=seconds();CSR docs(argv[1]),queries(argv[2]);require(docs.d==queries.d,"CSR dimensions differ");size_t parsed=0;uint64_t k=std::stoull(argv[4],&parsed);require(parsed==std::string(argv[4]).size() && k>0 && k<=docs.n,"Invalid k");
        std::ifstream f(argv[3]);require(bool(f),"Cannot open original query IDs");std::vector<uint32_t> selected;std::set<uint32_t> seen;std::string token;while(f>>token){size_t end=0;auto id=std::stoull(token,&end);require(end==token.size() && token[0]!='-' && id<queries.n && seen.insert(uint32_t(id)).second,"Invalid/duplicate original query ID");selected.push_back(uint32_t(id));}require(f.eof() && !selected.empty(),"Empty/invalid query subset");
        std::vector<uint64_t> posting_offsets(docs.d+1,0);for(auto id:docs.ids)++posting_offsets[id+1];for(size_t i=1;i<posting_offsets.size();++i)posting_offsets[i]+=posting_offsets[i-1];
        std::vector<uint64_t> write=posting_offsets;std::vector<uint32_t> posting_ids(docs.nnz);std::vector<float> posting_values(docs.nnz);
        for(uint32_t did=0;did<docs.n;++did)for(auto j=docs.offsets[did];j<docs.offsets[did+1];++j){auto position=write[docs.ids[j]]++;posting_ids[position]=did;posting_values[position]=docs.values[j];}
        std::vector<uint64_t>().swap(docs.offsets);std::vector<uint32_t>().swap(docs.ids);std::vector<float>().swap(docs.values);std::vector<uint64_t>().swap(write);
        double preparation=seconds()-start;std::vector<double> scores(docs.n,0.0);std::vector<uint64_t> stamps(docs.n,0);uint64_t epoch=0;std::string out=argv[5];
        require(!std::filesystem::exists(out+"/ground_truth.csv") && !std::filesystem::exists(out+"/summary.json"),"Refuse to overwrite ground truth or metadata");std::ofstream result(out+"/ground_truth.csv"),meta(out+"/summary.json");require(bool(result) && bool(meta),"Output directory must already exist");result<<std::setprecision(17)<<"query_id,rank,identifier,score\n";start=seconds();
        for(uint32_t qid:selected){
            ++epoch;std::vector<uint32_t> touched;
            for(auto j=queries.offsets[qid];j<queries.offsets[qid+1];++j){auto term=queries.ids[j];double qvalue=double(queries.values[j]);for(auto p=posting_offsets[term];p<posting_offsets[term+1];++p){auto did=posting_ids[p];if(stamps[did]!=epoch){stamps[did]=epoch;scores[did]=0;touched.push_back(did);}scores[did]+=qvalue*double(posting_values[p]);}}
            std::vector<uint32_t> candidates;for(auto did:touched)if(scores[did]>0)candidates.push_back(did);
            if(candidates.size()<k)for(uint32_t did=0;did<docs.n && candidates.size()<k;++did)if(stamps[did]!=epoch || scores[did]==0){scores[did]=0;stamps[did]=epoch;candidates.push_back(did);}
            auto better=[&](uint32_t a,uint32_t b){return scores[a]==scores[b]?a<b:scores[a]>scores[b];};std::partial_sort(candidates.begin(),candidates.begin()+k,candidates.end(),better);
            for(size_t rank=0;rank<k;++rank){auto did=candidates[rank];require(std::isfinite(scores[did]),"Nonfinite exact score");result<<qid<<','<<rank+1<<','<<did<<','<<scores[did]<<'\n';}
        }
        double scoring=seconds()-start;meta<<std::setprecision(17)<<"{\"scope\":\"Independent unpruned exact reference; not a retrieval benchmark\",\"documents\":"<<docs.n<<",\"queries\":"<<selected.size()<<",\"k\":"<<k<<",\"preparation_seconds\":"<<preparation<<",\"scoring_and_csv_seconds\":"<<scoring<<",\"summation\":\"Ascending coordinate binary64; float32 inputs; compiler ffp-contract=off required\",\"tie_rule\":\"descending score then ascending original document ID\"}\n";
        result.flush();meta.flush();require(bool(result) && bool(meta),"GT output write failed");return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
