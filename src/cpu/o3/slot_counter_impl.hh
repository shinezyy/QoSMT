#ifndef __CPU_O3_SLOTCOUNTER_IMPL_HH__
#define __CPU_O3_SLOTCOUNTER_IMPL_HH__

#include <cinttypes>
#include <algorithm>

#include "cpu/o3/comm.hh"
#include "debug/LB.hh"
#include "debug/VLB.hh"
#include "params/DerivO3CPU.hh"
#include "cpu/o3/slot_counter.hh"

using namespace std;

ThreadID HPT = 0, LPT = 1;

    template<class Impl>
SlotCounter<Impl>::SlotCounter(DerivO3CPUParams *params, uint32_t _width)
    : width((int) _width),
    numThreads(params->numThreads)
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        wait[tid] = 0;
        miss[tid] = 0;
        for (int i = 0; i < NumUse; i++) {
            perCycleSlots[tid][i] = 0;
        }
    }
}

template <class Impl>
void
SlotCounter<Impl>::incLocalSlots(ThreadID tid, SlotsUse su, int32_t num)
{
    perCycleSlots[tid][su] += num;
    slots[su] += num;

    DPRINTF(LB, "T[%i]: Adding %i %s slots locally\n", tid, num,
            getSlotUseStr(su));
}

template <class Impl>
void
SlotCounter<Impl>::incLocalSlots(ThreadID tid, SlotsUse su,
        int32_t num, bool verbose)
{
    perCycleSlots[tid][su] += num;
    slots[su] += num;

    if (verbose) {
        DPRINTF(VLB, "T[%i]: Adding %i %s slots locally\n", tid, num,
                getSlotUseStr(su));
    }
}

template <class Impl>
void
SlotCounter<Impl>::assignSlots(ThreadID tid, DynInstPtr& inst)
{
    inst->incWaitSlot(wait[tid]);
    wait[tid] = 0;
    inst->incMissSlot(miss[tid]);
    miss[tid] = 0;
    DPRINTF(LB, "T[%u]: Assigning %i wait slots, %i miss slots "
            "to Inst[%llu]\n", tid, wait[tid], miss[tid], inst->seqNum);
}

template <class Impl>
bool
SlotCounter<Impl>::checkSlots(ThreadID tid)
{
    if (std::accumulate(perCycleSlots[tid].begin(),
                perCycleSlots[tid].end(), 0) == width) {
        return true;
    } else {
        DPRINTF(LB, "Cycle slot checking not satisified!\n");
        int it = 0; // avoid to use [] unnecessarily
        for (auto slot : perCycleSlots[tid]) {
            if (slot) {
                DPRINTFR(LB, "%s: %d | ", getSlotUseStr(it), slot);
            }
            it++;
        }
        DPRINTFR(LB, "\n");
        panic("Cycle slot checking not satisified!\n");
        return false;
    }
}

template <class Impl>
void
SlotCounter<Impl>::sumLocalSlots(ThreadID tid)
{
    miss[tid] += perCycleSlots[tid][InstMiss];
    miss[tid] += perCycleSlots[tid][EntryMiss];
    miss[tid] += perCycleSlots[tid][ComputeEntryMiss];
    miss[tid] += perCycleSlots[tid][LaterMiss];
    miss[tid] += perCycleSlots[tid][SerializeMiss];

    wait[tid] += perCycleSlots[tid][EarlierWait];
    wait[tid] += perCycleSlots[tid][WidthWait];
    wait[tid] += perCycleSlots[tid][EntryWait];
    wait[tid] += perCycleSlots[tid][ComputeEntryWait];
    wait[tid] += perCycleSlots[tid][LaterWait];
    wait[tid] += perCycleSlots[tid][LBLCWait];

    std::fill(perCycleSlots[tid].begin(), perCycleSlots[tid].end(), 0);
}

template <class Impl>
void
SlotCounter<Impl>::regStats()
{
    using namespace Stats;

    for(int su = 0; su < NumUse; ++su) {
        const string suStr = getSlotUseStr(su);
        slots[su]
            .name(name() + "." + suStr + "_Slots")
            .desc("number of " + suStr + " Slots")
            .flags(total)
            ;
    }

    waitSlots
        .name(name() + ".local_wait_slots")
        .desc("number of HPT wait slots in " + name())
        ;

    waitSlots = slots[EarlierWait] + slots[LBLCWait] +
        slots[WidthWait] + slots[EntryWait] +
        slots[ComputeEntryWait] + slots[LaterWait];

    missSlots
        .name(name() + ".local_miss_slots")
        .desc("number of HPT miss slots in " + name())
        ;

    missSlots = slots[InstMiss] + slots[EntryMiss] +
        slots[ComputeEntryMiss] + slots[LaterMiss] +
        slots[SerializeMiss];

    baseSlots
        .name(name() + ".local_base_slots")
        .desc("number of HPT base slots in " + name())
        ;

    baseSlots = slots[Base];
}



#endif  //  __CPU_O3_SLOTCOUNTER_IMPL_HH__
