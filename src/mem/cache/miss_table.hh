#ifndef __MISS_TABLE_H__
#define __MISS_TABLE_H__

#include <cinttypes>
#include <unordered_map>
#include <vector>

#include "base/types.hh"
#include "base/trace.hh"
#include "debug/MissTable.hh"
#include "mem/cache/miss_descpriptor.hh"

struct MissEntry {
    ThreadID tid;
    int16_t cacheLevel;
    bool isInterference;
    MemAccessType mat;
    Tick startTick;
    uint64_t seqNum;
    int MSHRHits;

    MissEntry(ThreadID _tid, int32_t level, bool interf,
            MemAccessType _mat, Tick st, uint64_t sn, Addr address)
        : tid(_tid), cacheLevel((int16_t) level),
        isInterference(interf), mat(_mat), startTick(st),
        seqNum(sn), MSHRHits(0)
    {
        address = address;
        DPRINTF(MissTable, "Insert L%i cache miss @ addr [0x%x]\n", level, address);
    }
};



typedef std::unordered_map<Addr, MissEntry> MissTable;

#define MaxThreads 2
struct MissStat {
    std::array<int, MaxThreads> numL1InstMiss;
    std::array<int, MaxThreads> numL1StoreMiss;
    std::array<int, MaxThreads> numL1LoadMiss;
    std::array<int, MaxThreads> numL2InstMiss;
    std::array<int, MaxThreads> numL2DataMiss;
};
#undef MaxThreads


class MissTables {
public:
    MissTable l1IMissTable;
    MissTable l1DMissTable;
    MissTable l2MissTable;

    int numL1I_MSHR;
    int numL1DR_MSHR;
    int numL1DW_MSHR;
    int numL2_MSHR;

    MissStat missStat;

    bool isSpecifiedMiss(Addr address, bool isDCache, MissDescriptor &md);

    bool isL1Miss(Addr address, bool &isInst);

    void printMiss(MissTable &mt);

    void printAllMiss();

    int cacheBlockSize;

    Addr blockAlign(Addr addr) {
        return (addr & ~(Addr(cacheBlockSize - 1)));
    }

    bool isMSHRFull(int cacheLevel, bool isDCache);

    bool perThreadMSHRFull(int cacheLevel, bool isDCache, ThreadID tid, bool isLoad);

    bool hasInstMiss(ThreadID tid);

    bool hasDataMiss(ThreadID tid);

    bool hasMiss(MissTable &mt, ThreadID tid);

    bool kickedDataBlock(ThreadID tid, int &cacheLevel);

    bool kickedInstBlock(ThreadID tid, int &cacheLevel);

    bool kickedBlock(MissTable &mt, ThreadID tid);
};

extern MissTables missTables;

#endif // __MISS_TABLE_H__
