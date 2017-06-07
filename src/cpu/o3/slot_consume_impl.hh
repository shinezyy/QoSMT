#ifndef __CPU_O3_SLOT_CONSUME_IMPL_HH__
#define __CPU_O3_SLOT_CONSUME_IMPL_HH__


#include <queue>
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
                  slotConsumption[tid].end(), SlotsUse::NotUsed);
        std::fill(queueHeadState[tid].begin(),
                  queueHeadState[tid].end(), NoState);
        std::fill(vqState[tid].begin(), vqState[tid].end(), NoVQ);
    }
    std::fill(localSlotIndex.begin(), localSlotIndex.end(), 0);
}

template<class Impl>
void
SlotConsumer <Impl>::
consumeSlots(int numSlots, ThreadID who, SlotsUse su)
{
    for (int x = 0; x < numSlots; x++) {
        slotConsumption[who][localSlotIndex[who] + x] = su;
        slotConsumption[another(who)][localSlotIndex[another(who)] + x]
                = SlotsUse::WidthWait;
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
        DPRINTFR(SlotConsume, "%s ", slotUseStr[slotConsumption[HPT][x]]);
        assert(slotConsumption[HPT][x] != SlotsUse::NotUsed);
    }
    for (; x < Impl::MaxWidth; x++) {
        assert(slotConsumption[HPT][x] == SlotsUse::NotUsed);
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

template<class Impl>
void SlotConsumer <Impl>::
cycleEnd(ThreadID tid,
         std::array<unsigned, Impl::MaxThreads> &toNextStageNum,
         FullSource fullSource,
         std::array<SlotsUse, Impl::MaxWidth> &curCycleRow,
         std::queue<std::array<SlotsUse, Impl::MaxWidth> > &skidSlotBuffer,
         SlotCounter<Impl> *slotCounter,
         bool isRename, bool BLB, bool SI)
{
    // assert (localSlotIndex[tid] == stageWidth || localSlotIndex[tid] == 0);

    int blockedSlots = 0;
    if (toNextStageNum[tid] > 0) {
        slotCounter->incLocalSlots(tid, SlotsUse::Base, toNextStageNum[0]);
        int cursor = 0, index = 0;
        while (curCycleRow[cursor] == Referenced) cursor++;
        if (cursor > 0) {
            slotCounter->incLocalSlots(tid, SlotsUse::SplitMiss, cursor);
        }
        for (; index < toNextStageNum[tid]; index++) {
            assert(curCycleRow[cursor + index] == SlotsUse::Base);
        }

        bool allProc = false;
        for (int i = 0; cursor + index + i < stageWidth; i++) {
            if (curCycleRow[i] == Base) {
                allProc = true;
            }
        }
        if (!allProc) {
            for (int i = cursor; i < cursor + index; i++) {
                skidSlotBuffer.front()[i] = Referenced;
            }
        }

        if (SI) {
            slotCounter->incLocalSlots(tid, SlotsUse::SerializeMiss,
                                       toNextStageNum[tid] - cursor - index);
            return;

        } else if (fullSource == FullSource::NONE) {
            for (; cursor + index < stageWidth; index++) {
                slotCounter->incLocalSlots(tid, curCycleRow[cursor+index], 1);
            }
            return;
        } else {
            blockedSlots = toNextStageNum[tid] - cursor - index;
        }
    } else {
        if (localSlotIndex[tid] == 8) {
            return;
        }
        assert(localSlotIndex[tid] == 0);
        blockedSlots = stageWidth;
    }

    if (isRename) {
        if (fullSource == FullSource::IEWStage) {
            if (BLB) {
                slotCounter->incLocalSlots(tid, LaterWait, blockedSlots);
            } else {
                slotCounter->incLocalSlots(tid, LaterMiss, blockedSlots);
            }
        } else if (fullSource == FullSource::Register) {
            slotCounter->incLocalSlots(tid, EntryMiss, blockedSlots);
        } else if (fullSource == FullSource::ROB) {
            if (queueHeadState[tid][ROB] == Normal) {
                slotCounter->incLocalSlots(tid, ROBWait, blockedSlots);
            } else { // head inst in ROB is DCache Miss
                assert(queueHeadState[tid][ROB] != NoState);
                if (vqState[tid][ROB] == VQNotFull) {
                    slotCounter->incLocalSlots(tid, ROBWait, blockedSlots);
                } else {
                    slotCounter->incLocalSlots(tid, ROBMiss, blockedSlots);
                }
                assert(vqState[tid][ROB] != NoVQ);
            }
        }
    } else { // IEW stage
        assert(fullSource == FullSource::IQ ||
               fullSource == FullSource::LQ ||
               fullSource == FullSource::SQ);

        // trick depends on sequence of enum definition to avoid duplication
        int distance_to_iq = fullSource - IQ;
        if (queueHeadState[tid][fullSource] == Normal) {
            slotCounter->incLocalSlots(
                    tid, static_cast<SlotsUse>(2*distance_to_iq + 0), blockedSlots);
        } else { // head inst in queue is DCache Miss
            assert(queueHeadState[tid][fullSource] != NoState);
            if (vqState[tid][fullSource] == VQNotFull) {
                slotCounter->incLocalSlots(
                        tid, static_cast<SlotsUse>(2*distance_to_iq + 0), blockedSlots);
            } else {
                slotCounter->incLocalSlots(
                        tid, static_cast<SlotsUse>(2*distance_to_iq + 1), blockedSlots);
            }
            assert(vqState[tid][fullSource] != NoVQ);
        }
    }
}


#endif
