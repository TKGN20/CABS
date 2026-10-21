#pragma once
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <omp.h>

namespace cabs_index {

using Id = uint32_t;
using Slot = uint32_t;
constexpr Slot null_slot = std::numeric_limits<Slot>::max();
inline double seconds() { return omp_get_wtime(); }
inline uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline uint64_t derive(uint64_t seed, uint64_t domain, uint64_t i = 0) {
    return mix(seed ^ mix(domain) ^ mix(i));
}
inline double uniform01(uint64_t v) { return (v >> 11) * 0x1.0p-53; }
struct Row {
    const int32_t* indices = nullptr;
    const float* values = nullptr;
    size_t size = 0;
};
inline double norm(Row x) {
    double v = 0; for (size_t i=0;i<x.size;++i) v += double(x.values[i])*x.values[i];
    return std::sqrt(v);
}
inline double dot(Row a, Row b) {
    double v=0; size_t i=0,j=0;
    while (i<a.size && j<b.size) {
        if (a.indices[i]<b.indices[j]) ++i;
        else if (b.indices[j]<a.indices[i]) ++j;
        else { v+=double(a.values[i])*b.values[j]; ++i; ++j; }
    } return v;
}
inline double dense_dot(Row x, const std::vector<double>& y) {
    double v=0; for(size_t i=0;i<x.size;++i) v+=double(x.values[i])*y[x.indices[i]];
    return v;
}
inline double angle(double cosine) { return std::acos(std::clamp(cosine,-1.0,1.0)); }
inline double outward(double v) { return std::nextafter(v+1e-12, INFINITY); }


} // namespace cabs_index
