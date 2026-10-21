#pragma once
#include "cabs/minhash/local_index.hpp"
namespace cabs_index {
struct QueryStats {size_t verified=0,touched=0,entries=0,bucket_accesses=0,probed=0,pool_peak=0,delta=0;bool empty=false,fallback=false;double time=0;};
struct QueryDiagnostics {std::unordered_map<Id,double> proxies;std::unordered_set<Id> verified;};
struct Pool {
    size_t capacity;
    std::unordered_map<Id,double> observed;
    std::set<std::pair<double,Id>> ordered;
    explicit Pool(size_t b):capacity(b){}


    void update(const std::unordered_map<Id,uint32_t>& collisions,size_t m,size_t query_size,const std::vector<uint32_t>& cardinal,
                std::unordered_set<Id>* admissions=nullptr) {
        if(admissions)admissions->clear();
        for(auto kv:collisions) {
            double j=double(kv.second)/m,v=j/(1+j)*(query_size+cardinal[kv.first]);
            auto it=observed.find(kv.first);
            if(it!=observed.end() && it->second>=v)continue;
            bool was_in_pool=false;
            if(it!=observed.end())was_in_pool=ordered.erase({-it->second,kv.first})!=0;
            observed[kv.first]=v;ordered.insert({-v,kv.first});
            if(admissions && !was_in_pool)admissions->insert(kv.first);
        }
        while(ordered.size()>capacity) {
            auto evicted=std::prev(ordered.end());
            if(admissions)admissions->erase(evicted->second);
            ordered.erase(evicted);
        }
    }
};



struct BoundedPool : Pool {
    explicit BoundedPool(size_t b):Pool(b){}
    void update(const std::unordered_map<Id,uint32_t>& collisions,size_t m,size_t query_size,
                const std::vector<uint32_t>& cardinal,std::unordered_set<Id>* admissions=nullptr) {
        if(admissions)admissions->clear();
        for(auto kv:collisions){
            double j=double(kv.second)/m,v=j/(1+j)*(query_size+cardinal[kv.first]);
            auto old=observed.find(kv.first);
            if(old!=observed.end() && old->second>=v)continue;
            bool member=old!=observed.end() && ordered.erase({-old->second,kv.first})!=0;
            observed[kv.first]=v;
            std::pair<double,Id> candidate{-v,kv.first};
            if(ordered.size()==capacity){
                auto worst=std::prev(ordered.end());
                if(!(candidate<*worst))continue;
                if(admissions)admissions->erase(worst->second);
                ordered.erase(worst);
            }
            ordered.insert(candidate);
            if(admissions && !member)admissions->insert(kv.first);
        }
    }
};
}
