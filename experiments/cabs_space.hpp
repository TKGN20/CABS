#pragma once
#include "cabs/reporting.hpp"

struct CabsSpaceInventory {
    std::map<std::string,uint64_t> components,counts,abi,legacy;
    template<class T> void vector(const std::string& name,const std::vector<T>& value) {
        components[name]+=value.capacity()*sizeof(T);
        counts[name+"_size"]+=value.size();counts[name+"_capacity"]+=value.capacity();
        abi[name+"_element"]=sizeof(T);
    }
};

static CabsSpaceInventory cabs_space_inventory(const cabs_index::Index& index,
                                               const cabs_index::CSR& original,
                                               const cabs_index::Calibration& input) {
    using namespace cabs_index;
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    require(!index.sketch_cache_complete && index.sketches.sets.capacity()==0,"Release document sketch cache before static inventory");
    require(input.query_ids.capacity()==0 && input.queries.capacity()==0 && input.sketches.capacity()==0 && input.targets.capacity()==0,"Release temporary calibration input before static inventory");
    require(index.delta_ids.empty() && index.data.active==original.n && index.data.records.size()==original.n,"Static inventory requires the original active collection");
    for(size_t i=0;i<index.data.records.size();++i){const auto& record=index.data.records[i];
        require(!record.owning && record.active && !record.delta && record.owned.ix.capacity()==0 && record.owned.values.capacity()==0,"Static inventory does not support owned/dynamic rows");
        require(record.original.indices==original.ix+original.ptr[i] && record.original.values==original.val+original.ptr[i] && record.original.size==original.ptr[i+1]-original.ptr[i],"Collection rows do not borrow the original CSR");
    }
    CabsSpaceInventory out;
    out.components["index_object_headers"]=sizeof(Index);
    out.components["collection_object_headers"]=sizeof(Collection);
    out.abi["Index"]=sizeof(Index);out.abi["Collection"]=sizeof(Collection);
    out.vector("encoding_lengths",index.expansion.lengths);out.vector("encoding_offsets",index.expansion.offset);
    out.vector("cached_sketch_row_headers",index.sketches.sets);out.components["cached_sketch_slots"]=0;
    out.counts["cached_sketch_slots_size"]=0;out.counts["cached_sketch_slots_capacity"]=0;out.abi["cached_sketch_slots_element"]=sizeof(Slot);
    out.vector("cardinalities",index.cardinal);out.vector("collection_records",index.data.records);out.vector("collection_df",index.data.df);
    out.vector("partition_cluster_headers",index.partition.clusters);out.vector("partition_primary",index.partition.primary);out.vector("partition_secondary",index.partition.secondary);
    out.components["partition_centroids"]=0;out.components["partition_memberships"]=0;
    out.vector("minhash_cluster_headers",index.tables);out.components["minhash_table_headers"]=0;
    out.components["minhash_keys"]=0;out.components["minhash_offsets"]=0;out.components["minhash_postings"]=0;
    uint64_t expected_postings=0,expected_tables=0;
    require(index.partition.clusters.size()==index.tables.size(),"Cluster/table count differs");
    for(size_t c=0;c<index.partition.clusters.size();++c){
        const auto& cluster=index.partition.clusters[c];
        out.vector("partition_centroids",cluster.mu);out.vector("partition_memberships",cluster.ids);
        out.vector("minhash_table_headers",index.tables[c]);
        require(cluster.mu.size()==original.d && index.tables[c].size()==cluster.m,"Native cluster dimensions/hash count differ");
        expected_tables+=cluster.m;
        for(auto id:cluster.ids){require(id<original.n,"Invalid native member ID");if(index.cardinal[id])expected_postings+=cluster.m;}
        for(const auto& table:index.tables[c]){
            require(table.additions.empty() && table.additions.bucket_count()<=1,"Static inventory excludes retained dynamic bucket allocations");
            require(table.offsets.size()==table.keys.size()+1 && table.offsets.front()==0 && table.offsets.back()==table.ids.size(),"Invalid native table shape");
            out.vector("minhash_keys",table.keys);out.vector("minhash_offsets",table.offsets);out.vector("minhash_postings",table.ids);
        }
    }
    require(out.counts["minhash_postings_size"]==expected_postings,"Native table postings differ from encoded memberships");
    out.vector("calibration_query_ids",index.calibration.query_ids);out.vector("calibration_row_views",index.calibration.queries);
    out.vector("calibration_sketch_headers",index.calibration.sketches);out.vector("calibration_target_headers",index.calibration.targets);
    out.vector("candidate_log",index.candidate_log);
    out.components["calibration_sketch_slots"]=0;out.components["calibration_target_hits"]=0;
    for(const auto& sketch:index.calibration.sketches)out.vector("calibration_sketch_slots",sketch);
    for(const auto& target:index.calibration.targets)out.vector("calibration_target_hits",target);
    auto external=[](const std::string& value)->uint64_t{auto start=reinterpret_cast<uintptr_t>(&value),buffer=reinterpret_cast<uintptr_t>(value.data());return buffer>=start && buffer<start+sizeof(value)?0:value.capacity()+1;};
    out.components["allocation_label_external"]=external(index.opts.allocation);
    out.components["stage_labels_external"]=0;
    using StageValue=std::map<std::string,double>::value_type;
#ifdef __GLIBCXX__
    using StageNode=std::_Rb_tree_node<StageValue>;
    out.abi["stage_map_node"]=sizeof(StageNode);
#else
#error "CABS experimental full map-node accounting currently requires audited libstdc++"
#endif
    out.abi["stage_map_value"]=sizeof(StageValue);
    out.counts["stage_map_nodes"]=index.stage_seconds.size();
    out.components["stage_map_nodes"]=out.counts["stage_map_nodes"]*out.abi["stage_map_node"];
    for(const auto& entry:index.stage_seconds)out.components["stage_labels_external"]+=external(entry.first);
    out.counts["documents"]=original.n;out.counts["dimensions"]=original.d;out.counts["nnz"]=original.nnz;
    out.counts["ell"]=index.opts.ell;out.counts["expanded_slot_capacity"]=index.expansion.offset.back();
    out.counts["calibration_count"]=index.calibration.queries.size();out.counts["kmax"]=index.opts.kmax;
    out.counts["expected_table_postings"]=expected_postings;out.counts["expected_hash_tables"]=expected_tables;
    out.legacy=retained_index_bytes(index);
    uint64_t current=0,legacy=0;for(const auto& entry:out.components)current+=entry.second;for(const auto& entry:out.legacy)legacy+=entry.second;
    require(current==legacy+out.counts["stage_map_nodes"]*(out.abi["stage_map_node"]-out.abi["stage_map_value"]),"Detailed components differ beyond disclosed map-node bookkeeping");
    return out;
}

static uint64_t write_cabs_space(const std::string& path,const CabsSpaceInventory& inventory,
                                 const cabs_index::CSR& original,uint64_t cache_before) {
    uint64_t structure=0,legacy=0;for(const auto& entry:inventory.components)structure+=entry.second;for(const auto& entry:inventory.legacy)legacy+=entry.second;
    std::ofstream file(path);if(!file)throw std::runtime_error("Cannot open CABS inventory");
    auto mapping=[&](const char* name,const auto& entries){file<<'"'<<name<<"\":{";bool first=true;for(const auto& entry:entries){if(!first)file<<',';first=false;file<<'"'<<entry.first<<"\":"<<entry.second;}file<<'}';};
    file<<"{\"schema\":1,\"method\":\"CABS\",\"status\":\"REPORTED_NOT_COMMON_VALIDATED\",";
    mapping("components_bytes",inventory.components);file<<',';mapping("counts",inventory.counts);file<<',';mapping("abi_bytes",inventory.abi);file<<',';mapping("legacy_components_bytes",inventory.legacy);
    file<<",\"original_vector_bytes\":"<<original.bytes<<",\"index_structure_bytes\":"<<structure<<",\"index_plus_vectors_bytes\":"<<original.bytes+structure
        <<",\"legacy_index_structure_bytes\":"<<legacy<<",\"legacy_index_plus_vectors_bytes\":"<<original.bytes+legacy
        <<",\"document_sketch_cache_before_release_bytes\":"<<cache_before
        <<",\"document_sketch_cache_released\":true,\"temporary_calibration_input_released\":true,\"original_rows_borrowed\":true,\"static_only\":true"
        <<",\"policy\":\"Retained object headers and allocated capacities plus one mmap CSR; full libstdc++ stage-map nodes counted; original reporting aggregate preserved separately\""
        <<",\"excluded\":\"CSR loader wrapper, query data/scratch, released construction temporaries, allocator bookkeeping/allocation overhead and shared code\""
        <<",\"measurement_boundary\":\"Read-only traversal after timed queries; document sketch cache and copied construction input released before query timing\"}\n";
    file.flush();if(!file)throw std::runtime_error("CABS inventory write failed");return original.bytes+structure;
}
