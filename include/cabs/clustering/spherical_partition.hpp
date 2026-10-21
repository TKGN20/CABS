#pragma once
#include "cabs/io/ground_truth.hpp"
namespace cabs_index {
struct Cluster {
    std::vector<double> mu;std::vector<Id> ids; double radius=0,cover=0,norm_bound=0;
    double floor=0;uint32_t m=1;
};
struct Partition {
    double spherical_seconds=0,spillover_seconds=0;
    std::vector<Cluster> clusters;std::vector<int> primary,secondary;
    std::vector<double> transposed_centroids(uint32_t dim) const {
        size_t K=clusters.size();std::vector<double> out(size_t(dim)*K);
        for(uint32_t j=0;j<dim;++j)for(size_t c=0;c<K;++c)out[size_t(j)*K+c]=clusters[c].mu[j];
        return out;
    }
    static void all_scores(Row x,const std::vector<double>& matrix,std::vector<double>& scores) {
        std::fill(scores.begin(),scores.end(),0);size_t K=scores.size();
        for(size_t p=0;p<x.size;++p) {
            const double* values=matrix.data()+size_t(x.indices[p])*K;double v=x.values[p];
            for(size_t c=0;c<K;++c)scores[c]+=v*values[c];
        }
    }
    void build(const Collection& data,int K,uint64_t seed,int max_iterations) {
        double stage_start=seconds();
        auto ids=data.active_ids();if(K<1 || size_t(K)>ids.size()) throw std::runtime_error("Invalid K");
        clusters.assign(K,{});primary.assign(data.records.size(),-1);secondary.assign(data.records.size(),-1);
        std::mt19937_64 rng(seed);std::shuffle(ids.begin(),ids.end(),rng);
        for(int c=0;c<K;++c) {
            auto& mu=clusters[c].mu;mu.assign(data.dim,0);Row x=data.row(ids[c]);
            for(size_t p=0;p<x.size;++p)mu[x.indices[p]]=x.values[p]/data.records[ids[c]].length;
        }
        for(int iteration=0;iteration<max_iterations;++iteration) {
            uint64_t changes=0;auto matrix=transposed_centroids(data.dim);
            #pragma omp parallel reduction(+:changes)
            {
                std::vector<double> scores(K);
                #pragma omp for schedule(dynamic,256)
                for(size_t p=0;p<ids.size();++p) {
                    Id id=ids[p];all_scores(data.row(id),matrix,scores);int best=0;
                    for(int c=1;c<K;++c)if(scores[c]>scores[best])best=c;
                    changes+=primary[id]!=best;primary[id]=best;
                }
            }
            std::vector<size_t> counts(K,0);for(Id id:ids)++counts[primary[id]];

            for(int c=0;c<K;++c) if(!counts[c]) {
                Id pick=null_slot;double worst=INFINITY;
                for(Id id:ids) if(counts[primary[id]]>1) {
                    double score=dense_dot(data.row(id),clusters[primary[id]].mu)/data.records[id].length;
                    if(score<worst || (score==worst && id<pick)){worst=score;pick=id;}
                }
                if(pick==null_slot)throw std::runtime_error("Cannot repair empty cluster");
                --counts[primary[pick]];primary[pick]=c;++counts[c];++changes;
            }
            for(auto& c:clusters)std::fill(c.mu.begin(),c.mu.end(),0);
            for(Id id:ids) {Row x=data.row(id);auto& mu=clusters[primary[id]].mu;
                for(size_t p=0;p<x.size;++p)mu[x.indices[p]]+=x.values[p]/data.records[id].length;
            }
            for(auto& c:clusters){double z=std::sqrt(std::inner_product(c.mu.begin(),c.mu.end(),c.mu.begin(),0.0));for(double& v:c.mu)v/=z;}
            if(!changes)break;
        }

        auto matrix=transposed_centroids(data.dim);
        #pragma omp parallel
        {
            std::vector<double> scores(K);
            #pragma omp for schedule(dynamic,256)
            for(size_t p=0;p<ids.size();++p) {
                Id id=ids[p];all_scores(data.row(id),matrix,scores);int first=-1,second=-1;double top=-1,next=-1;
                for(int c=0;c<K;++c){double v=scores[c];
                    if(v>top){next=top;second=first;top=v;first=c;}else if(v>next){next=v;second=c;}}
                primary[id]=first;secondary[id]=second;
            }
        }
        std::vector<size_t> count(K,0);
        for(Id id:ids){int a=primary[id];++count[a];clusters[a].radius=std::max(clusters[a].radius,outward(angle(dense_dot(data.row(id),clusters[a].mu)/data.records[id].length)));}
        for(int c=0;c<K;++c) if(!count[c])throw std::runtime_error("Empty final primary cluster; increase Lloyd iteration budget");
        for(auto& c:clusters)c.cover=c.radius;
        spherical_seconds=seconds()-stage_start;stage_start=seconds();
        for(Id id:ids) {
            int a=primary[id],b=secondary[id];clusters[a].ids.push_back(id);
            if(b>=0 && angle(dense_dot(data.row(id),clusters[b].mu)/data.records[id].length)<=clusters[b].radius)clusters[b].ids.push_back(id);
            else secondary[id]=-1;
        }
        for(auto& c:clusters){std::sort(c.ids.begin(),c.ids.end());for(Id id:c.ids)c.norm_bound=std::max(c.norm_bound,outward(data.records[id].length));}
        spillover_seconds=seconds()-stage_start;
    }
    std::vector<int> order(Row q) const {
        std::vector<int> o(clusters.size());std::iota(o.begin(),o.end(),0);
        std::vector<double> rho(clusters.size());for(size_t i=0;i<rho.size();++i)rho[i]=dense_dot(q,clusters[i].mu);
        std::sort(o.begin(),o.end(),[&](int a,int b){return rho[a]!=rho[b]?rho[a]>rho[b]:a<b;});return o;
    }
    double upper(Row q,int i) const {
        double nq=norm(q);if(nq==0)return 0;
        auto& c=clusters[i];double theta=angle(dense_dot(q,c.mu)/nq);
        return outward(nq*c.norm_bound*std::cos(std::max(0.0,theta-c.cover)));
    }
};

}
