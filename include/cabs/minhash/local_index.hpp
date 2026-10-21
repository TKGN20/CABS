#pragma once
#include "cabs/clustering/spherical_partition.hpp"
namespace cabs_index {


inline Slot signature(const std::vector<Slot>& sketch,uint64_t seed) {
    Slot winner=null_slot;uint64_t best=UINT64_MAX;
    for(Slot slot:sketch){uint64_t rank=mix(uint64_t(slot)^seed);if(winner==null_slot || rank<best){best=rank;winner=slot;}}
    return winner;
}
struct Table {
    uint64_t seed=0;
    std::vector<Slot> keys;std::vector<uint32_t> offsets;std::vector<Id> ids;
    std::unordered_map<Slot,std::vector<Id>> additions;
    void build(const std::vector<Id>& members,const Sketches& sketches,uint32_t universe) {
        std::vector<uint64_t> ranks(universe);for(Slot s=0;s<universe;++s)ranks[s]=mix(uint64_t(s)^seed);
        std::vector<Slot> sigs(members.size(),null_slot);std::vector<uint32_t> counts(universe,0);
        for(size_t p=0;p<members.size();++p) {
            Slot winner=null_slot;uint64_t best=UINT64_MAX;
            for(Slot s:sketches.sets[members[p]])if(winner==null_slot || ranks[s]<best){best=ranks[s];winner=s;}
            sigs[p]=winner;if(winner!=null_slot)++counts[winner];
        }
        uint32_t z=0;for(Slot s=0;s<universe;++s)if(counts[s]){keys.push_back(s);offsets.push_back(z);z+=counts[s];}
        offsets.push_back(z);ids.resize(z);std::vector<uint32_t> cursor(universe,0);
        for(size_t p=0;p<keys.size();++p)cursor[keys[p]]=offsets[p];
        for(size_t p=0;p<members.size();++p)if(sigs[p]!=null_slot)ids[cursor[sigs[p]]++]=members[p];
    }
    template<class F> void visit(Slot key,F f) const {
        if(key==null_slot)return;
        auto it=std::lower_bound(keys.begin(),keys.end(),key);
        if(it!=keys.end() && *it==key){size_t p=it-keys.begin();for(size_t j=offsets[p];j<offsets[p+1];++j)f(ids[j]);}
        auto a=additions.find(key);if(a!=additions.end())for(Id id:a->second)f(id);
    }
    uint64_t bytes() const{return keys.capacity()*4+offsets.capacity()*4+ids.capacity()*4;}
};

}
