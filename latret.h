#include<chrono>
#include <stdio.h>
//
// Created by Sharath Gururaj on 7/26/17.
//

#ifndef MYC_LATRET_H
#define MYC_LATRET_H
#define BIN_LENGTH 4000

struct LatRet {
    double *nanos;
    double *pTiles;
    long total;
    long maxNanos;
    long snapTimeMillis;
};

class LatencyTimerThreadUnsafe {
private:
    long bins[BIN_LENGTH];
    long maxNanos=0;
    std::chrono::high_resolution_clock::time_point lastCount;
    double pTiles[7] = {1, 50, 75, 90, 95, 99, 99.9};
    bool lastCountInit = false;
public:
    void count(long latencyNanos);
    void count();
    LatRet snap();
    void reset();
    LatencyTimerThreadUnsafe();
};

#endif //MYC_LATRET_H
