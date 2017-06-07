#ifndef __MISS_TABLE_H_
#define __MISS_TABLE_H_

#include <cinttypes>
#include <unordered_map>
#include <vector>

#include <base/types.hh>

struct MissEntry {
    ThreadID tid;
    int16_t cacheLevel;
    uint64_t seqNum;
    Tick startTick;
    bool isLoad;

    MissEntry(ThreadID _tid, int32_t level, uint64_t sn, Tick t,
            bool r)
        : tid(std::move(_tid)), cacheLevel((int16_t) level),
        seqNum(sn), startTick(std::move(t)), isLoad(std::move(r)) {}
};



typedef std::unordered_map<uint64_t, MissEntry> MissTable;

extern MissTable l1MissTable;
extern MissTable l2MissTable;

struct MissStat {
    std::vector<int> numL1Miss;
    std::vector<int> numL2Miss;
    std::vector<int> numL1MissLoad;
    std::vector<int> numL2MissLoad;
    std::vector<uint64_t> oldestStoreTick;
};

extern MissStat missStat;

bool inMissTable(MissTable &mt, uint64_t sn);

bool isMiss(uint64_t sn);

enum MemAccessType {
    MemLoad,
    MemStore,
    NotCare,
};

bool isSpecifiedMiss(uint64_t sn, int16_t cacheLevel, MemAccessType mat);


#endif // __MISS_TABLE_H_
