#pragma once
#include "cabs/search/working_pool.hpp"
namespace cabs_index {
struct Calibration {std::vector<Id> query_ids;std::vector<Row> queries;std::vector<std::vector<Slot>> sketches;std::vector<std::vector<Hit>> targets;};
struct CandidateLog {int K;double floor=0,seconds=0,estimated_gib=0;uint32_t min_m=0,max_m=0;uint64_t memberships=0;};
struct Options {int ell=24,kmax=50,iterations=15,threads=4;double delta_h=.2,max_index_gib=7;uint64_t seed=20261007;std::string allocation="idf";};

}
