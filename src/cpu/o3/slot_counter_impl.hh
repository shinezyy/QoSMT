#ifndef __CPU_O3_SLOTCOUNTER_IMPL_HH__
#define __CPU_O3_SLOTCOUNTER_IMPL_HH__

#include <cinttypes>
#include <algorithm>
#include <numeric>

#include "cpu/o3/comm.hh"
#include "debug/LB.hh"
#include "debug/VLB.hh"
#include "debug/SlotCounter.hh"
#include "params/DerivO3CPU.hh"
#include "cpu/o3/slot_counter.hh"

using namespace std;

ThreadID HPT = 0, LPT = 1;

const char* slotUseStr[] = {
        "NotInitiated",
        "NotUsed",
        "InstSupMiss",
        "L1ICacheInterference",
        "L2ICacheInterference",
        "FetchSliceWait",
        "WidthWait",
        "EntryMiss",
        "Base",
        "LaterMiss",
        "LaterWait",
        "LBLCWait",
        "SerializeMiss",
        "SquashMiss",
        "NotFullInstSupMiss",
        "Referenced",
        "SplitWait",
        "ROBWait",
        "ROBMiss",
        "IQWait",
        "IQMiss",
        "LQWait",
        "LQMiss",
        "SQWait",
        "SQMiss",
        "SplitMiss",
        "L1DCacheInterference",
        "L2DCacheInterference",
};

std::array<SlotsUse, 11> missEnums = {
        InstSupMiss,
        EntryMiss,
        LaterMiss,
        SerializeMiss,
        SquashMiss,
        NotFullInstSupMiss,
        ROBMiss,
        IQMiss,
        LQMiss,
        SQMiss,
        SplitMiss,
};

std::array<SlotsUse, 13> waitEnums = {
        L1ICacheInterference,
        L2ICacheInterference,
        FetchSliceWait,
        ROBWait,
        IQWait,
        LQWait,
        SQWait,
        L1DCacheInterference,
        L2DCacheInterference,
        WidthWait,
        LaterWait,
        LBLCWait,
        SplitWait,
};

    template<class Impl>
SlotCounter<Impl>::SlotCounter(DerivO3CPUParams *params, uint32_t _width)
    : width((int) _width),
    numThreads((ThreadID) params->numThreads)
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        wait[tid] = 0;
        miss[tid] = 0;
        std::fill(perCycleSlots[tid].begin(), perCycleSlots[tid].end(), NotInitiated);
        std::fill(slotUseRow[tid].begin(), slotUseRow[tid].end(), NotInitiated);
        slotIndex[tid] = 0;
        curCycleBase[tid] = 0;
        curCycleMiss[tid] = 0;
        curCycleWait[tid] = 0;
    }
    std::fill(slots.begin(), slots.end(), 0);
    std::fill(recentSlots.begin(), recentSlots.end(), 0);
}

template <class Impl>
void
SlotCounter<Impl>::incLocalSlots(ThreadID tid, SlotsUse su, int32_t num)
{
    DPRINTF(SlotCounter, "T[%i]: Adding %i %s slots locally [Index=%i]\n", tid, num,
            slotUseStr[su], slotIndex[tid]);
    for (int x = 0; x < num; x++) {
        if (slotIndex[tid] >= Impl::MaxWidth) {
            panic("slotIndex[%i] = %i is too large\n", tid, slotIndex[tid]);
        }
        slotUseRow[tid][slotIndex[tid]++] = su;
    }

    perCycleSlots[tid][su] += num;
    slotsStat[su] += num;
    slots[su] += num;
    recentSlots[su] += num;
}

template <class Impl>
void
SlotCounter<Impl>::incLocalSlots(ThreadID tid, SlotsUse su,
        int32_t num, bool verbose)
{
    perCycleSlots[tid][su] += num;
    slotsStat[su] += num;
    slots[su] += num;
    recentSlots[su] += num;

    if (verbose) {
        DPRINTF(VLB, "T[%i]: Adding %i %s slots locally\n", tid, num,
                slotUseStr[su]);
    }
}


template <class Impl>
bool
SlotCounter<Impl>::checkSlots(ThreadID tid)
{
    if (std::accumulate(perCycleSlots[tid].begin(),
                perCycleSlots[tid].end(), 0) == width &&
            perCycleSlots[tid][NotUsed] == 0) {
        return true;
    } else {
        int it = 0; // avoid to use [] unnecessarily
        for (auto slot : perCycleSlots[tid]) {
            if (slot) {
                printf("%s: %d | ", slotUseStr[it], slot);
            }
            it++;
        }
        printf("\n");
        fflush(stdout);
        panic("Cycle slot in %s checking not satisified!\n", this->name().c_str());
        return false;
    }
}

template <class Impl>
void
SlotCounter<Impl>::sumLocalSlots(ThreadID tid)
{
    curCycleMiss[tid] = 0;
    curCycleWait[tid] = 0;

    for (auto it = missEnums.begin(); it != missEnums.end(); ++it) {
        curCycleMiss[tid] += perCycleSlots[tid][*it];
    }
    miss[tid] += curCycleMiss[tid];

    for (auto it = waitEnums.begin(); it != waitEnums.end(); ++it) {
        curCycleWait[tid] += perCycleSlots[tid][*it];
    }
    wait[tid] += curCycleWait[tid];

    curCycleBase[tid] = perCycleSlots[tid][SlotsUse::Base];

    std::fill(perCycleSlots[tid].begin(), perCycleSlots[tid].end(), 0);

    for(ThreadID i = 0; i < numThreads; i++) {
        std::fill(slotUseRow[i].begin(), slotUseRow[i].end(), NotUsed);
        slotIndex[i] = 0;
    }
}

template <class Impl>
void
SlotCounter<Impl>::regStats()
{
    using namespace Stats;

    for(int su = 0; su < NumUse; ++su) {
        const string suStr = slotUseStr[su];
        slotsStat[su]
            .name(name() + "." + suStr + "_Slots")
            .desc("number of " + suStr + " Slots")
            .flags(total)
            ;
    }

    waitSlots
        .name(name() + ".local_wait_slots")
        .desc("number of HPT wait slots in " + name())
        ;

    missSlots
        .name(name() + ".local_miss_slots")
        .desc("number of HPT miss slots in " + name())
        ;

    baseSlots
        .name(name() + ".local_base_slots")
        .desc("number of HPT base slots in " + name())
        ;

    baseSlots = slotsStat[Base];
}

template<class Impl>
void
SlotCounter<Impl>::
printSlotRow(std::array<SlotsUse, Impl::MaxWidth> row, int width) {
    for (int i = 0; i < width; i++) {
        DPRINTFR(SlotCounter, "%s | ", slotUseStr[row[i]]);
    }
    DPRINTFR(SlotCounter, "\n");
}

template<class Impl>
void
SlotCounter<Impl>::dumpStats() {
    waitSlots = 0;
    missSlots = 0;
    for (auto it = waitEnums.begin(); it != waitEnums.end(); ++it) {
        waitSlots += slots[*it];
    }
    for (auto it = missEnums.begin(); it != missEnums.end(); ++it) {
        missSlots += slots[*it];
    }
}

template<class Impl>
void
SlotCounter<Impl>::clearRecent() {
    std::fill(recentSlots.begin(), recentSlots.end(), 0);
}


#endif  //  __CPU_O3_SLOTCOUNTER_IMPL_HH__
