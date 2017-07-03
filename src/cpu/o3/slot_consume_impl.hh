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
                  queueHeadState[tid].end(), HeadInstrState::NoState);
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
    }
    localSlotIndex[who] += numSlots;
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
        numThreads((ThreadID) params->numThreads),
        considerHeadStatus(params->considerHeadStatus)
{
    consumerName = father_name;
    std::fill(blockCounter.begin(), blockCounter.end(), 0);
}

template<class Impl>
void SlotConsumer <Impl>::
cycleEnd(ThreadID tid,
         std::array<unsigned, Impl::MaxThreads> &toNextStageNum,
         FullSource fullSource,
         std::array<SlotsUse, Impl::MaxWidth> &curCycleRow,
         std::queue<std::array<SlotsUse, Impl::MaxWidth> > &skidSlotBuffer,
         SlotCounter<Impl> *slotCounter,
         bool isRename, bool BLB_in, bool SI, bool finishSS,
         bool siTail, bool siTailNext, bool &BLB_out)
{
    // assert (localSlotIndex[tid] == stageWidth || localSlotIndex[tid] == 0);

    DPRINTF(SlotConsume, "SI: %i, finishSS: %i, tail SI: %i, tail SI next cycle:%i\n",
            SI, finishSS, siTail, siTailNext);

    int blockedSlots = 0;
    unsigned tNSN = toNextStageNum[tid];
    BLB_out = false;

    if (toNextStageNum[tid] > 0) {
        slotCounter->incLocalSlots(tid, SlotsUse::Base, toNextStageNum[tid]);
        int cursor = 0, index = 0;
        /** cursor:将会指向当前skid row中的以前处理过的slots尾部
          * index:将会指向本周期处理过的slots尾部
          * 当有旧的SI在本周期被处理掉时，需要修改该tNsN(减1)
          * 以避免送到下一个阶段的slots与insts不一致
         */

        DPRINTF(SlotConsume, "curCycleRow:\n");
        slotCounter->printSlotRow(curCycleRow, stageWidth);

        while (curCycleRow[cursor] == Referenced) cursor++;
        if (cursor > 0) {
            slotCounter->incLocalSlots(tid, SlotsUse::SplitMiss, cursor);
        }
        if (finishSS && siTail) {
            tNSN -= 1;
        }
        for (; index < tNSN; index++) {
            assert(curCycleRow[cursor + index] == SlotsUse::Base ||
                    curCycleRow[cursor + index] == SlotsUse::NotInitiated);
            curCycleRow[cursor + index] = Referenced;
        }

        DPRINTF(SlotConsume, "curCycleRow after set referenced:\n");
        slotCounter->printSlotRow(curCycleRow, stageWidth);

        bool allProc = true;
        for (int i = 0; cursor + index + i < stageWidth; i++) {
            if (curCycleRow[cursor + index + i] == Base) {
                allProc = false;
            }
        }
        DPRINTF(SlotConsume, "all processed:%i\n", allProc);
        if (!allProc && !siTailNext) {
            for (int i = cursor; i < cursor + tNSN; i++) {
                DPRINTF(SlotConsume, "Setting slot[%i] to Referenced\n", i);
                skidSlotBuffer.front()[i] = Referenced;
            }
        }

        if (SI) {
            slotCounter->incLocalSlots(tid, SlotsUse::SerializeMiss,
                                       stageWidth - toNextStageNum[tid] - cursor);
            return;

        } else if (fullSource == FullSource::NONE) {
            for (int i = cursor + toNextStageNum[tid]; i < stageWidth; i++) {
                slotCounter->incLocalSlots(tid, curCycleRow[i], 1);
            }
            return;
        } else {
            blockedSlots = stageWidth - toNextStageNum[tid] - cursor;
        }
    } else {
        if (localSlotIndex[tid] == stageWidth) {
            for (int i = 0; i < stageWidth; i++) {
                slotCounter->incLocalSlots(tid, slotConsumption[tid][i], 1);
            }
            return;
        }
        assert(localSlotIndex[tid] == 0);
        if (SI) {
            slotCounter->incLocalSlots(tid, SlotsUse::SerializeMiss, stageWidth);
            return;
        } else if (fullSource == FullSource::NONE) {
            for (int i = 0; i < stageWidth; i++) {
                slotCounter->incLocalSlots(tid, curCycleRow[i], 1);
            }
            return;
        } else {
            DPRINTF(SlotConsume, "fullSource: %i\n", fullSource);
        }
        blockedSlots = stageWidth;
    }

    if (isRename) { // rename stage
        if (fullSource == FullSource::IEWStage) {
            if (BLB_in) {
                slotCounter->incLocalSlots(tid, LaterWait, blockedSlots);
                BLB_out = true;
            } else {
                slotCounter->incLocalSlots(tid, LaterMiss, blockedSlots);
            }
        } else if (fullSource == FullSource::Register) {
            slotCounter->incLocalSlots(tid, EntryMiss, blockedSlots);
        } else if (fullSource == FullSource::ROB) {
            if (considerHeadStatus && queueHeadState[tid][ROB] == Normal) {
                slotCounter->incLocalSlots(tid, ROBWait, blockedSlots);
                ROBWait_HeadNotMiss[tid] += blockedSlots;
                BLB_out = true;

            } else if (queueHeadState[tid][ROB] == HeadInstrState::L1DCacheWait) {
                slotCounter->incLocalSlots(tid, L1DCacheInterference, blockedSlots);
                BLB_out = true;

            } else if (queueHeadState[tid][ROB] == HeadInstrState::L2DCacheWait) {
                slotCounter->incLocalSlots(tid, L2DCacheInterference, blockedSlots);
                BLB_out = true;

            } else { // head inst in ROB is DCache Miss
                assert(queueHeadState[tid][ROB] != NoState);
                if (vqState[tid][ROB] == VQNotFull) {
                    slotCounter->incLocalSlots(tid, ROBWait, blockedSlots);
                    ROBWait_VQNotFull[tid] += blockedSlots;
                    BLB_out = true;
                } else {
                    slotCounter->incLocalSlots(tid, ROBMiss, blockedSlots);
                }
                assert(vqState[tid][ROB] != NoVQ);
            }
        }
    } else { // IEW stage
        assert(fullSource == FullSource::IQ ||
               fullSource == FullSource::LQ ||
               fullSource == FullSource::SQ ||
               fullSource == FullSource::CommitStage);

        if (fullSource == FullSource::CommitStage) {
            slotCounter->incLocalSlots(tid, SquashMiss, blockedSlots);
        } else {
            // trick depends on sequence of enum definition to avoid duplication
            int distance_to_iq = fullSource - IQ;
            if (queueHeadState[tid][fullSource] == HeadInstrState::L1DCacheWait) {
                slotCounter->incLocalSlots(tid, L1DCacheInterference, blockedSlots);
                BLB_out = true;

            } else if (queueHeadState[tid][fullSource] == HeadInstrState::L2DCacheWait) {
                slotCounter->incLocalSlots(tid, L2DCacheInterference, blockedSlots);
                BLB_out = true;

            } else { // head inst in queue is DCache Miss
                assert(queueHeadState[tid][fullSource] != NoState);
                if (vqState[tid][fullSource] == VQNotFull) {
                    slotCounter->incLocalSlots(
                            tid, static_cast<SlotsUse>(IQWait + 2*distance_to_iq + 0),
                            blockedSlots);
                    BLB_out = true;
                } else {
                    slotCounter->incLocalSlots(
                            tid, static_cast<SlotsUse>(IQWait + 2*distance_to_iq + 1),
                            blockedSlots);
                }
                assert(vqState[tid][fullSource] != NoVQ);
            }
        }
    }

    BLB_out = BLB_out && blockedSlots + blockCounter[tid] >= stageWidth;
    blockCounter[tid] = blockedSlots + blockCounter[tid] % stageWidth;
}

template<class Impl>
void SlotConsumer<Impl>::
regStats()
{
    using namespace Stats;
    ROBWait_HeadNotMiss
            .init((size_type) numThreads)
            .name(name() + ".ROBWait_HeadNotMiss")
            .desc("ROBWait_HeadNotMiss")
            ;

    ROBWait_VQNotFull
            .init((size_type) numThreads)
            .name(name() + ".ROBWait_VQNotFull")
            .desc("ROBWait_VQNotFull")
            ;
}


#endif
