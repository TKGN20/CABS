#pragma once
#include "cabs.hpp"

namespace cabs_index {



inline std::map<std::string,uint64_t> retained_index_bytes(const Index& index) {
    std::map<std::string,uint64_t> out;
    out["index_object_headers"] = sizeof(Index);
    out["encoding_metadata"] = index.expansion.lengths.capacity()*sizeof(uint32_t)
        + index.expansion.offset.capacity()*sizeof(uint32_t);
    out["cached_sketches"] = index.sketches.bytes();
    out["cardinalities"] = index.cardinal.capacity()*sizeof(uint32_t);
    out["collection_metadata"] = sizeof(Collection)
        + index.data.records.capacity()*sizeof(Record)
        + index.data.df.capacity()*sizeof(uint64_t);
    uint64_t partition = index.partition.clusters.capacity()*sizeof(Cluster)
        + index.partition.primary.capacity()*sizeof(int)
        + index.partition.secondary.capacity()*sizeof(int);
    for(const auto& c:index.partition.clusters)
        partition += c.mu.capacity()*sizeof(double) + c.ids.capacity()*sizeof(Id);
    out["partition"] = partition;
    uint64_t tables = index.tables.capacity()*sizeof(std::vector<Table>);
    for(const auto& cluster:index.tables) {
        tables += cluster.capacity()*sizeof(Table);
        for(const auto& table:cluster) {
            if(!table.additions.empty())
                throw std::runtime_error("Static payload inventory does not support dynamic additions");
            tables += table.bytes();
        }
    }
    out["local_Minhash_indexes"] = tables;
    uint64_t calibration = index.calibration.query_ids.capacity()*sizeof(Id)
        + index.calibration.queries.capacity()*sizeof(Row)
        + index.calibration.sketches.capacity()*sizeof(std::vector<Slot>)
        + index.calibration.targets.capacity()*sizeof(std::vector<Hit>)
        + index.candidate_log.capacity()*sizeof(CandidateLog);
    for(const auto& sketch:index.calibration.sketches) calibration += sketch.capacity()*sizeof(Slot);
    for(const auto& target:index.calibration.targets) calibration += target.capacity()*sizeof(Hit);
    out["retained_calibration_buffers"] = calibration;

    auto external_string_bytes=[](const std::string& value) -> uint64_t {
        auto start=reinterpret_cast<uintptr_t>(&value);
        auto buffer=reinterpret_cast<uintptr_t>(value.data());
        return buffer>=start && buffer<start+sizeof(value)?0:value.capacity()+1;
    };
    uint64_t labels = external_string_bytes(index.opts.allocation);
    for(const auto& entry:index.stage_seconds)
        labels += sizeof(entry) + external_string_bytes(entry.first);
    out["retained_labels_payload"] = labels;
    return out;
}
}
