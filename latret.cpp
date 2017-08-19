//
// Created by Sharath Gururaj on 7/26/17.
//
#include <algorithm>    // std::max
#include <iostream>     // std::cout
#include "latret.h"

template<size_t SIZE, class T> inline size_t array_size(T (&arr)[SIZE]) {
    return SIZE;
}

void LatencyTimerThreadUnsafe::count(long latencyNanos) {
    int index=0;
    maxNanos = std::max(maxNanos, latencyNanos);
    while(latencyNanos >= 1000) {
        latencyNanos /= 1000;
        index+=1000;
    }
    bins[std::min((int)(index+latencyNanos), BIN_LENGTH-1)]++;
//    for(int i=0; i<BIN_LENGTH; i++) std::cout<<bins[i]<<" ";
//    std::cout<<std::endl;

}

void LatencyTimerThreadUnsafe::count() {
    auto now = std::chrono::high_resolution_clock::now();
    if(lastCountInit) {
        count(std::chrono::duration_cast<std::chrono::nanoseconds>(now-lastCount).count());
    }
    lastCount = now;
    lastCountInit = true;
}

LatRet LatencyTimerThreadUnsafe::snap() {

    long mytotal = 0;
    for (long bin : bins) {
        mytotal += bin;
    }

    static double nanos[7];
    int index = 0;
    long cumulative = 0;
    for(int i=0; i<array_size(pTiles); i++) {
        long max = (long)((mytotal*pTiles[i])/100.0);
        while(index < array_size(bins) && bins[index] + cumulative <  max) {
            cumulative+=bins[index];
            index++;
        }

        long mul = 1;
        int temp = index;
        while(temp >= 1000) {
            temp -= 1000;
            mul *= 1000;
        }
        nanos[i] = (temp+1)*mul;
    }
    reset();
    return {nanos, pTiles, mytotal, maxNanos, 0};
}

void LatencyTimerThreadUnsafe::reset() {
    memset(bins, 0, sizeof(bins));
}

LatencyTimerThreadUnsafe::LatencyTimerThreadUnsafe() {
    memset(bins, 0, sizeof(bins));
}

//int main1() {
//    int loops = 20000000;
//    LatencyTimerThreadUnsafe timer;
//    for(int i=0; i<loops; i++) {
//        timer.count();
//    }
//    LatRet ret = timer.snap();
//    for(int i=0; i<7; i++)
//        std::cout<<ret.pTiles[i]<<": "<<ret.nanos[i]<<std::endl;
//}

