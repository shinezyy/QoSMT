/*
 * Copyright (c) 2010-2013 ARM Limited
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Kevin Lim
 */

#ifndef __CPU_O3_IEW_IMPL_IMPL_HH__
#define __CPU_O3_IEW_IMPL_IMPL_HH__

// @todo: Fix the instantaneous communication among all the stages within
// iew.  There's a clear delay between issue and execute, yet backwards
// communication happens simultaneously.

#include <queue>

#include "arch/utility.hh"
#include "config/the_isa.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/o3/fu_pool.hh"
#include "cpu/o3/iew.hh"
#include "cpu/timebuf.hh"
#include "mem/cache/miss_table.hh"
#include "debug/Activity.hh"
#include "debug/Drain.hh"
#include "debug/IEW.hh"
#include "debug/O3PipeView.hh"
#include "debug/Pard.hh"
#include "debug/FmtSlot.hh"
#include "debug/FmtSlot2.hh"
#include "debug/BMT.hh"
#include "params/DerivO3CPU.hh"

using namespace std;

template<class Impl>
DefaultIEW<Impl>::DefaultIEW(O3CPU *_cpu, DerivO3CPUParams *params)
    : SlotCounter<Impl>(params, params->dispatchWidth),
      issueToExecQueue(params->backComSize, params->forwardComSize),
      cpu(_cpu),
      instQueue(_cpu, this, params),
      ldstQueue(_cpu, this, params),
      fuPool(params->fuPool),
      commitToIEWDelay(params->commitToIEWDelay),
      renameToIEWDelay(params->renameToIEWDelay),
      fetchToIEWDelay(params->fetchToDecodeDelay),
      issueToExecuteDelay(params->issueToExecuteDelay),
      dispatchWidth(params->dispatchWidth),
      issueWidth(params->issueWidth),
      wbWidth(params->wbWidth),
      numThreads(params->numThreads),
      Programmable(params->iewProgrammable),
      hptInitDispatchWidth(params->hptInitDispatchWidth),
      BLBlocal(false),
      l1Lat(params->l1Lat),
      localInstMiss(0)
{
    if (dispatchWidth > Impl::MaxWidth)
        fatal("dispatchWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             dispatchWidth, static_cast<int>(Impl::MaxWidth));
    if (issueWidth > Impl::MaxWidth)
        fatal("issueWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             issueWidth, static_cast<int>(Impl::MaxWidth));
    if (wbWidth > Impl::MaxWidth)
        fatal("wbWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             wbWidth, static_cast<int>(Impl::MaxWidth));

    _status = Active;
    exeStatus = Running;
    wbStatus = Idle;

    // Setup wire to read instructions coming from issue.
    fromIssue = issueToExecQueue.getWire(-issueToExecuteDelay);

    // Instruction queue needs the queue between issue and execute.
    instQueue.setIssueToExecuteQueue(&issueToExecQueue);

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispatchStatus[tid] = Running;
        fetchRedirect[tid] = false;
        dispatchWidths[tid] = dispatchWidth/numThreads;
    }

    if (hptInitDispatchWidth != 0) {
        // overwrite assignments to DisWidthin last for statment
        dispatchWidths[0] = hptInitDispatchWidth;
        for (ThreadID tid = 1; tid < numThreads; tid++) {
            dispatchWidths[tid] =
                (dispatchWidth - hptInitDispatchWidth)/(numThreads - 1);
        }
    }

    updateLSQNextCycle = false;

    skidBufferMax = (renameToIEWDelay + 1) * params->renameWidth;
}

template <class Impl>
std::string
DefaultIEW<Impl>::name() const
{
    return cpu->name() + ".iew";
}

template <class Impl>
void
DefaultIEW<Impl>::regProbePoints()
{
    ppDispatch = new ProbePointArg<DynInstPtr>(cpu->getProbeManager(), "Dispatch");
    ppMispredict = new ProbePointArg<DynInstPtr>(cpu->getProbeManager(), "Mispredict");
}

template <class Impl>
void
DefaultIEW<Impl>::regStats()
{
    using namespace Stats;
    SlotCounter<Impl>::regStats();

    instQueue.regStats();
    ldstQueue.regStats();

    iewIdleCycles
        .init(numThreads)
        .name(name() + ".iewIdleCycles")
        .desc("Number of cycles IEW is idle");

    iewSquashCycles
        .init(numThreads)
        .name(name() + ".iewSquashCycles")
        .desc("Number of cycles IEW is squashing");

    iewBlockCycles
        .init(numThreads)
        .name(name() + ".iewBlockCycles")
        .desc("Number of cycles IEW is blocking");

    iewUnblockCycles
        .init(numThreads)
        .name(name() + ".iewUnblockCycles")
        .desc("Number of cycles IEW is unblocking");

    iewRunCycles
        .init(numThreads)
        .name(name() + ".iewRunCycles")
        .desc("Number of cycles IEW is Running");

    iewDispatchedInsts
        .name(name() + ".iewDispatchedInsts")
        .desc("Number of instructions dispatched to IQ")
        .flags(display);

    iewDispSquashedInsts
        .name(name() + ".iewDispSquashedInsts")
        .desc("Number of squashed instructions skipped by dispatch");

    iewDispLoadInsts
        .name(name() + ".iewDispLoadInsts")
        .desc("Number of dispatched load instructions");

    iewDispStoreInsts
        .name(name() + ".iewDispStoreInsts")
        .desc("Number of dispatched store instructions");

    iewDispNonSpecInsts
        .name(name() + ".iewDispNonSpecInsts")
        .desc("Number of dispatched non-speculative instructions");

    iewIQFullEvents
        .name(name() + ".iewIQFullEvents")
        .desc("Number of times the IQ has become full, causing a stall");

    iewIQFullEventsPerThread
        .init(cpu->numThreads)
        .name(name() + ".iewIQFullEventsPerThread")
        .desc("Number of times each thread's IQ has become full");

    iewLSQFullEvents
        .name(name() + ".iewLSQFullEvents")
        .desc("Number of times the LSQ has become full, causing a stall");

    memOrderViolationEvents
        .name(name() + ".memOrderViolationEvents")
        .desc("Number of memory order violations");

    predictedTakenIncorrect
        .init(cpu->numThreads)
        .name(name() + ".predictedTakenIncorrect")
        .desc("Number of branches that were predicted taken incorrectly")
        .flags(total)
        ;

    predictedNotTakenIncorrect
        .init(cpu->numThreads)
        .name(name() + ".predictedNotTakenIncorrect")
        .desc("Number of branches that were predicted not taken incorrectly")
        .flags(total)
        ;

    branchMispredicts
        .name(name() + ".branchMispredicts")
        .desc("Number of branch mispredicts detected at execute")
        .flags(total)
        ;

    branchMispredicts = predictedTakenIncorrect + predictedNotTakenIncorrect;

    iewExecutedInsts
        .name(name() + ".iewExecutedInsts")
        .desc("Number of executed instructions");

    iewExecLoadInsts
        .init(cpu->numThreads)
        .name(name() + ".iewExecLoadInsts")
        .desc("Number of load instructions executed")
        .flags(total);

    iewExecSquashedInsts
        .name(name() + ".iewExecSquashedInsts")
        .desc("Number of squashed instructions skipped in execute");

    iewExecutedSwp
        .init(cpu->numThreads)
        .name(name() + ".exec_swp")
        .desc("number of swp insts executed")
        .flags(total);

    iewExecutedNop
        .init(cpu->numThreads)
        .name(name() + ".exec_nop")
        .desc("number of nop insts executed")
        .flags(total);

    iewExecutedRefs
        .init(cpu->numThreads)
        .name(name() + ".exec_refs")
        .desc("number of memory reference insts executed")
        .flags(total);

    iewExecutedBranches
        .init(cpu->numThreads)
        .name(name() + ".exec_branches")
        .desc("Number of branches executed")
        .flags(total);

    iewExecStoreInsts
        .name(name() + ".exec_stores")
        .desc("Number of stores executed")
        .flags(total);
    iewExecStoreInsts = iewExecutedRefs - iewExecLoadInsts;

    iewExecRate
        .name(name() + ".exec_rate")
        .desc("Inst execution rate")
        .flags(total);

    iewExecRate = iewExecutedInsts / cpu->numCycles;

    iewInstsToCommit
        .init(cpu->numThreads)
        .name(name() + ".wb_sent")
        .desc("cumulative count of insts sent to commit")
        .flags(total);

    writebackCount
        .init(cpu->numThreads)
        .name(name() + ".wb_count")
        .desc("cumulative count of insts written-back")
        .flags(total);

    producerInst
        .init(cpu->numThreads)
        .name(name() + ".wb_producers")
        .desc("num instructions producing a value")
        .flags(total);

    consumerInst
        .init(cpu->numThreads)
        .name(name() + ".wb_consumers")
        .desc("num instructions consuming a value")
        .flags(total);

    wbPenalized
        .init(cpu->numThreads)
        .name(name() + ".wb_penalized")
        .desc("number of instrctions required to write to 'other' IQ")
        .flags(total);

    wbPenalizedRate
        .name(name() + ".wb_penalized_rate")
        .desc ("fraction of instructions written-back that wrote to 'other' IQ")
        .flags(total);

    wbPenalizedRate = wbPenalized / writebackCount;

    wbFanout
        .name(name() + ".wb_fanout")
        .desc("average fanout of values written-back")
        .flags(total);

    wbFanout = producerInst / consumerInst;

    wbRate
        .name(name() + ".wb_rate")
        .desc("insts written-back per cycle")
        .flags(total);
    wbRate = writebackCount / cpu->numCycles;

    rectifiedWaits
        .init(cpu->numThreads)
        .name(name() + ".rectified_waits")
        .desc("Numbers of slots rectified from miss to wait.")
        .flags(display)
        ;

    numConcerned
        .init(cpu->numThreads)
        .name(name() + ".numConcerned")
        .desc("Number of concerned instructions writeback.")
        ;

    overallWaitWBCycle
        .init(cpu->numThreads)
        .name(name() + ".overallWaitWBCycle")
        .desc("Sum of scheduled wait cycles.")
        ;

    avgWaitWBCycle
        .name(name() + ".avgWaitWBCycle")
        .desc("Average of scheduled wait cycles.")
        ;

    avgWaitWBCycle = overallWaitWBCycle / numConcerned;
}

template<class Impl>
void
DefaultIEW<Impl>::startupStage()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {

        toRename->iewInfo[tid].usedIQ = true;
        toRename->iewInfo[tid].freeIQEntries = instQueue.numFreeEntries(tid);
        toRename->iewInfo[tid].maxIQEntries = instQueue.maxEntries[tid];

        toRename->iewInfo[tid].usedLSQ = true;

        toRename->iewInfo[tid].freeLQEntries = ldstQueue.numFreeLoadEntries(tid);
        toRename->iewInfo[tid].freeSQEntries = ldstQueue.numFreeStoreEntries(tid);

        toRename->iewInfo[tid].maxLQEntries = ldstQueue.maxLQEntries[tid];
        toRename->iewInfo[tid].maxSQEntries = ldstQueue.maxSQEntries[tid];

        toRename->iewInfo[tid].busyIQEntries = instQueue.numBusyEntries(tid);
        toRename->iewInfo[tid].busyLQEntries = ldstQueue.numLoads(tid);
        toRename->iewInfo[tid].busySQEntries = ldstQueue.numStores(tid);

        toRename->iewInfo[tid].dispatchWidth = dispatchWidths[tid];
    }

    clearFull();

    // Initialize the checker's dcache port here
    if (cpu->checker) {
        cpu->checker->setDcachePort(&cpu->getDataPort());
    }

    cpu->activateStage(O3CPU::IEWIdx);
}

template<class Impl>
void
DefaultIEW<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from commit.
    fromCommit = timeBuffer->getWire(-commitToIEWDelay);

    // Setup wire to write information back to previous stages.
    toRename = timeBuffer->getWire(0);

    toFetch = timeBuffer->getWire(0);

    // Instruction queue also needs main time buffer.
    instQueue.setTimeBuffer(tb_ptr);
}

template<class Impl>
void
DefaultIEW<Impl>::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr)
{
    fetchQueue = fq_ptr;

    // Setup wire to read information from fetch queue.
    fromFetch = fetchQueue->getWire(-fetchToIEWDelay);
}

template<class Impl>
void
DefaultIEW<Impl>::setRenameQueue(TimeBuffer<RenameStruct> *rq_ptr)
{
    renameQueue = rq_ptr;

    // Setup wire to read information from rename queue.
    fromRename = renameQueue->getWire(-renameToIEWDelay);
}

template<class Impl>
void
DefaultIEW<Impl>::setIEWQueue(TimeBuffer<IEWStruct> *iq_ptr)
{
    iewQueue = iq_ptr;

    // Setup wire to write instructions to commit.
    toCommit = iewQueue->getWire(0);
}

template<class Impl>
void
DefaultIEW<Impl>::setActiveThreads(list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;

    ldstQueue.setActiveThreads(at_ptr);
    instQueue.setActiveThreads(at_ptr);
}

template<class Impl>
void
DefaultIEW<Impl>::setScoreboard(Scoreboard *sb_ptr)
{
    scoreboard = sb_ptr;
}

template <class Impl>
bool
DefaultIEW<Impl>::isDrained() const
{
    bool drained = ldstQueue.isDrained() && instQueue.isDrained();

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!insts[tid].empty()) {
            DPRINTF(Drain, "%i: Insts not empty.\n", tid);
            drained = false;
        }
        if (!skidBuffer[tid].empty()) {
            DPRINTF(Drain, "%i: Skid buffer not empty.\n", tid);
            drained = false;
        }
    }

    // Also check the FU pool as instructions are "stored" in FU
    // completion events until they are done and not accounted for
    // above
    if (drained && !fuPool->isDrained()) {
        DPRINTF(Drain, "FU pool still busy.\n");
        drained = false;
    }

    return drained;
}

template <class Impl>
void
DefaultIEW<Impl>::drainSanityCheck() const
{
    assert(isDrained());

    instQueue.drainSanityCheck();
    ldstQueue.drainSanityCheck();
}

template <class Impl>
void
DefaultIEW<Impl>::takeOverFrom()
{
    // Reset all state.
    _status = Active;
    exeStatus = Running;
    wbStatus = Idle;

    instQueue.takeOverFrom();
    ldstQueue.takeOverFrom();
    fuPool->takeOverFrom();

    startupStage();
    cpu->activityThisCycle();

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispatchStatus[tid] = Running;
        fetchRedirect[tid] = false;
    }

    updateLSQNextCycle = false;

    for (int i = 0; i < issueToExecQueue.getSize(); ++i) {
        issueToExecQueue.advance();
    }
}

template<class Impl>
void
DefaultIEW<Impl>::squash(ThreadID tid)
{
    DPRINTF(IEW, "[tid:%i]: Squashing all instructions.\n", tid);

    // Tell the IQ to start squashing.
    instQueue.squash(tid);

    // Tell the LDSTQ to start squashing.
    ldstQueue.squash(fromCommit->commitInfo[tid].doneSeqNum, tid);
    updatedQueues = true;

    BLBlocal = tid == 0 ? false : BLBlocal;

    // Clear the skid buffer in case it has any data in it.
    DPRINTF(IEW, "[tid:%i]: Removing skidbuffer instructions until [sn:%i].\n",
            tid, fromCommit->commitInfo[tid].doneSeqNum);

    while (!skidBuffer[tid].empty()) {
        if (skidBuffer[tid].front()->isLoad()) {
            toRename->iewInfo[tid].dispatchedToLQ++;
        }
        if (skidBuffer[tid].front()->isStore()) {
            toRename->iewInfo[tid].dispatchedToSQ++;
        }

        toRename->iewInfo[tid].dispatched++;

        skidBuffer[tid].pop();
    }

    emptyRenameInsts(tid);

    if (HPT == tid) {
        shine("squash");
        toRename->iewInfo[HPT].shine = true;
    }
}

template<class Impl>
void
DefaultIEW<Impl>::squashDueToBranch(DynInstPtr &inst, ThreadID tid)
{
    DPRINTF(IEW, "[tid:%i]: Squashing from a specific instruction, PC: %s "
            "[sn:%i].\n", tid, inst->pcState(), inst->seqNum);

    // For convenience, I resolve mispredicted branch here
    fmt->resolveBranch(false, inst, tid);
    inst->setMispred();

    if (!toCommit->squash[tid] ||
            inst->seqNum < toCommit->squashedSeqNum[tid]) {
        toCommit->squash[tid] = true;
        toCommit->squashedSeqNum[tid] = inst->seqNum;
        toCommit->branchTaken[tid] = inst->pcState().branching();

        TheISA::PCState pc = inst->pcState();
        TheISA::advancePC(pc, inst->staticInst);

        toCommit->pc[tid] = pc;
        toCommit->mispredictInst[tid] = inst;
        toCommit->includeSquashInst[tid] = false;

        wroteToTimeBuffer = true;
    }

}

template<class Impl>
void
DefaultIEW<Impl>::squashDueToMemOrder(DynInstPtr &inst, ThreadID tid)
{
    DPRINTF(IEW, "[tid:%i]: Memory violation, squashing violator and younger "
            "insts, PC: %s [sn:%i].\n", tid, inst->pcState(), inst->seqNum);
    // Need to include inst->seqNum in the following comparison to cover the
    // corner case when a branch misprediction and a memory violation for the
    // same instruction (e.g. load PC) are detected in the same cycle.  In this
    // case the memory violator should take precedence over the branch
    // misprediction because it requires the violator itself to be included in
    // the squash.
    if (!toCommit->squash[tid] ||
            inst->seqNum <= toCommit->squashedSeqNum[tid]) {
        toCommit->squash[tid] = true;

        toCommit->squashedSeqNum[tid] = inst->seqNum;
        toCommit->pc[tid] = inst->pcState();
        toCommit->mispredictInst[tid] = NULL;

        // Must include the memory violator in the squash.
        toCommit->includeSquashInst[tid] = true;

        wroteToTimeBuffer = true;
    }
}

template<class Impl>
void
DefaultIEW<Impl>::block(ThreadID tid)
{
    DPRINTF(IEW, "[tid:%u]: Blocking.\n", tid);

    if (dispatchStatus[tid] != Blocked &&
        dispatchStatus[tid] != Unblocking) {
        toRename->iewBlock[tid] = true;
        wroteToTimeBuffer = true;

        if (tid == HPT && instQueue.isFull(HPT) &&
                instQueue.numBusyEntries(LPT) > 0) {
            if (!fromRename->genShadow) {
                toRename->iewInfo[HPT].genShadow = true;
            }
            if (!inShadow) {
                genShadow();
            } else {
                shine("Blocking again");
            }
        }
    }

    // Add the current inputs to the skid buffer so they can be
    // reprocessed when this stage unblocks.
    skidInsert(tid);

    dispatchStatus[tid] = Blocked;
    BLBlocal = tid == HPT ? LB_all : BLBlocal;
}

template<class Impl>
void
DefaultIEW<Impl>::unblock(ThreadID tid)
{
    DPRINTF(IEW, "[tid:%i]: Reading instructions out of the skid "
            "buffer %u.\n",tid, tid);

    // If the skid buffer is empty, signal back to previous stages to unblock.
    // Also switch status to running.
    if (skidBuffer[tid].empty()) {
        toRename->iewUnblock[tid] = true;
        BLBlocal = tid == HPT ? false : BLBlocal;
        wroteToTimeBuffer = true;
        DPRINTF(IEW, "[tid:%i]: Done unblocking.\n",tid);
        dispatchStatus[tid] = Running;
        if (HPT == tid) {
            vsq = 0;
        }
    }
}

template<class Impl>
void
DefaultIEW<Impl>::wakeDependents(DynInstPtr &inst)
{
    instQueue.wakeDependents(inst);
}

template<class Impl>
void
DefaultIEW<Impl>::rescheduleMemInst(DynInstPtr &inst)
{
    instQueue.rescheduleMemInst(inst);
}

template<class Impl>
void
DefaultIEW<Impl>::replayMemInst(DynInstPtr &inst)
{
    instQueue.replayMemInst(inst);
}

template<class Impl>
void
DefaultIEW<Impl>::blockMemInst(DynInstPtr& inst)
{
    instQueue.blockMemInst(inst);
}

template<class Impl>
void
DefaultIEW<Impl>::cacheUnblocked()
{
    instQueue.cacheUnblocked();
}

template<class Impl>
void
DefaultIEW<Impl>::instToCommit(DynInstPtr &inst)
{
    // This function should not be called after writebackInsts in a
    // single cycle.  That will cause problems with an instruction
    // being added to the queue to commit without being processed by
    // writebackInsts prior to being sent to commit.

    // First check the time slot that this instruction will write
    // to.  If there are free write ports at the time, then go ahead
    // and write the instruction to that time.  If there are not,
    // keep looking back to see where's the first time there's a
    // free slot.
    while ((*iewQueue)[wbCycle].insts[wbNumInst]) {
        ++wbNumInst;
        if (wbNumInst == wbWidth) {
            ++wbCycle;
            wbNumInst = 0;
        }
    }

    DPRINTF(IEW, "Current wb cycle: %i, width: %i, numInst: %i\nwbActual:%i\n",
            wbCycle, wbWidth, wbNumInst, wbCycle * wbWidth + wbNumInst);
    // Add finished instruction to queue to commit.
    (*iewQueue)[wbCycle].insts[wbNumInst] = inst;
    (*iewQueue)[wbCycle].size++;

    if (inst->concerned) {
        numConcerned[inst->threadNumber]++;
        overallWaitWBCycle[inst->threadNumber] += wbCycle;
    }
}

template <class Impl>
unsigned
DefaultIEW<Impl>::validInstsFromRename()
{
    unsigned inst_count = 0;

    for (int i=0; i<fromRename->size; i++) {
        if (!fromRename->insts[i]->isSquashed())
            inst_count++;
    }

    return inst_count;
}

template<class Impl>
void
DefaultIEW<Impl>::skidInsert(ThreadID tid)
{
    DynInstPtr inst = NULL;

    while (!insts[tid].empty()) {
        inst = insts[tid].front();

        insts[tid].pop();

        DPRINTF(IEW,"[tid:%i]: Inserting [sn:%lli] PC:%s into "
                "dispatch skidBuffer %i\n",tid, inst->seqNum,
                inst->pcState(),tid);

        /**这条指令即将被阻塞，说明它提前到达此阶段也无法提前被处理*/
        if (inst->getWaitSlot() > 0 && !skidBuffer[tid].empty()) {
            this->reshape(inst);
        }

        skidBuffer[tid].push(inst);
    }

    assert(skidBuffer[tid].size() <= skidBufferMax &&
           "Skidbuffer Exceeded Max Size");
}

template<class Impl>
int
DefaultIEW<Impl>::skidCount()
{
    int max=0;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;
        unsigned thread_count = skidBuffer[tid].size();
        if (max < thread_count)
            max = thread_count;
    }

    return max;
}

template<class Impl>
bool
DefaultIEW<Impl>::skidsEmpty()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!skidBuffer[tid].empty())
            return false;
    }

    return true;
}

template <class Impl>
void
DefaultIEW<Impl>::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (dispatchStatus[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // If there are no ready instructions waiting to be scheduled by the IQ,
    // and there's no stores waiting to write back, and dispatch is not
    // unblocking, then there is no internal activity for the IEW stage.
    instQueue.intInstQueueReads++;
    if (_status == Active && !instQueue.hasReadyInsts() &&
        !ldstQueue.willWB() && !any_unblocking) {
        DPRINTF(IEW, "IEW switching to idle\n");

        deactivateStage();

        _status = Inactive;
    } else if (_status == Inactive && (instQueue.hasReadyInsts() ||
                                       ldstQueue.willWB() ||
                                       any_unblocking)) {
        // Otherwise there is internal activity.  Set to active.
        DPRINTF(IEW, "IEW switching to active\n");

        activateStage();

        _status = Active;
    }
}

template <class Impl>
void
DefaultIEW<Impl>::resetEntries()
{
    instQueue.resetEntries();
    ldstQueue.resetEntries();
}

template <class Impl>
bool
DefaultIEW<Impl>::checkStall(ThreadID tid)
{
    bool ret_val(false);

    unsigned numAvailInsts = 0;
    if (tid == HPT) {
        switch (dispatchStatus[tid]) {
            //这里的状态是周期开始时的状态
            case Running:
            case Idle:
                numAvailInsts = insts[tid].size();
                break;
            case Blocked:
            case Unblocking:
                numAvailInsts = skidBuffer[tid].size();
                break;
            default:
                break;
        }
        numLPTcause = std::min(numAvailInsts, dispatchWidth);
        DPRINTF(FmtSlot2, "T[0]: %i insts in skidbuffer, %i insts from Rename\n",
                skidBuffer[tid].size(), insts[tid].size());
    }

    if (fromCommit->commitInfo[tid].robSquashing) {
        DPRINTF(IEW,"[tid:%i]: Stall from Commit stage detected.\n",tid);
        ret_val = true;
    } else if (instQueue.isFull(tid)) {
        if (tid == HPT) {
            LB_all = instQueue.numBusyEntries(LPT) >= numLPTcause;
            LB_part = !LB_all && instQueue.numBusyEntries(LPT) > 0;
            /**TODO检查后面用到这个numLPTcause的情况，
             * 以确定应该使用dispatchWidth，还是dispatchWidths
             */
            numLPTcause = std::min((int) instQueue.numBusyEntries(LPT), numLPTcause);
            DPRINTF(FmtSlot2, "HPT Stall: IQ  is full.\n",tid);
        }
        DPRINTF(IEW,"[tid:%i]: Stall: IQ  is full.\n",tid);
        ret_val = true;
    }

    return ret_val;
}

template <class Impl>
void
DefaultIEW<Impl>::checkSignalsAndUpdate(ThreadID tid)
{
    // Check if there's a squash signal, squash if there is
    // Check stall signals, block if there is.
    // If status was Blocked
    //     if so then go to unblocking
    // If status was Squashing
    //     check if squashing is not high.  Switch to running this cycle.

    if (fromCommit->commitInfo[tid].squash) {
        squash(tid);

        if (dispatchStatus[tid] == Blocked ||
            dispatchStatus[tid] == Unblocking) {
            toRename->iewUnblock[tid] = true;
            wroteToTimeBuffer = true;
        }

        dispatchStatus[tid] = Squashing;
        fetchRedirect[tid] = false;
        return;
    }

    if (fromCommit->commitInfo[tid].robSquashing) {
        DPRINTF(IEW, "[tid:%i]: ROB is still squashing.\n", tid);

        dispatchStatus[tid] = Squashing;
        emptyRenameInsts(tid);
        wroteToTimeBuffer = true;
    }

    if (checkStall(tid)) {
        // 可能因为rob squash或者IQ full
        block(tid); //如果是HPT被block，那么后面不可能再dispatch

        dispatchStatus[tid] = Blocked;
        return;
    }

    if (dispatchStatus[tid] == Blocked) {
        // Status from previous cycle was blocked, but there are no more stall
        // conditions.  Switch over to unblocking.
        DPRINTF(IEW, "[tid:%i]: Done blocking, switching to unblocking.\n",
                tid);

        dispatchStatus[tid] = Unblocking;

        unblock(tid);

        return;
    }

    if (dispatchStatus[tid] == Squashing) {
        // Switch status to running if rename isn't being told to block or
        // squash this cycle.
        DPRINTF(IEW, "[tid:%i]: Done squashing, switching to running.\n",
                tid);

        dispatchStatus[tid] = Running;

        return;
    }
}

template <class Impl>
void
DefaultIEW<Impl>::sortInsts()
{
    int insts_from_rename = fromRename->size;
#ifdef DEBUG
    for (ThreadID tid = 0; tid < numThreads; tid++)
        assert(insts[tid].empty());
#endif
    for (int i = 0; i < insts_from_rename; ++i) {
        insts[fromRename->insts[i]->threadNumber].push(fromRename->insts[i]);
    }

    storeRate = fromRename->storeRate;
}

template <class Impl>
void
DefaultIEW<Impl>::emptyRenameInsts(ThreadID tid)
{
    DPRINTF(IEW, "[tid:%i]: Removing incoming rename instructions\n", tid);

    while (!insts[tid].empty()) {

        if (insts[tid].front()->isLoad()) {
            toRename->iewInfo[tid].dispatchedToLQ++;
        }
        if (insts[tid].front()->isStore()) {
            toRename->iewInfo[tid].dispatchedToSQ++;
        }

        toRename->iewInfo[tid].dispatched++;

        insts[tid].pop();
    }
}

template <class Impl>
void
DefaultIEW<Impl>::wakeCPU()
{
    cpu->wakeCPU();
}

template <class Impl>
void
DefaultIEW<Impl>::activityThisCycle()
{
    DPRINTF(Activity, "Activity this cycle.\n");
    cpu->activityThisCycle();
}

template <class Impl>
inline void
DefaultIEW<Impl>::activateStage()
{
    DPRINTF(Activity, "Activating stage.\n");
    cpu->activateStage(O3CPU::IEWIdx);
}

template <class Impl>
inline void
DefaultIEW<Impl>::deactivateStage()
{
    DPRINTF(Activity, "Deactivating stage.\n");
    cpu->deactivateStage(O3CPU::IEWIdx);
}

template<class Impl>
void
DefaultIEW<Impl>::dispatch(ThreadID tid)
{
    // If status is Running or idle,
    //     call dispatchInsts()
    // If status is Unblocking,
    //     buffer any instructions coming from rename
    //     continue trying to empty skid buffer
    //     check if stall conditions have passed

    // 计算每个线程可以dispatch的指令数

    DPRINTF(FmtSlot, "Dispatch thread(%d)\n", tid);

    if (dispatchStatus[tid] == Blocked) {
        ++iewBlockCycles[tid];
        DPRINTF(FmtSlot, "Recording miss slots when T[%d] is blocked.\n", tid);

    } else if (dispatchStatus[tid] == Squashing) {
        ++iewSquashCycles[tid];
        DPRINTF(FmtSlot, "Recording miss slots when T[%d] is squashing.\n", tid);
    }

    // Dispatch should try to dispatch as many instructions as its bandwidth
    // will allow, as long as it is not currently blocked.
    if (dispatchStatus[tid] == Running ||
        dispatchStatus[tid] == Idle) {
        DPRINTF(IEW, "[tid:%i] Not blocked, so attempting to run "
                "dispatch.\n", tid);

        dispatchInsts(tid);

        ++iewRunCycles[tid];

    } else if (dispatchStatus[tid] == Unblocking) {
        // Make sure that the skid buffer has something in it if the
        // status is unblocking.
        assert(!skidsEmpty());

        // If the status was unblocking, then instructions from the skid
        // buffer were used.  Remove those instructions and handle
        // the rest of unblocking.
        dispatchInsts(tid);

        ++iewUnblockCycles[tid];

        if (validInstsFromRename()) {
            // Add the current inputs to the skid buffer so they can be
            // reprocessed when this stage unblocks.
            skidInsert(tid);
        }

        unblock(tid);
    }
}

template <class Impl>
void
DefaultIEW<Impl>::dispatchInsts(ThreadID tid)
{
    // Obtain instructions from skid buffer if unblocking, or queue from rename
    // otherwise.
    DPRINTF(FmtSlot, "DispatchInsts(%d)\n", tid);
    std::queue<DynInstPtr> &insts_to_dispatch =
        dispatchStatus[tid] == Unblocking ?
        skidBuffer[tid] : insts[tid];

    int insts_to_add = insts_to_dispatch.size();

    DynInstPtr inst;
    bool add_to_iq = false;
    int dis_num_inst = 0;

    // Loop through the instructions, putting them in the instruction
    // queuedis_num_inst
    for ( ; dis_num_inst < insts_to_add &&
              dis_num_inst < dispatchWidths[tid];
          ++dis_num_inst)
    {
        inst = insts_to_dispatch.front();

        if (dispatchStatus[tid] == Unblocking) {
            DPRINTF(IEW, "[tid:%i]: Issue: Examining instruction from skid "
                    "buffer\n", tid);
        }

        // Make sure there's a valid instruction there.
        assert(inst);

        DPRINTF(IEW, "[tid:%i]: Issue: Adding PC %s [sn:%lli] [tid:%i] to "
                "IQ.\n",
                tid, inst->pcState(), inst->seqNum, inst->threadNumber);

        // Be sure to mark these instructions as ready so that the
        // commit stage can go ahead and execute them, and mark
        // them as issued so the IQ doesn't reprocess them.

        // Check for squashed instructions.
        if (inst->isSquashed()) {
            DPRINTF(IEW, "[tid:%i]: Issue: Squashed instruction encountered, "
                    "not adding to IQ.\n", tid);

            ++iewDispSquashedInsts;

            insts_to_dispatch.pop();

            //Tell Rename That An Instruction has been processed
            if (inst->isLoad()) {
                toRename->iewInfo[tid].dispatchedToLQ++;
            }
            if (inst->isStore()) {
                toRename->iewInfo[tid].dispatchedToSQ++;
            }

            toRename->iewInfo[tid].dispatched++;
            dispatched[tid]++;
            squashed[tid]++;

            if (tid == HPT && !headInst[tid]) {
                headInst[tid] = inst;
            }

            continue;
        }

        // Check for full conditions.
        if (instQueue.isFull(tid)) {
            DPRINTF(IEW, "[tid:%i]: Issue: IQ has become full.\n", tid);

            if (tid == HPT) {
                LB_all = instQueue.numBusyEntries(LPT) >= dispatchable[HPT];
                LB_part = !LB_all && instQueue.numBusyEntries(LPT) > 0;
                numLPTcause = std::min((int) instQueue.numBusyEntries(LPT),
                        dispatchable[HPT]);
            }

            // Call function to start blocking.
            block(tid);

            // Set unblock to false. Special case where we are using
            // skidbuffer (unblocking) instructions but then we still
            // get full in the IQ.
            toRename->iewUnblock[tid] = false;

            ++iewIQFullEvents;
            ++iewIQFullEventsPerThread[tid];

            ++numIQFull[tid];

            break;
        }

        // Check LSQ if inst is LD/ST
        if ((inst->isLoad() && ldstQueue.lqFull(tid)) ||
            (inst->isStore() && ldstQueue.sqFull(tid))) {
            DPRINTF(IEW, "[tid:%i]: Issue: %s has become full.\n",tid,
                    inst->isLoad() ? "LQ" : "SQ");

            if (tid == HPT) {
                if (inst->isLoad() && ldstQueue.numLoads(LPT)) {
                    numLQWait[HPT]++;
                    LB_all = true;
                    /**这里这样其实是认为HPT剩下指令里面没有Load Store*/
                    numLPTcause = dispatchable[HPT];

                } else if (inst->isStore() && ldstQueue.numStores(LPT)) {
                    if (vsq <= 0.001) {
                        vsq = ldstQueue.numStores(HPT);
                    }
                    DPRINTF(Pard, "%llu cycles since oldest Store, vsq is %f\n",
                            (curTick() - missStat.oldestStoreTick[HPT])/500, vsq);
                    bool m = vsq > 63;
                    vsq += storeRate;

                    if (!m) {
                        numSQWait[HPT]++;
                        LB_all = true; // for unblocking
                        LB_part = true;
                        numLPTcause = dispatchable[HPT];

                    } else {
                        LB_all = false;
                        LB_part = false;
                        numLPTcause = 0;

                    }
                } else {
                    LB_all = false;
                    LB_part = false;
                }
            }

            if(inst->isLoad()) {
                ++numLQFull[tid];
            } else {
                ++numSQFull[tid];
            }

            // Call function to start blocking.
            block(tid);

            // Set unblock to false. Special case where we are using
            // skidbuffer (unblocking) instructions but then we still
            // get full in the IQ.
            toRename->iewUnblock[tid] = false;

            ++iewLSQFullEvents;
            break;
        }

        // Otherwise issue the instruction just fine.
        if (inst->isLoad()) {
            DPRINTF(IEW, "[tid:%i]: Issue: Memory instruction "
                    "encountered, adding to LSQ.\n", tid);

            // Reserve a spot in the load store queue for this
            // memory access.
            inst->enLSQTick = curTick();
            ldstQueue.insertLoad(inst);

            ++iewDispLoadInsts;

            add_to_iq = true;

            toRename->iewInfo[tid].dispatchedToLQ++;

            if (HPT == tid && inShadow) {
                DPRINTF(BMT, "Put inst [%llu] into shadow LQ\n", inst->seqNum);
                inst->inShadowLQ = true;
                shadowLQ--;
            }

        } else if (inst->isStore()) {
            DPRINTF(IEW, "[tid:%i]: Issue: Memory instruction "
                    "encountered, adding to LSQ.\n", tid);

            inst->enLSQTick = curTick();
            ldstQueue.insertStore(inst);

            ++iewDispStoreInsts;

            if (inst->isStoreConditional()) {
                // Store conditionals need to be set as "canCommit()"
                // so that commit can process them when they reach the
                // head of commit.
                // @todo: This is somewhat specific to Alpha.
                inst->setCanCommit();
                instQueue.insertNonSpec(inst);
                add_to_iq = false;

                ++iewDispNonSpecInsts;
            } else {
                add_to_iq = true;
            }

            if (HPT == tid && inShadow) {
                DPRINTF(BMT, "Put inst [%llu] into shadow SQ\n", inst->seqNum);
                inst->inShadowSQ = true;
                shadowSQ--;
            }

            toRename->iewInfo[tid].dispatchedToSQ++;
        } else if (inst->isMemBarrier() || inst->isWriteBarrier()) {
            // Same as non-speculative stores.
            inst->setCanCommit();
            instQueue.insertBarrier(inst);
            add_to_iq = false;
        } else if (inst->isNop()) {
            DPRINTF(IEW, "[tid:%i]: Issue: Nop instruction encountered, "
                    "skipping.\n", tid);

            inst->setIssued();
            inst->setExecuted();
            inst->setCanCommit();

            instQueue.recordProducer(inst);

            iewExecutedNop[tid]++;

            add_to_iq = false;
        } else {
            assert(!inst->isExecuted());
            add_to_iq = true;
        }

        if (inst->isNonSpeculative()) {
            DPRINTF(IEW, "[tid:%i]: Issue: Nonspeculative instruction "
                    "encountered, skipping.\n", tid);

            // Same as non-speculative stores.
            inst->setCanCommit();

            // Specifically insert it as nonspeculative.
            instQueue.insertNonSpec(inst);

            ++iewDispNonSpecInsts;

            add_to_iq = false;
        }

        if (inst->isControl()) {
            fmt->addBranch(inst, tid, cpu->localCycles);
        }

        // If the instruction queue is not full, then add the
        // instruction.
        if (add_to_iq) {
            instQueue.insert(inst);

            if (HPT == tid && inShadow) {
                inst->inShadowIQ = true;
                shadowIQ--;
            }
        }

        if (HPT == tid && inShadow) {
            if (shadowIQ <= 0 || shadowLQ <= 0 || shadowSQ <= 0) {
                toRename->iewInfo[HPT].shine = true;
                if (shadowIQ <= 0) {
                    shine("use up IQ");
                } else if (shadowLQ <= 0) {
                    shine("use up LQ");
                } else {
                    shine("use up SQ");
                }
            }
        }

        insts_to_dispatch.pop();

        toRename->iewInfo[tid].dispatched++;

        dispatched[tid]++;

        if (tid == HPT && !headInst[tid]) {
            headInst[tid] = inst;
        }

        // check other threads' status
        if (HPT == tid) {
            DPRINTF(FmtSlot, "Increment 1 base slot of T[%d].\n", tid);
            this->incLocalSlots(HPT, Base, 1);
            fmt->incBaseSlot(inst, HPT, 1);
            dispatchable[HPT]--;
        }

        ++iewDispatchedInsts;

#if TRACING_ON
        inst->dispatchTick = curTick() - inst->fetchTick;
#endif
        ppDispatch->notify(inst);
    }

    if (HPT == tid && dispatchable[HPT] > 0) {

        if (fromRename->frontEndMiss) {
            this->incLocalSlots(tid, InstMiss, dispatchable[HPT]);

        } else if (dispatchStatus[tid] == Blocked && missStat.numL2MissLoad[HPT]) {
            this->incLocalSlots(tid, EntryMiss, dispatchable[HPT]);

        } else if (dispatchStatus[tid] == Blocked && missStat.numL2MissLoad[LPT]) {
            LB_all = true;
            this->incLocalSlots(tid, EntryWait, dispatchable[HPT]);

        }else if (LB_all) {
            this->incLocalSlots(tid, EntryWait, dispatchable[HPT]);

        } else if (LB_part) {
            this->incLocalSlots(tid, ComputeEntryWait, numLPTcause);
            this->incLocalSlots(tid, ComputeEntryMiss,
                    dispatchable[tid] - numLPTcause);

        } else if (dispatchStatus[tid] == Blocked){
            this->incLocalSlots(tid, EntryMiss, dispatchable[tid]);

        } else {
            this->incLocalSlots(tid, WidthWait, dispatchable[tid]);
        }
    }

    if (!insts_to_dispatch.empty()) {


        DPRINTF(IEW,"[tid:%i]: Issue: Bandwidth Full. Blocking.\n", tid);

        block(tid);

        toRename->iewUnblock[tid] = false;
    }

    if (dispatchStatus[tid] == Idle && dis_num_inst) {
        dispatchStatus[tid] = Running;

        updatedQueues = true;
    }
}

template <class Impl>
void
DefaultIEW<Impl>::printAvailableInsts()
{
    int inst = 0;

    std::cout << "Available Instructions: ";

    while (fromIssue->insts[inst]) {

        if (inst%3==0) std::cout << "\n\t";

        std::cout << "PC: " << fromIssue->insts[inst]->pcState()
             << " TN: " << fromIssue->insts[inst]->threadNumber
             << " SN: " << fromIssue->insts[inst]->seqNum << " | ";

        inst++;

    }

    std::cout << "\n";
}

template <class Impl>
void
DefaultIEW<Impl>::executeInsts()
{
    wbNumInst = 0;
    wbCycle = 0;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;
        fetchRedirect[tid] = false;
    }

    // Uncomment this if you want to see all available instructions.
    // @todo This doesn't actually work anymore, we should fix it.
//    printAvailableInsts();

    // Execute/writeback any instructions that are available.
    int insts_to_execute = fromIssue->size;
    int inst_num = 0;
    for (; inst_num < insts_to_execute;
          ++inst_num) {

        DPRINTF(IEW, "Execute: Executing instructions from IQ.\n");

        DynInstPtr inst = instQueue.getInstToExecute();

        DPRINTF(IEW, "Execute: Processing PC %s, [tid:%i] [sn:%i].\n",
                inst->pcState(), inst->threadNumber,inst->seqNum);

        // Check if the instruction is squashed; if so then skip it
        if (inst->isSquashed()) {
            DPRINTF(IEW, "Execute: Instruction was squashed. PC: %s, [tid:%i]"
                         " [sn:%i]\n", inst->pcState(), inst->threadNumber,
                         inst->seqNum);

            // Consider this instruction executed so that commit can go
            // ahead and retire the instruction.
            inst->setExecuted();

            // Not sure if I should set this here or just let commit try to
            // commit any squashed instructions.  I like the latter a bit more.
            inst->setCanCommit();

            ++iewExecSquashedInsts;

            continue;
        }

        Fault fault = NoFault;

        // Execute instruction.
        // Note that if the instruction faults, it will be handled
        // at the commit stage.
        if (inst->isMemRef()) {
            DPRINTF(IEW, "Execute: Calculating address for memory "
                    "reference.\n");

            // Tell the LDSTQ to execute this instruction (if it is a load).
            if (inst->isLoad()) {
                // Loads will mark themselves as executed, and their writeback
                // event adds the instruction to the queue to commit
                fault = ldstQueue.executeLoad(inst);

                if (inst->isTranslationDelayed() &&
                    fault == NoFault) {
                    // A hw page table walk is currently going on; the
                    // instruction must be deferred.
                    DPRINTF(IEW, "Execute: Delayed translation, deferring "
                            "load.\n");
                    instQueue.deferMemInst(inst);
                    continue;
                }

                if (inst->isDataPrefetch() || inst->isInstPrefetch()) {
                    inst->fault = NoFault;
                }
            } else if (inst->isStore()) {
                fault = ldstQueue.executeStore(inst);

                if (inst->isTranslationDelayed() &&
                    fault == NoFault) {
                    // A hw page table walk is currently going on; the
                    // instruction must be deferred.
                    DPRINTF(IEW, "Execute: Delayed translation, deferring "
                            "store.\n");
                    instQueue.deferMemInst(inst);
                    continue;
                }

                // If the store had a fault then it may not have a mem req
                if (fault != NoFault || !inst->readPredicate() ||
                        !inst->isStoreConditional()) {
                    // If the instruction faulted, then we need to send it along
                    // to commit without the instruction completing.
                    // Send this instruction to commit, also make sure iew stage
                    // realizes there is activity.
                    inst->setExecuted();
                    instToCommit(inst);
                    activityThisCycle();
                }

                // Store conditionals will mark themselves as
                // executed, and their writeback event will add the
                // instruction to the queue to commit.
            } else {
                panic("Unexpected memory type!\n");
            }

        } else {
            // If the instruction has already faulted, then skip executing it.
            // Such case can happen when it faulted during ITLB translation.
            // If we execute the instruction (even if it's a nop) the fault
            // will be replaced and we will lose it.
            if (inst->getFault() == NoFault) {
                inst->execute();    // This is where instruction execute
                if (!inst->readPredicate())
                    inst->forwardOldRegs();
            }

            inst->setExecuted();

            instToCommit(inst);
        }

        updateExeInstStats(inst);

        // Check if branch prediction was correct, if not then we need
        // to tell commit to squash in-flight instructions.  Only
        // handle this if there hasn't already been something that
        // redirects fetch in this group of instructions.

        // This probably needs to prioritize the redirects if a different
        // scheduler is used.  Currently the scheduler schedules the oldest
        // instruction first, so the branch resolution order will be correct.
        ThreadID tid = inst->threadNumber;

        if (!fetchRedirect[tid] ||
            !toCommit->squash[tid] ||
            toCommit->squashedSeqNum[tid] > inst->seqNum) {

            // Prevent testing for misprediction on load instructions,
            // that have not been executed.
            bool loadNotExecuted = !inst->isExecuted() && inst->isLoad();

            if (inst->mispredicted() && !loadNotExecuted) {
                fetchRedirect[tid] = true;

                DPRINTF(IEW, "Execute: Branch mispredict detected.\n");
                DPRINTF(IEW, "Predicted target was PC: %s.\n",
                        inst->readPredTarg());
                DPRINTF(IEW, "Execute: Redirecting fetch to PC: %s.\n",
                        inst->pcState());
                // If incorrect, then signal the ROB that it must be squashed.
                squashDueToBranch(inst, tid);

                ppMispredict->notify(inst);

                if (inst->readPredTaken()) {
                    predictedTakenIncorrect[tid]++;
                } else {
                    predictedNotTakenIncorrect[tid]++;
                }
            } else if (ldstQueue.violation(tid)) {
                assert(inst->isMemRef());
                // If there was an ordering violation, then get the
                // DynInst that caused the violation.  Note that this
                // clears the violation signal.
                DynInstPtr violator;
                violator = ldstQueue.getMemDepViolator(tid);

                DPRINTF(IEW, "LDSTQ detected a violation. Violator PC: %s "
                        "[sn:%lli], inst PC: %s [sn:%lli]. Addr is: %#x.\n",
                        violator->pcState(), violator->seqNum,
                        inst->pcState(), inst->seqNum, inst->physEffAddr);

                fetchRedirect[tid] = true;

                // Tell the instruction queue that a violation has occured.
                instQueue.violation(inst, violator);

                // Squash.
                squashDueToMemOrder(violator, tid);

                ++memOrderViolationEvents;
            }
        } else {
            // Reset any state associated with redirects that will not
            // be used.
            if (ldstQueue.violation(tid)) {
                assert(inst->isMemRef());

                DynInstPtr violator = ldstQueue.getMemDepViolator(tid);

                DPRINTF(IEW, "LDSTQ detected a violation.  Violator PC: "
                        "%s, inst PC: %s.  Addr is: %#x.\n",
                        violator->pcState(), inst->pcState(),
                        inst->physEffAddr);
                DPRINTF(IEW, "Violation will not be handled because "
                        "already squashing\n");

                ++memOrderViolationEvents;
            }
        }
    }

    // Update and record activity if we processed any instructions.
    if (inst_num) {
        if (exeStatus == Idle) {
            exeStatus = Running;
        }

        updatedQueues = true;

        cpu->activityThisCycle();
    }

    // Need to reset this in case a writeback event needs to write into the
    // iew queue.  That way the writeback event will write into the correct
    // spot in the queue.
    wbNumInst = 0;

}

template <class Impl>
void
DefaultIEW<Impl>::writebackInsts()
{
    // Loop through the head of the time buffer and wake any
    // dependents.  These instructions are about to write back.  Also
    // mark scoreboard that this instruction is finally complete.
    // Either have IEW have direct access to scoreboard, or have this
    // as part of backwards communication.
    for (int inst_num = 0; inst_num < wbWidth &&
             toCommit->insts[inst_num]; inst_num++) {
        DynInstPtr inst = toCommit->insts[inst_num];
        ThreadID tid = inst->threadNumber;

        DPRINTF(IEW, "Sending instructions to commit, [sn:%lli] PC %s.\n",
                inst->seqNum, inst->pcState());

        iewInstsToCommit[tid]++;

        // Some instructions will be sent to commit without having
        // executed because they need commit to handle them.
        // E.g. Strictly ordered loads have not actually executed when they
        // are first sent to commit.  Instead commit must tell the LSQ
        // when it's ready to execute the strictly ordered load.
        if (!inst->isSquashed() && inst->isExecuted() && inst->getFault() == NoFault) {
            int dependents = instQueue.wakeDependents(inst);

            for (int i = 0; i < inst->numDestRegs(); i++) {
                //mark as Ready
                DPRINTF(IEW,"Setting Destination Register %i\n",
                        inst->renamedDestRegIdx(i));
                scoreboard->setReg(inst->renamedDestRegIdx(i));
            }

            if (dependents) {
                producerInst[tid]++;
                consumerInst[tid]+= dependents;
            }
            writebackCount[tid]++;
        }
    }
}

template<class Impl>
void
DefaultIEW<Impl>::tick()
{
    wbNumInst = 0;
    wbCycle = 0;

    wroteToTimeBuffer = false;
    updatedQueues = false;

    sortInsts();

    if (fromRename->genShadow) {
        genShadow();
    } else if (fromRename->shine) {
        shine("rename shine");
    }

    LBLC = LB_all;

    LB_all = false; // reset it
    LB_part = false; // reset it
    numLPTcause = -1; // -1表示无意义

    std::fill(dispatched.begin(), dispatched.end(), 0);
    std::fill(squashed.begin(), squashed.end(), 0);
    std::fill(headInst.begin(), headInst.end(), nullptr);

    // Free function units marked as being freed this cycle.
    fuPool->processFreeUnits();

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    instQueue.updateMaxEntries();
    ldstQueue.updateMaxEntries();

    // Check stall and squash signals, dispatch any instructions.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {

        DPRINTF(FmtSlot2, "Dispatch: Processing [tid:%i]\n",tid);
        checkSignalsAndUpdate(tid);
    }

    /** missTry();*/

    getDispatchable();

    for (ThreadID tid = 0; tid < numThreads; ++tid) {

        computeMiss(tid);
        dispatch(tid);
    }

    if (this->checkSlots(HPT)) {
        //only inst miss should be counted
        localInstMiss += this->perCycleSlots[HPT][InstMiss];
        this->sumLocalSlots(HPT);
    }

    fmt->incMissDirect(HPT, this->miss[HPT], false);

    if (dispatched[HPT] > 0) {
        /** reshape 保证不会高估wait的数量*/
        DynInstPtr &inst = this->getHeadInst(HPT);

        if (inst->isSquashed()) {
            fmt->incMissDirect(HPT, this->wait[HPT], false);

        } else {
            // this->assignSlots(HPT, inst);
            fmt->incMissDirect(HPT, -(std::min(inst->getWaitSlot(),
                            localInstMiss)), false);
            fmt->incWaitSlot(inst, HPT, std::min(inst->getWaitSlot(),
                        localInstMiss) + this->wait[HPT]);

            localInstMiss = 0;
        }
        this->wait[HPT] = 0;
    }
    this->miss[HPT] = 0;

    if (exeStatus != Squashing) {
        executeInsts();

        writebackInsts();

        // Have the instruction queue try to schedule any ready instructions.
        // (In actuality, this scheduling is for instructions that will
        // be executed next cycle.)
        instQueue.scheduleReadyInsts();

        // Also should advance its own time buffers if the stage ran.
        // Not the best place for it, but this works (hopefully).
        issueToExecQueue.advance();
    }

    bool broadcast_free_entries = false;

    if (updatedQueues || exeStatus == Running || updateLSQNextCycle) {
        exeStatus = Idle;
        updateLSQNextCycle = false;

        broadcast_free_entries = true;
    }

    toRename->iewInfo[0].BLB = (dispatchStatus[HPT] == Blocked && LB_all) ||
        (BLBlocal && dispatchStatus[HPT] == Unblocking);

    if (toRename->iewInfo[0].BLB) {
        DPRINTF(FmtSlot, "Send LPT cause HPT Stall backward to rename.\n");
    }

    // Writeback any stores using any leftover bandwidth.
    ldstQueue.writebackStores();

    // Check the committed load/store signals to see if there's a load
    // or store to commit.  Also check if it's being told to execute a
    // nonspeculative instruction.
    // This is pretty inefficient...

    threads = activeThreads->begin();
    while (threads != end) {
        ThreadID tid = (*threads++);

        DPRINTF(IEW,"Processing [tid:%i]\n",tid);

        // Update structures based on instructions committed.
        if (fromCommit->commitInfo[tid].doneSeqNum != 0 &&
            !fromCommit->commitInfo[tid].squash &&
            !fromCommit->commitInfo[tid].robSquashing) {

            ldstQueue.commitStores(fromCommit->commitInfo[tid].doneSeqNum,tid);

            ldstQueue.commitLoads(fromCommit->commitInfo[tid].doneSeqNum,tid);

            updateLSQNextCycle = true;
            instQueue.commit(fromCommit->commitInfo[tid].doneSeqNum,tid);
        }

        if (fromCommit->commitInfo[tid].nonSpecSeqNum != 0) {

            //DPRINTF(IEW,"NonspecInst from thread %i",tid);
            if (fromCommit->commitInfo[tid].strictlyOrdered) {
                instQueue.replayMemInst(
                    fromCommit->commitInfo[tid].strictlyOrderedLoad);
                fromCommit->commitInfo[tid].strictlyOrderedLoad->setAtCommit();
            } else {
                instQueue.scheduleNonSpec(
                    fromCommit->commitInfo[tid].nonSpecSeqNum);
            }
        }

        if (broadcast_free_entries) {
            toFetch->iewInfo[tid].iqCount =
                instQueue.getCount(tid);
            toFetch->iewInfo[tid].ldstqCount =
                ldstQueue.getCount(tid);

            toRename->iewInfo[tid].usedIQ = true;
            toRename->iewInfo[tid].freeIQEntries =
                instQueue.numFreeEntries(tid);
            toRename->iewInfo[tid].maxIQEntries = instQueue.maxEntries[tid];

            toRename->iewInfo[tid].usedLSQ = true;

            toRename->iewInfo[tid].freeLQEntries =
                ldstQueue.numFreeLoadEntries(tid);
            toRename->iewInfo[tid].freeSQEntries =
                ldstQueue.numFreeStoreEntries(tid);

            toRename->iewInfo[tid].maxLQEntries = ldstQueue.maxLQEntries[tid];
            toRename->iewInfo[tid].maxSQEntries = ldstQueue.maxSQEntries[tid];

            toRename->iewInfo[tid].busyIQEntries = instQueue.numBusyEntries(tid);
            toRename->iewInfo[tid].busyLQEntries = ldstQueue.numLoads(tid);
            toRename->iewInfo[tid].busySQEntries = ldstQueue.numStores(tid);

            toRename->iewInfo[tid].LQHead = ldstQueue.getLoadHeadInst(tid);
            toRename->iewInfo[tid].SQHead = ldstQueue.getStoreHeadInst(tid);

            toRename->iewInfo[tid].dispatchWidth = dispatchWidths[tid];

            wroteToTimeBuffer = true;

        }

        DPRINTF(IEW, "[tid:%i], Dispatch dispatched %i instructions.\n",
                tid, toRename->iewInfo[tid].dispatched);
    }

    DPRINTF(IEW, "IQ has %i free entries (Can schedule: %i).  "
            "LQ has %i free entries. SQ has %i free entries.\n",
            instQueue.numFreeEntries(), instQueue.hasReadyInsts(),
            ldstQueue.numFreeLoadEntries(), ldstQueue.numFreeStoreEntries());

    DPRINTF(IEW, "Thread [0] IQ has %i free entries (Can schedule: %i).  "
            "LQ has %i free entries. SQ has %i free entries.\n",
            instQueue.numFreeEntries(0), instQueue.hasReadyInsts(),
            ldstQueue.numFreeLoadEntries(0), ldstQueue.numFreeStoreEntries(0));

    instQueue.increaseUsedEntries();
    ldstQueue.increaseUsedEntries();

    updateStatus();

    if (wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

}

template <class Impl>
void
DefaultIEW<Impl>::updateExeInstStats(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;

    iewExecutedInsts++;

#if TRACING_ON
    if (DTRACE(O3PipeView)) {
        inst->completeTick = curTick() - inst->fetchTick;
    }
#endif

    //
    //  Control operations
    //
    if (inst->isControl())
        iewExecutedBranches[tid]++;

    //
    //  Memory operations
    //
    if (inst->isMemRef()) {
        iewExecutedRefs[tid]++;

        if (inst->isLoad()) {
            iewExecLoadInsts[tid]++;
        }
    }
}

template <class Impl>
void
DefaultIEW<Impl>::checkMisprediction(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;

    if (!fetchRedirect[tid] ||
        !toCommit->squash[tid] ||
        toCommit->squashedSeqNum[tid] > inst->seqNum) {

        if (inst->mispredicted()) {
            fetchRedirect[tid] = true;

            DPRINTF(IEW, "Execute: Branch mispredict detected.\n");
            DPRINTF(IEW, "Predicted target was PC:%#x, NPC:%#x.\n",
                    inst->predInstAddr(), inst->predNextInstAddr());
            DPRINTF(IEW, "Execute: Redirecting fetch to PC: %#x,"
                    " NPC: %#x.\n", inst->nextInstAddr(),
                    inst->nextInstAddr());
            // If incorrect, then signal the ROB that it must be squashed.
            squashDueToBranch(inst, tid);

            if (inst->readPredTaken()) {
                predictedTakenIncorrect[tid]++;
            } else {
                predictedNotTakenIncorrect[tid]++;
            }
        }
    }
}

template<class Impl>
void
DefaultIEW<Impl>::setFmt(Fmt *_fmt)
{
    fmt = _fmt;
}

template<class Impl>
void
DefaultIEW<Impl>::setVoc(Voc *_voc)
{
    voc = _voc;
}

template<class Impl>
void
DefaultIEW<Impl>::reassignDispatchWidth(int newWidthVec[], int lenWidthVec)
{
    //assert(lenWidthVec == numThreads);
    if (!Programmable) {
        return;
    }

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        dispatchWidths[tid] = newWidthVec[tid];
        toRename->iewInfo[tid].dispatchWidth = dispatchWidths[tid];
    }
}

template<class Impl>
void
DefaultIEW<Impl>::getDispatchable()
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        switch (dispatchStatus[tid]) {
            case Running:
            case Idle:
                dispatchable[tid] = insts[tid].size();
                DPRINTF(FmtSlot2, "T[%i][Running] has %i insts to dispatch\n",
                        tid, dispatchable[tid]);
                break;

            case Blocked:
                dispatchable[tid] = std::min((int) skidBuffer[tid].size(),
                        (int) dispatchWidth);
                DPRINTF(FmtSlot2, "T[%i][Blocked] has %i insts to dispatch\n",
                        tid, dispatchable[tid]);
                break;

            case Unblocking:
                dispatchable[tid] = std::min((int) skidBuffer[tid].size(),
                        (int) dispatchWidth);
                DPRINTF(FmtSlot2, "T[%i][Unblocking] has %i insts to dispatch\n",
                        tid, dispatchable[tid]);
                /**should dispatch*/
                if(LBLC) {
                    DPRINTF(FmtSlot2, "T[%i] should has %i insts dispatch\n",
                            tid, dispatchWidth);
                }
                break;

            case Squashing:
            case StartSquash:
                dispatchable[tid] = 0;
                DPRINTF(FmtSlot2, "T[%i][Squash] has no insts to dispatch\n", tid);
                break;
        }
    }
}

template<class Impl>
void
DefaultIEW<Impl>::computeMiss(ThreadID tid)
{
    /**在dispatch之前被调用
     *记录已经确定了的该线程在本周期的wait和miss情况
     */
    if (tid != HPT) return;

    switch (dispatchStatus[tid]) {
        case Running:
        case Idle:
        case Unblocking:
            if (dispatchable[tid] < dispatchWidth) {
                int wasted = dispatchWidth - dispatchable[tid];

                if (dispatchStatus[HPT] == Unblocking &&
                        !fromRename->frontEndMiss && LBLC) {
                    this->incLocalSlots(HPT, LBLCWait, wasted);

                } else {
                    this->incLocalSlots(HPT, InstMiss, wasted);
                }

                DPRINTF(FmtSlot2, "T[%i] wastes %i slots because insts not enough\n",
                        tid, wasted);
            }
            break;

        case Blocked:
            /**block一定导致本周期miss */
            if (fromRename->frontEndMiss) {
                this->incLocalSlots(HPT, InstMiss, dispatchWidth);

            } else if (missStat.numL2MissLoad[HPT]) {
                this->incLocalSlots(tid, EntryMiss, dispatchWidth);

            } else if (missStat.numL2MissLoad[LPT]) {
                LB_all = true;
                this->incLocalSlots(tid, EntryWait, dispatchWidth);

            } else if (LB_all) {
                this->incLocalSlots(tid, InstMiss,
                        dispatchWidth - numLPTcause);
                this->incLocalSlots(tid, EntryWait, numLPTcause);

            } else if(LB_part) {
                this->incLocalSlots(tid, EntryWait, numLPTcause);
                this->incLocalSlots(tid, EntryMiss,
                        dispatchable[tid] - numLPTcause);
                this->incLocalSlots(tid, InstMiss,
                        dispatchWidth - dispatchable[tid]);
            } else {
                this->incLocalSlots(tid, EntryMiss, dispatchable[tid]);
                this->incLocalSlots(tid, InstMiss,
                        dispatchWidth - dispatchable[tid]);
            }

            break;

        case Squashing:
        case StartSquash:
            /**Squash一定导致本周期miss*/

            this->incLocalSlots(HPT, InstMiss, dispatchWidth);

            DPRINTF(FmtSlot2, "T[%i] wastes %i slots because blocked or squash\n",
                    tid, dispatchWidths[tid]);
            break;

        default:
            break;
    }
}

template<class Impl>
void
DefaultIEW<Impl>::missTry()
{
}

template<class Impl>
void
DefaultIEW<Impl>::genShadow()
{
    DPRINTF(BMT, "genShadowing\n");
    inShadow = true;
    shadowIQ = instQueue.maxEntries[HPT] - instQueue.numBusyEntries(HPT);
    shadowLQ = ldstQueue.maxLQEntries[HPT] - ldstQueue.numLoads(HPT);
    shadowSQ = ldstQueue.maxSQEntries[HPT] - ldstQueue.numStores(HPT);

    InstSeqNum start = ~0, end = 0;

    for (MissTable::const_iterator it = l2MissTable.begin();
            it != l2MissTable.end(); it++) {
        if (it->second.cacheLevel == 2 && it->second.tid == HPT) {
            if (it->second.seqNum < start) {
                start = it->second.seqNum;
            }

            if (it->second.seqNum > end) {
                end = it->second.seqNum;
            }
        }
    }
    bmt->clear(HPT);
    bmt->setRange(start, end);
}

template<class Impl>
void
DefaultIEW<Impl>::shine(const char *reason)
{
    DPRINTF(BMT, "Shinging because of %s\n", reason);
    inShadow = false;
    shadowIQ = 0;
    shadowLQ = 0;
    shadowSQ = 0;
}

template<class Impl>
void
DefaultIEW<Impl>::clearFull()
{
    for(ThreadID tid = 0; tid < numThreads; tid++) {
        numLQFull[tid] = 0;
        numSQFull[tid] = 0;
        numIQFull[tid] = 0;
        numLQWait[tid] = 0;
        numSQWait[tid] = 0;
        numIQWait[tid] = 0;
    }
}


#endif//__CPU_O3_IEW_IMPL_IMPL_HH__
