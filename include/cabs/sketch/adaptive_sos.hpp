#pragma once
#include "cabs/io/dataset_loader.hpp"
namespace cabs_index {
struct Expansion {
    std::vector<uint32_t> lengths,offset; double scale=0; uint64_t seed=0;
    void allocate(const std::vector<uint64_t>& df,size_t n,int ell,const std::string& rule="idf") {
        if(n==0 || ell<1) throw std::runtime_error("Nonempty collection and ell>=1 required");
        const size_t d=df.size(); if(d*uint64_t(ell)>=null_slot) throw std::runtime_error("Expanded universe too large");
        lengths.assign(d,1);offset.assign(d+1,0);std::vector<double> weights(d,0),rem(d);
        double sum=0;for(size_t j=0;j<d;++j) {
            weights[j]=rule=="uniform"?1.0:(df[j]?(rule=="inverse"?double(n)/df[j]:1+std::log(double(n)/df[j])):0);
            sum+=weights[j];
        }
        if(sum<=0) throw std::runtime_error("No positive document frequency");
        uint64_t total=0; for(size_t j=0;j<d;++j) {
            double v=1+double(d)*(ell-1)*weights[j]/sum;
            lengths[j]=uint32_t(std::floor(v));rem[j]=v-lengths[j];total+=lengths[j];
        }
        std::vector<size_t> order(d);std::iota(order.begin(),order.end(),0);
        std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return rem[a]!=rem[b]?rem[a]>rem[b]:a<b;});
        uint64_t budget=d*uint64_t(ell);
        if(total>budget || budget-total>d) throw std::runtime_error("Rounding error");
        for(size_t p=0;p<budget-total;++p) ++lengths[order[p]];
        for(size_t j=0;j<d;++j) offset[j+1]=offset[j]+lengths[j];
        if(offset.back()!=budget) throw std::runtime_error("Budget mismatch");
    }
    std::vector<Slot> encode(Row x,uint64_t nonce,bool query=false) const {
        double s=scale;if(query) {s=0;for(size_t p=0;p<x.size;++p)s=std::max(s,double(x.values[p]));}
        if(s<=0) return {};
        std::vector<Slot> out;
        const uint64_t key=derive(seed,query?0x5155455259ULL:0x44415441ULL,nonce);
        for(size_t p=0;p<x.size;++p) {
            const auto j=x.indices[p];double probability=x.values[p]/s;
            if(probability>1+1e-12) throw std::runtime_error("Out-of-range encoding");
            for(Slot slot=offset[j];slot<offset[j+1];++slot)
                if(uniform01(mix(key ^ mix(slot)))<probability) out.push_back(slot);
        } return out;
    }
};
struct Sketches {
    std::vector<std::vector<Slot>> sets;
    void build(const Collection& data,const Expansion& e) {
        sets.resize(data.records.size());
        #pragma omp parallel for schedule(dynamic,256)
        for(size_t i=0;i<data.records.size();++i) if(data.records[i].active) sets[i]=e.encode(data.row(i),i);
    }
    uint64_t bytes() const {uint64_t z=sets.capacity()*sizeof(std::vector<Slot>);for(auto& s:sets)z+=s.capacity()*sizeof(Slot);return z;}
};
inline size_t overlap(const std::vector<Slot>& a,const std::vector<Slot>& b) {
    size_t i=0,j=0,z=0;while(i<a.size() && j<b.size()) {if(a[i]<b[j])++i;else if(b[j]<a[i])++j;else{++i;++j;++z;}}return z;
}
inline double jaccard(const std::vector<Slot>& a,const std::vector<Slot>& b) {
    size_t z=overlap(a,b);return z?double(z)/(a.size()+b.size()-z):0;
}

}
