#ifndef __MISS_TABLE_H_
#define __MISS_TABLE_H_

#include <cinttypes>
#include <list>
#include <vector>

#include <base/types.hh>

struct MissEntry {
    ThreadID tid;
    int16_t cacheLevel;
    uint64_t seqNum;
    Tick startTick;

    MissEntry(ThreadID _tid, int16_t level, uint64_t sn, Tick t)
        : tid(std::move(_tid)), cacheLevel(std::move(level)),
        seqNum(sn), startTick(std::move(t)) {}
};

typedef std::list<MissEntry> MissTable;

extern MissTable missTable;

struct MissStat {
    std::vector<int> numL1Miss;
    std::vector<int> numL2Miss;
};

extern MissStat missStat;


#endif // __MISS_TABLE_H_
