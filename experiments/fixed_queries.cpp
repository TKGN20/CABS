#include "cabs.hpp"
#include "cabs/reporting.hpp"
#include "cabs_space.hpp"
#include <iomanip>
using namespace cabs_index;

static std::vector<Id> ids(const std::string& path,uint64_t count) {
    std::ifstream f(path);if(!f)throw std::runtime_error("Cannot open query IDs");
    std::vector<Id> out;std::set<Id> seen;std::string line;
    while(std::getline(f,line)) {
        size_t pos=0;auto id=std::stoull(line,&pos);
        if(pos!=line.size() || id>=count || !seen.insert(Id(id)).second)throw std::runtime_error("Invalid/duplicate query ID");
        out.push_back(Id(id));
    }
    if(out.empty())throw std::runtime_error("Empty query IDs");
    return out;
}

int main(int argc,char** argv) {
    try {
        if(argc!=14)throw std::runtime_error("Expected data queries calibration_ids measured_ids out index_seed kmax k repeats ell delta threads iterations");
        double build_start=seconds(),start=build_start;CSR docs(argv[1]),queries(argv[2]);
        if(docs.d!=queries.d)throw std::runtime_error("Dimension mismatch");
        Collection data(docs);double loading=seconds()-start;
        auto calibration=ids(argv[3],queries.n),measured=ids(argv[4],queries.n);
        std::set<Id> reserved(calibration.begin(),calibration.end());
        for(auto id:measured)if(reserved.count(id))throw std::runtime_error("Calibration leakage");
        Options o;o.seed=std::stoull(argv[6]);o.kmax=std::stoi(argv[7]);int k=std::stoi(argv[8]),repeats=std::stoi(argv[9]);
        o.ell=std::stoi(argv[10]);o.delta_h=std::stod(argv[11]);o.threads=std::stoi(argv[12]);o.iterations=std::stoi(argv[13]);o.max_index_gib=9;
        if(k<1 || k>o.kmax || o.kmax>int(data.active) || repeats<1 || o.ell<1 || o.threads<1 || o.iterations<1 || !(o.delta_h>0 && o.delta_h<1))throw std::runtime_error("Invalid configuration");
        omp_set_num_threads(o.threads);Calibration cal;start=seconds();
        for(auto id:calibration){cal.query_ids.push_back(id);cal.queries.push_back(queries.row(id));cal.targets.push_back(exact(data,queries.row(id),o.kmax));}
        double calibration_gt=seconds()-start;
        Index index(data,o);index.rebuild(cal);uint64_t cache_before=index.sketches.bytes();start=seconds();index.release_sketch_cache();cal=Calibration{};double release=seconds()-start;
        double it=seconds()-build_start,legacy_it=loading+calibration_gt+index.build_seconds+release;
        std::string out=argv[5];std::ofstream gt(out+"/ground_truth.csv"),hitsfile(out+"/retrieved.csv"),times(out+"/queries.csv");
        if(!gt || !hitsfile || !times)throw std::runtime_error("Cannot open output files");
        gt<<std::setprecision(17)<<"query_id,rank,identifier,score\n";hitsfile<<std::setprecision(17)<<"query_id,rank,identifier,score\n";
        times<<std::setprecision(17)<<"repeat,query_id,recall,seconds,verified,probed\n";
        std::vector<std::vector<Hit>> truth;
        for(auto id:measured){truth.push_back(exact(data,queries.row(id),k));for(size_t j=0;j<truth.back().size();++j)gt<<id<<','<<j+1<<','<<truth.back()[j].id<<','<<truth.back()[j].score<<'\n';}
        for(size_t j=0;j<std::min<size_t>(5,measured.size());++j){QueryStats stats;index.search(queries.row(measured[j]),measured[j],k,stats);}
        double elapsed=0,recall=0;
        for(int repeat=0;repeat<repeats;++repeat)for(size_t j=0;j<measured.size();++j){
            Id id=measured[j];QueryStats stats;start=seconds();auto result=index.search(queries.row(id),id,k,stats);double time=seconds()-start;
            std::set<Id> wanted,unique;for(auto h:truth[j])wanted.insert(h.id);size_t matched=0;
            for(size_t p=0;p<result.size();++p){auto h=result[p];if(!unique.insert(h.id).second || h.score!=dot(queries.row(id),data.row(h.id)) || (p && !better(result[p-1],h)))throw std::runtime_error("Invalid ranked result");matched+=wanted.count(h.id);if(!repeat)hitsfile<<id<<','<<p+1<<','<<h.id<<','<<h.score<<'\n';}
            double value=double(matched)/k;elapsed+=time;recall+=value;times<<repeat<<','<<id<<','<<value<<','<<time<<','<<stats.verified<<','<<stats.probed<<'\n';
        }
        auto inventory=cabs_space_inventory(index,docs,cal);uint64_t payload=write_cabs_space(out+"/index_space.json",inventory,docs,cache_before),legacy_payload=docs.bytes;
        for(const auto& entry:inventory.legacy)legacy_payload+=entry.second;
        std::ofstream summary(out+"/summary.json");summary<<std::setprecision(17)<<"{\"method\":\"CABS\",\"search_threads\":1,\"build_threads\":"<<o.threads<<",\"record_version\":\"fixed-query-retained-space-v2\",\"recall\":"<<recall/(repeats*measured.size())<<",\"qps\":"<<repeats*measured.size()/elapsed<<",\"IT_seconds\":"<<it<<",\"IT_legacy_stage_sum_seconds\":"<<legacy_it<<",\"IT_components_seconds\":{\"loading\":"<<loading<<",\"calibration_GT\":"<<calibration_gt<<",\"native_build\":"<<index.build_seconds<<",\"release\":"<<release<<",\"driver_setup\":"<<it-legacy_it<<"},\"IS_status\":\"REPORTED_NOT_COMMON_VALIDATED\",\"IS_bytes\":"<<payload<<",\"IS_legacy_bytes\":"<<legacy_payload<<",\"K\":"<<index.tables.size()<<",\"B\":"<<index.B<<",\"k\":"<<k<<",\"repeats\":"<<repeats<<",\"queries\":"<<measured.size()<<",\"clusters\":[";
        for(size_t i=0;i<index.partition.clusters.size();++i){if(i)summary<<',';summary<<"{\"m\":"<<index.partition.clusters[i].m<<",\"floor\":"<<index.partition.clusters[i].floor<<'}';}summary<<"]}\n";
        gt.flush();hitsfile.flush();times.flush();summary.flush();if(!gt || !hitsfile || !times || !summary)throw std::runtime_error("Output write failure");
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
