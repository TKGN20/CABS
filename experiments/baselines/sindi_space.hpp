#pragma once

static nlohmann::json sindi_space_inventory(const vsag::Index& index,const CSR& docs){
    auto native=nlohmann::json::parse(index.GetStats());
    require(native.at("schema")==1 && native.at("method")=="SINDI","Unexpected native space schema");
    auto components=native.at("components_bytes");
    auto counts=native.at("counts");
    auto abi=native.at("abi_bytes");
    counts["nnz"]=docs.nnz;
    counts["original_offset_capacity"]=docs.offsets.capacity();
    counts["original_coordinate_capacity"]=docs.coordinates.capacity();
    counts["original_value_capacity"]=docs.values.capacity();
    counts["adapter_row_view_capacity"]=docs.rows.capacity();
    abi["uint64"]=sizeof(uint64_t);
    abi["pointer"]=sizeof(void*);
    abi["vector_SparseVector"]=sizeof(docs.rows);
    components["adapter_row_view_container_header"]=sizeof(docs.rows);
    components["adapter_row_view_headers"]=uint64_t(docs.rows.capacity())*sizeof(vsag::SparseVector);
    components["original_capacity_slack"]=uint64_t(docs.offsets.capacity()-docs.n-1)*sizeof(uint64_t)
        +uint64_t(docs.coordinates.capacity()-docs.nnz)*sizeof(uint32_t)
        +uint64_t(docs.values.capacity()-docs.nnz)*sizeof(float);
    uint64_t structure=0;
    for(const auto& item:components.items())structure+=item.value().get<uint64_t>();
    uint64_t original=24+8*(docs.n+1)+8*docs.nnz;
    return {{"schema",1},{"method","SINDI"},{"status","REPORTED_NOT_COMMON_VALIDATED"},
        {"components_bytes",components},{"counts",counts},{"abi_bytes",abi},
        {"native_reordering",native.at("native_reordering")},
        {"native_payload_bytes",native.at("native_payload_bytes")},
        {"original_vector_bytes",original},{"index_structure_bytes",structure},
        {"index_plus_vectors_bytes",original+structure},
        {"policy","Logical original CSR bytes counted once; native forward deep copy and adapter row views are additional retained storage; allocated capacities and object headers included"},
        {"excluded","Query data/scratch, released construction temporaries, loader wrappers, shared library/code, allocator bookkeeping and allocation overhead"},
        {"measurement_boundary","Read-only traversal after all timed queries; serialization and process RSS are separate quantities"}};
}
