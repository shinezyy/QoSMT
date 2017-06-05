#ifndef __CPU_O3_SLOT_CONSUME_IMPL_HH__
#define __CPU_O3_SLOT_CONSUME_IMPL_HH__


#include "base/trace.hh"
#include "cpu/o3/slot_consume.hh"
#include "params/DerivO3CPU.hh"
#include "debug/SlotConsume.hh"

template<class Impl>
void
SlotConsumer <Impl>::
cleanSlots()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        std::fill(slotConsumption[tid].begin(),
                  slotConsumption[tid].end(), NotConsumed);
        std::fill(queueHeadState[tid].begin(),
                  queueHeadState[tid].end(), NoState);
        std::fill(vqState[tid].begin(), vqState[tid].end(), NoVQ);
    }
    std::fill(localSlotIndex.begin(), localSlotIndex.end(), 0);
}

template<class Impl>
void
SlotConsumer <Impl>::
consumeSlots(int numSlots, ThreadID who, WayOfConsumeSlots wocs)
{
    for (int x = 0; x < numSlots; x++) {
        slotConsumption[who][localSlotIndex[who] + x] = wocs;
        slotConsumption[another(who)][localSlotIndex[another(who)] + x]
                = OtherThreadsUsed;
    }
    localSlotIndex[who] += numSlots;
    localSlotIndex[another(who)] += numSlots;
}

template<class Impl>
void
SlotConsumer <Impl>::
addUpSlots()
{
    DPRINTF(SlotConsume, "Rename slot used this cycle [Begin] --------\n");
    int x = 0;
    for (; x < stageWidth; x++) {
        DPRINTFR(SlotConsume, "%s ", getConsumeString(slotConsumption[HPT][x]));
        assert(slotConsumption[HPT][x] != NotConsumed);
    }
    for (; x < Impl::MaxWidth; x++) {
        assert(slotConsumption[HPT][x] == NotConsumed);
    }
    DPRINTFR(SlotConsume, "\nRename slot used this cycle [end] --------\n");
}

    template<class Impl>
SlotConsumer <Impl>::SlotConsumer(DerivO3CPUParams *params, unsigned width,
                                 std::string father_name)
        : stageWidth(width),
        numThreads((ThreadID) params->numThreads)
{
    consumerName = father_name;
}


#endif
