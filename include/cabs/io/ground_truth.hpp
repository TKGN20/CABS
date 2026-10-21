#pragma once
#include "cabs/sketch/adaptive_sos.hpp"
namespace cabs_index {
struct Hit {Id id;double score;};
inline bool better(const Hit& a,const Hit& b){return a.score!=b.score?a.score>b.score:a.id<b.id;}
inline std::vector<Hit> exact(const Collection& data,Row q,size_t k) {
    auto cmp=[](const Hit& a,const Hit& b){return better(a,b);};
    std::priority_queue<Hit,std::vector<Hit>,decltype(cmp)> heap(cmp);
    std::vector<double> dense(data.dim,0);for(size_t p=0;p<q.size;++p)dense[q.indices[p]]=q.values[p];
    for(Id i=0;i<data.records.size();++i) if(data.records[i].active) {
        Hit h{i,dense_dot(data.row(i),dense)};
        if(heap.size()<k)heap.push(h);else if(better(h,heap.top())){heap.pop();heap.push(h);}
    }
    std::vector<Hit> out;while(!heap.empty()){out.push_back(heap.top());heap.pop();}std::sort(out.begin(),out.end(),better);return out;
}

}
