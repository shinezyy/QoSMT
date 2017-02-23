#ifndef __CPU_O3_SLOTCOUNTER_IMPL_HH__
#define __CPU_O3_SLOTCOUNTER_IMPL_HH__

#include <cinttypes>

#include "cpu/o3/comm.hh"
#include "debug/LB.hh"
#include "params/DerivO3CPU.hh"
#include "cpu/o3/slot_counter.hh"

using namespace std;

    template<class Impl>
SlotCounter<Impl>::SlotCounter(DerivO3CPUParams *params)
    : wait(params->numThreads, 0),
    miss(params->numThreads, 0)
{
}

template <class Impl>
void
SlotCounter<Impl>::sumLocalSlots(ThreadID tid, bool isWait, uint32_t num)
{
    if (isWait) {
        wait[tid] += num;
    } else {
        miss[tid] += num;
    }
    DPRINTF("T[%u]: Adding %i %s slots locally\n", tid, num,
            isWait ? "wait" : "miss");
}

template <class Impl>
void
SlotCounter<Impl>::assignSlots(ThreadID tid, DynInstPtr& inst)
{
    inst->incWaitSlots(wait[tid]);
    wait[tid] = 0;
    inst->incMissSlots(miss[tid]);
    miss[tid] = 0;
    DPRINTF("T[%u]: Assigning %i wait slots, %i miss slots "
            "to Inst[%llu]\n", tid, wait[tid], miss[tid], inst->seqNum);
}

#endif  //  __CPU_O3_SLOTCOUNTER_IMPL_HH__
