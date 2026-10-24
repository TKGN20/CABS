#pragma once
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

static std::uint64_t external_string_bytes(const std::string& text) {
    auto start=reinterpret_cast<std::uintptr_t>(&text);
    auto buffer=reinterpret_cast<std::uintptr_t>(text.data());
    return buffer>=start && buffer<start+sizeof(text)?0:text.capacity()+1;
}

static std::uint64_t write_sosia_space(const std::string& path,const Preprocess& prep,
                                     const mips2set& transformed,const Sosia& index) {
    static_assert(sizeof(std::size_t)==8 && sizeof(int)==4 && sizeof(float)==4);
    std::map<std::string,std::uint64_t> bytes,counts;
    index.spaceInventory(bytes,counts);
    auto n=std::uint64_t(prep.data->n),d=std::uint64_t(prep.data->dim),nnz=std::uint64_t(prep.data->nnz);
    bytes["encoding_object_headers"]=sizeof(transformed);
    bytes["expanded_vector_headers"]=n*sizeof(std::vector<int>);
    bytes["expanded_slot_capacity"]=0;
    counts["expanded_slots_size"]=0;counts["expanded_slots_capacity"]=0;
    counts["expanded_empty_rows"]=0;
    for(std::uint64_t i=0;i<n;++i) {
        bytes["expanded_slot_capacity"]+=transformed.transSets[i].capacity()*sizeof(int);
        counts["expanded_slots_size"]+=transformed.transSets[i].size();
        counts["expanded_slots_capacity"]+=transformed.transSets[i].capacity();
        counts["expanded_empty_rows"]+=transformed.transSets[i].empty();
    }
    bytes["hash_permutation_pointer_array"]=std::uint64_t(index.s)*sizeof(int*);
    bytes["hash_permutation_slots"]=std::uint64_t(index.s)*index.len*sizeof(int);
    bytes["data_hash_pointer_array"]=n*sizeof(int*);
    bytes["data_hash_slots"]=n*index.s*sizeof(int);
    bytes["preprocessing_object_headers"]=sizeof(prep);
    bytes["preprocessing_square_norms"]=prep.squareLen?n*sizeof(float):0;
    bytes["preprocessing_labels_payload"]=external_string_bytes(prep.dataFile)+external_string_bytes(prep.benFile);
    if(prep.benchmark.N || prep.benchmark.indice || prep.benchmark.innerproduct)
        throw std::runtime_error("SOSIA space inventory requires benchmark-free construction");
    counts["documents"]=n;counts["dimensions"]=d;counts["nnz"]=nnz;
    counts["ell"]=mips2set::l;counts["hash_functions"]=index.s;counts["hash_domain_slots"]=index.len;
    std::map<std::string,std::uint64_t> abi={{"int",sizeof(int)},{"float",sizeof(float)},
        {"pointer",sizeof(int*)},{"vector_int",sizeof(std::vector<int>)},
        {"vector_vector_int",sizeof(std::vector<std::vector<int>>)},{"Res",sizeof(Res)},
        {"Sosia",sizeof(index)},{"mips2set",sizeof(transformed)},{"Preprocess",sizeof(prep)}};
    std::uint64_t structure=0;for(const auto& entry:bytes)structure+=entry.second;
    std::uint64_t original=24+8*(n+1)+8*nnz;
    std::ofstream out(path);if(!out)throw std::runtime_error("Cannot write SOSIA space inventory");
    auto mapping=[&](const char* name,const auto& values) {
        out<<'"'<<name<<"\":{";bool first=true;
        for(const auto& entry:values){if(!first)out<<',';first=false;out<<'"'<<entry.first<<"\":"<<entry.second;}
        out<<'}';
    };
    out<<"{\"schema\":1,\"method\":\"SOSIA\",\"status\":\"REPORTED_NOT_COMMON_VALIDATED\","
       <<"\"definition\":\"Retained index object headers and allocated capacities plus one logical original CSR store\","
       <<"\"exclusions\":\"Evaluation query storage, CSR loader wrapper objects, executable/global code, allocator bookkeeping and query scratch; not RSS or serialized size\","
       <<"\"measurement_phase\":\"Read-only after all query timing; retained SOS/hash buffers are counted\","
       <<"\"original_vector_bytes\":"<<original<<",\"index_structure_bytes\":"<<structure
       <<",\"index_plus_vectors_bytes\":"<<original+structure<<',';
    mapping("components_bytes",bytes);out<<',';mapping("counts",counts);out<<',';mapping("abi_bytes",abi);out<<"}\n";
    out.flush();if(!out)throw std::runtime_error("SOSIA space inventory write failed");
    return original+structure;
}
