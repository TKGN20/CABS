#pragma once
#include "cabs/common.hpp"
namespace cabs_index {

struct CSR {
    int fd=-1; void* mapping=MAP_FAILED; size_t bytes=0;
    uint64_t n=0,d=0,nnz=0; const uint64_t* ptr=nullptr;
    const int32_t* ix=nullptr; const float* val=nullptr;
    CSR(const std::string& path) {
        try {
        fd=open(path.c_str(),O_RDONLY); if(fd<0) throw std::runtime_error("Cannot open CSR: "+path);
        struct stat st{}; if(fstat(fd,&st)) throw std::runtime_error("fstat failed");
        bytes=st.st_size;
        if(bytes<32) throw std::runtime_error("Truncated CSR");
        mapping=mmap(nullptr,bytes,PROT_READ,MAP_PRIVATE,fd,0);
        if(mapping==MAP_FAILED) throw std::runtime_error("mmap failed");
        const auto* h=static_cast<const uint64_t*>(mapping); n=h[0];d=h[1];nnz=h[2];
        if(n>=null_slot || d==0 || d>INT32_MAX || n>(bytes-32)/8 || nnz>(bytes-32)/8)
            throw std::runtime_error("Unsupported CSR dimensions");
        if(bytes != 24+8*(n+1)+8*nnz) throw std::runtime_error("CSR size does not match header");
        ptr=h+3; ix=reinterpret_cast<const int32_t*>(ptr+n+1);
        val=reinterpret_cast<const float*>(ix+nnz);
        if(ptr[0]!=0 || ptr[n]!=nnz) throw std::runtime_error("Bad CSR pointers");
        for(uint64_t i=0;i<n;++i) {
            if(ptr[i]>ptr[i+1] || ptr[i+1]>nnz) throw std::runtime_error("Nonmonotone CSR pointers");
            int32_t prev=-1;
            for(uint64_t j=ptr[i];j<ptr[i+1];++j) {
                if(ix[j]<=prev || uint64_t(ix[j])>=d || !std::isfinite(val[j]) || val[j]<0)
                    throw std::runtime_error("CSR requires sorted unique coordinates and finite nonnegative values");
                prev=ix[j];
            }
        }
        } catch(...) {
            if(mapping!=MAP_FAILED) munmap(mapping,bytes);
            if(fd>=0) close(fd);
            throw;
        }
    }
    CSR(const CSR&)=delete;
    ~CSR(){if(mapping!=MAP_FAILED) munmap(mapping,bytes); if(fd>=0) close(fd);}
    Row row(Id i) const { return {ix+ptr[i],val+ptr[i],size_t(ptr[i+1]-ptr[i])}; }
};
struct OwnedRow {
    std::vector<int32_t> ix; std::vector<float> values;
    Row row() const { return {ix.data(),values.data(),ix.size()}; }
};
struct Record { Row original; OwnedRow owned; bool owning=false,active=true,delta=false; double length=0; };
struct Collection {
    uint32_t dim; std::vector<Record> records; std::vector<uint64_t> df; size_t active=0;
    explicit Collection(uint32_t d):dim(d),df(d,0){}
    Collection(const CSR& data,size_t limit=0):Collection(data.d) {
        size_t n=limit?std::min<uint64_t>(limit,data.n):data.n; records.reserve(n);
        for(size_t i=0;i<n;++i) {
            Row x=data.row(i); if(norm(x)==0) throw std::runtime_error("Data vector is zero");
            records.push_back({x,{},false,true,false,norm(x)}); ++active;
            for(size_t p=0;p<x.size;++p) if(x.values[p]>0) ++df[x.indices[p]];
        }
    }
    Row row(Id i) const { auto& r=records.at(i); return r.owning?r.owned.row():r.original; }
    std::vector<Id> active_ids() const {std::vector<Id> ids; for(Id i=0;i<records.size();++i) if(records[i].active) ids.push_back(i); return ids;}
    Id append(OwnedRow x) {
        if(records.size()>=null_slot || x.ix.size()!=x.values.size() || x.ix.empty()) throw std::runtime_error("Invalid inserted row");
        int32_t prev=-1; for(size_t p=0;p<x.ix.size();++p) {
            if(x.ix[p]<=prev || x.ix[p]<0 || uint32_t(x.ix[p])>=dim || !std::isfinite(x.values[p]) || x.values[p]<0)
                throw std::runtime_error("Invalid inserted coordinates");
            prev=x.ix[p];
        }
        if(norm(x.row())==0) throw std::runtime_error("Cannot insert zero vector");
        Record r; r.owned=std::move(x); r.owning=true; r.length=norm(r.owned.row());
        Id id=records.size(); records.push_back(std::move(r)); ++active;
        Row v=row(id);for(size_t p=0;p<v.size;++p) if(v.values[p]>0) ++df[v.indices[p]];
        return id;
    }
    void erase(Id id) { auto& r=records.at(id); if(!r.active) throw std::runtime_error("Identifier already inactive");
        Row v=row(id);for(size_t p=0;p<v.size;++p) if(v.values[p]>0) --df[v.indices[p]];
        r.active=false;r.delta=false;--active;
    }
};

}
