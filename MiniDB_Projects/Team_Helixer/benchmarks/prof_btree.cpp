// Quick per-phase profiler to locate the B+Tree bottleneck.
#include <cstdio>
#include <chrono>
#include <random>
#include <numeric>
#include <algorithm>
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "index/btree.h"
using namespace minidb;
using clk = std::chrono::high_resolution_clock;
static double ms(clk::time_point a, clk::time_point b){ return std::chrono::duration<double,std::milli>(b-a).count(); }

int main(int argc, char**argv){
    int N = argc>1 ? atoi(argv[1]) : 20000;
    int frames = argc>2 ? atoi(argv[2]) : 128;
    std::remove("prof.db");
    DiskManager disk("prof.db");
    BufferPoolManager bpm(frames, &disk);
    BPlusTree tree(&bpm);
    std::vector<int> keys(N); std::iota(keys.begin(),keys.end(),0);
    std::mt19937 rng(1); std::shuffle(keys.begin(),keys.end(),rng);

    auto t0=clk::now();
    for(int k:keys) tree.insert(k, RID{k%97,k});
    auto t1=clk::now();
    for(int k=0;k<N;++k){ RID r; tree.search(k,&r); }
    auto t2=clk::now();
    for(int k=0;k<N;k+=2) tree.remove(k);
    auto t3=clk::now();
    for(int k=0;k<N;++k){ RID r; tree.search(k,&r); }
    auto t4=clk::now();
    printf("N=%d frames=%d  insert=%.1fms  search=%.1fms  delete=%.1fms  search2=%.1fms  writes=%d\n",
        N, frames, ms(t0,t1), ms(t1,t2), ms(t2,t3), ms(t3,t4), disk.write_count());
    std::remove("prof.db");
    return 0;
}
