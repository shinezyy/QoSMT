/*
 * Copyright (c) 2010-2012, 2014-2015 ARM Limited
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
 *          Korey Sewell
 */

#ifndef __CPU_O3_RENAME_IMPL_HH__
#define __CPU_O3_RENAME_IMPL_HH__

#include <list>

#include "arch/isa_traits.hh"
#include "arch/registers.hh"
#include "config/the_isa.hh"
#include "cpu/o3/rename.hh"
#include "mem/cache/miss_table.hh"
#include "cpu/reg_class.hh"
#include "debug/Activity.hh"
#include "debug/Rename.hh"
#include "debug/O3PipeView.hh"
#include "debug/Pard.hh"
#include "debug/FmtSlot.hh"
#include "debug/RenameBreakdown.hh"
#include "debug/LB.hh"
#include "debug/BMT.hh"
#include "debug/InstPass.hh"
#include "debug/SI.hh"
#include "debug/missTry.hh"
#include "debug/missTry3.hh"
#include "params/DerivO3CPU.hh"
#include "enums/OpClass.hh"
#include "base/misc.hh"
#include "cpu/thread_context.hh"
#include "cpu/o3/comm.hh"
#include "cpu/o3/scoreboard.hh"

using namespace std;

#define dis(x) \
    x ? (x)->staticInst->disassemble((x)->instAddr()) : "Nothing"

template <class Impl>
DefaultRename<Impl>::DefaultRename(O3CPU *_cpu, DerivO3CPUParams *params)
    : SlotCounter<Impl>(params, params->renameWidth),
      cpu(_cpu),
      iewToRenameDelay((int) params->iewToRenameDelay),
      decodeToRenameDelay((int) params->decodeToRenameDelay),
      commitToRenameDelay((unsigned) params->commitToRenameDelay),
      renameWidth(params->renameWidth),
      commitWidth(params->commitWidth),
      numThreads((ThreadID) params->numThreads),
      maxPhysicalRegs((PhysRegIndex) (params->numPhysIntRegs + params->numPhysFloatRegs
                      + params->numPhysCCRegs)),
      BLBlocal(false),
      blockCycles(0),
      slotConsumer (params, params->renameWidth, name())
{
    if (renameWidth > Impl::MaxWidth)
        fatal("renameWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             renameWidth, static_cast<int>(Impl::MaxWidth));

    // @todo: Make into a parameter.
    skidBufferMax = (unsigned) decodeToRenameDelay + 1;

    for (int i = 0; i < sizeof(this->nrFreeRegs) / sizeof(this->nrFreeRegs[0]); i++) {
        nrFreeRegs[i] = params->numPhysIntRegs;
    }
}

template <class Impl>
std::string
DefaultRename<Impl>::name() const
{
    return cpu->name() + ".rename";
}

template <class Impl>
void
DefaultRename<Impl>::regStats()
{
    SlotCounter<Impl>::regStats();
    slotConsumer.regStats();
    renameSquashCycles
        .init(numThreads)
        .name(name() + ".SquashCycles")
        .desc("Number of cycles rename is squashing")
        ;
    renameIdleCycles
        .init(numThreads)
        .name(name() + ".IdleCycles")
        .desc("Number of cycles rename is idle")
        ;
    renameBlockCycles
        .init(numThreads)
        .name(name() + ".BlockCycles")
        .desc("Number of cycles rename is blocking")
        ;
    renameSerializeStallCycles
        .init(numThreads)
        .name(name() + ".serializeStallCycles")
        .desc("count of cycles rename stalled for serializing inst")
        ;
    renameRunCycles
        .init(numThreads)
        .name(name() + ".RunCycles")
        .desc("Number of cycles rename is running")
        ;
    renameUnblockCycles
        .init(numThreads)
        .name(name() + ".UnblockCycles")
        .desc("Number of cycles rename is unblocking")
        ;
    renameRenamedInsts
        .name(name() + ".RenamedInsts")
        .desc("Number of instructions processed by rename")
        .prereq(renameRenamedInsts);
    renameSquashedInsts
        .name(name() + ".SquashedInsts")
        .desc("Number of squashed instructions processed by rename")
        .prereq(renameSquashedInsts);
    renameROBFullEvents
        .init(numThreads)
        .name(name() + ".ROBFullEvents")
        .desc("Number of times rename has blocked due to ROB full")
        .flags(Stats::display);
    renameIQFullEvents
        .init(numThreads)
        .name(name() + ".IQFullEvents")
        .desc("Number of times rename has blocked due to IQ full")
        .flags(Stats::display);
    renameLQFullEvents
        .init(numThreads)
        .name(name() + ".LQFullEvents")
        .desc("Number of times rename has blocked due to LQ full")
        .flags(Stats::display);
    renameSQFullEvents
        .init(numThreads)
        .name(name() + ".SQFullEvents")
        .desc("Number of times rename has blocked due to SQ full")
        .flags(Stats::display);
    renameFullRegistersEvents
        .name(name() + ".FullRegisterEvents")
        .desc("Number of times there has been no free registers")
        .prereq(renameFullRegistersEvents);
    renameRenamedOperands
        .name(name() + ".RenamedOperands")
        .desc("Number of destination operands rename has renamed")
        .prereq(renameRenamedOperands);
    renameRenameLookups
        .name(name() + ".RenameLookups")
        .desc("Number of register rename lookups that rename has made")
        .prereq(renameRenameLookups);
    renameCommittedMaps
        .name(name() + ".CommittedMaps")
        .desc("Number of HB maps that are committed")
        .prereq(renameCommittedMaps);
    renameUndoneMaps
        .name(name() + ".UndoneMaps")
        .desc("Number of HB maps that are undone due to squashing")
        .prereq(renameUndoneMaps);
    renamedSerializing
        .name(name() + ".serializingInsts")
        .desc("count of serializing insts renamed")
        .flags(Stats::total)
        ;
    renamedTempSerializing
        .name(name() + ".tempSerializingInsts")
        .desc("count of temporary serializing insts renamed")
        .flags(Stats::total)
        ;
    renameSkidInsts
        .name(name() + ".skidInsts")
        .desc("count of insts added to the skid buffer")
        .flags(Stats::total)
        ;
    intRenameLookups
        .name(name() + ".int_rename_lookups")
        .desc("Number of integer rename lookups")
        .prereq(intRenameLookups);
    fpRenameLookups
        .name(name() + ".fp_rename_lookups")
        .desc("Number of floating rename lookups")
        .prereq(fpRenameLookups);
    intRegUtilization
        .name(name() + ".intPhyReg_utilization")
        .desc("Utilization of int registers")
        .flags(Stats::display);
    floatRegUtilization
        .name(name() + ".floatPhyReg_utilization")
        .desc("Utilization of float registers")
        .flags(Stats::display);

    numROBWaitStat
        .init(numThreads)
        .name(name() + ".ROBWaits")
        .desc("ROBWaits")
        ;

    numIQWaitStat
        .init(numThreads)
        .name(name() + ".IQWaits")
        .desc("IQWaits")
        ;

    numLQWaitStat
        .init(numThreads)
        .name(name() + ".LQWaits")
        .desc("LQWaits")
        ;

    numSQWaitStat
        .init(numThreads)
        .name(name() + ".SQWaits")
        .desc("SQWaits")
        ;

    normalNoROBHead
            .init((Stats::size_type) numThreads)
            .name(name() + ".normalNoROBHead")
            .desc("normalNoROBHead")
            ;

   normalHeadNotMiss
            .init((Stats::size_type) numThreads)
            .name(name() + ".normalHeadNotMiss")
            .desc("normalHeadNotMiss")
            ;

   normalCount
            .init((Stats::size_type) numThreads)
            .name(name() + ".normalCount")
            .desc("normalCount")
            ;
}

template <class Impl>
void
DefaultRename<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from IEW stage.
    fromIEW = timeBuffer->getWire(-iewToRenameDelay);

    // Setup wire to read infromation from time buffer, from commit stage.
    fromCommit = timeBuffer->getWire(-commitToRenameDelay);

    // Setup wire to write information to previous stages.
    toDecode = timeBuffer->getWire(0);
}

template <class Impl>
void
DefaultRename<Impl>::setRenameQueue(TimeBuffer<RenameStruct> *rq_ptr)
{
    renameQueue = rq_ptr;

    // Setup wire to write information to future stages.
    toIEW = renameQueue->getWire(0);
}

template <class Impl>
void
DefaultRename<Impl>::setDecodeQueue(TimeBuffer<DecodeStruct> *dq_ptr)
{
    decodeQueue = dq_ptr;

    // Setup wire to get information from decode.
    fromDecode = decodeQueue->getWire(-decodeToRenameDelay);
}

template <class Impl>
void
DefaultRename<Impl>::startupStage()
{
    resetStage();
}

template <class Impl>
void
DefaultRename<Impl>::resetStage()
{
    _status = Inactive;

    resumeSerialize = false;
    resumeUnblocking = false;

    // Grab the number of free entries directly from the stages.
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        renameStatus[tid] = Idle;

        freeEntries[tid].iqEntries = iew_ptr->instQueue.numFreeEntries(tid);
        freeEntries[tid].lqEntries = iew_ptr->ldstQueue.numFreeLoadEntries(tid);
        freeEntries[tid].sqEntries = iew_ptr->ldstQueue.numFreeStoreEntries(tid);
        freeEntries[tid].robEntries = commit_ptr->numROBFreeEntries(tid);

        DPRINTF(Pard, "Thread %d:\n\t- free IQ: %d\n\t"
                "- free LQ: %d\n\t- free SQ: %d\n\t"
                "- free ROB: %d\n", tid, freeEntries[tid].iqEntries,
                freeEntries[tid].lqEntries, freeEntries[tid].sqEntries,
                freeEntries[tid].robEntries);

        emptyROB[tid] = true;

        stalls[tid].iew = false;
        serializeInst[tid] = NULL;
        tailSI[tid] = false;
        tailSINext[tid] = false;

        instsInProgress[tid] = 0;
        loadsInProgress[tid] = 0;
        storesInProgress[tid] = 0;

        serializeOnNextInst[tid] = false;
        numVROB[tid] = 0.0;
    }
    clearFull();
}

template<class Impl>
void
DefaultRename<Impl>::setActiveThreads(list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}


template <class Impl>
void
DefaultRename<Impl>::setRenameMap(RenameMap rm_ptr[])
{
    for (ThreadID tid = 0; tid < numThreads; tid++)
        renameMap[tid] = &rm_ptr[tid];
}

template <class Impl>
void
DefaultRename<Impl>::setFreeList(FreeList *fl_ptr)
{
    freeList = fl_ptr;
}

template<class Impl>
void
DefaultRename<Impl>::setScoreboard(Scoreboard *_scoreboard)
{
    scoreboard = _scoreboard;
}

template <class Impl>
bool
DefaultRename<Impl>::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (instsInProgress[tid] != 0 ||
            !historyBuffer[tid].empty() ||
            !skidBuffer[tid].empty() ||
            !insts[tid].empty())
            return false;
    }
    return true;
}

template <class Impl>
void
DefaultRename<Impl>::takeOverFrom()
{
    resetStage();
}

template <class Impl>
void
DefaultRename<Impl>::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        assert(historyBuffer[tid].empty());
        assert(insts[tid].empty());
        assert(skidBuffer[tid].empty());
        assert(instsInProgress[tid] == 0);
    }
}

template <class Impl>
void
DefaultRename<Impl>::squash(const InstSeqNum &squash_seq_num, ThreadID tid) {
    DPRINTF(Rename, "[tid:%u]: Squashing instructions.\n", tid);

    // Clear the stall signal if rename was blocked or unblocking before.
    // If it still needs to block, the blocking should happen the next
    // cycle and there should be space to hold everything due to the squash.
    if (renameStatus[tid] == Blocked ||
        renameStatus[tid] == Unblocking) {
        toDecode->renameUnblock[tid] = 1;
        BLBlocal = tid == HPT ? false : BLBlocal;

        resumeSerialize = false;
        serializeInst[tid] = NULL;
        tailSINext[tid] = false;
    } else if (renameStatus[tid] == SerializeStall) {
        if (serializeInst[tid]->seqNum <= squash_seq_num) {
            DPRINTF(Rename, "Rename will resume serializing after squash\n");
            resumeSerialize = true;
            assert(serializeInst[tid]);
        } else {
            resumeSerialize = false;
            toDecode->renameUnblock[tid] = 1;
            BLBlocal = tid == HPT ? false : BLBlocal;

            serializeInst[tid] = NULL;
            tailSINext[tid] = false;
        }
    }

    // Set the status to Squashing.
    renameStatus[tid] = Squashing;
    DPRINTF(RenameBreakdown, "Thread [%i] Rename status switched to Squashing\n", tid);

    // Squash any instructions from decode.
    for (int i = 0; i < fromDecode->size; i++) {
        if (fromDecode->insts[i]->threadNumber == tid &&
            fromDecode->insts[i]->seqNum > squash_seq_num) {
            fromDecode->insts[i]->setSquashed();
            wroteToTimeBuffer = true;
        }
    }

    // Clear the instruction list and skid buffer in case they have any
    // insts in them.
    insts[tid].clear();

    // Clear the skid buffer in case it has any data in it.
    while (!skidBuffer[tid].empty()) {
        if (!skidBuffer[tid].front().empty()) {
            DPRINTF(RenameBreakdown, "Squash sn[%lli] from skidBuffer of T[%i]\n",
                    skidBuffer[tid].front().front()->seqNum, tid);
        }
        skidBuffer[tid].pop();
        skidInstTick[tid].pop();
    }

    while (!skidSlotBuffer[tid].empty()) {
        skidSlotBuffer[tid].pop();
        skidSlotTick[tid].pop();
    }

    doSquash(squash_seq_num, tid);

    squashedThisCycle[tid] = true;

    if (HPT == tid) {
        shine("squash");
        toIEW->shine = true;
    }
}

template <class Impl>
void
DefaultRename<Impl>::tick()
{
    clearLocalSignals();
    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    slotConsumer.cycleStart();

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        toIEW->serialize[tid] = false;
        toIEW->unSerialize[tid] = false;
    }

    if (fromIEW->iewInfo[HPT].genShadow) {
        genShadow();
    } else if (fromIEW->iewInfo[HPT].shine) {
        shine("IEW shine");
    }

    sortInsts();

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    // Check stall and squash signals.
    for (ThreadID tid = 0; tid < numThreads; tid++) {

        DPRINTF(RenameBreakdown, "Processing [tid:%i]\n", tid);
        status_change = checkSignalsAndUpdate(tid) || status_change;
    }

    for (ThreadID tid = 0; tid < numThreads; tid++) {

        computeMiss(tid);
        rename(status_change, tid);
    }

    /**
    if (status_change && renameStatus[HPT] == Blocked) {
        missTry();
    }
    */

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

    threads = activeThreads->begin();

    while (threads != end) {
        ThreadID tid = *threads++;

        // If we committed this cycle then doneSeqNum will be > 0
        if (fromCommit->commitInfo[tid].doneSeqNum != 0 &&
            !fromCommit->commitInfo[tid].squash &&
            renameStatus[tid] != Squashing) {

            removeFromHistory(fromCommit->commitInfo[tid].doneSeqNum,
                                  tid);
        }
    }

    // @todo: make into updateProgress function
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        instsInProgress[tid] -= fromIEW->iewInfo[tid].dispatched;
        loadsInProgress[tid] -= fromIEW->iewInfo[tid].dispatchedToLQ;
        storesInProgress[tid] -= fromIEW->iewInfo[tid].dispatchedToSQ;
        assert(loadsInProgress[tid] >= 0);
        assert(storesInProgress[tid] >= 0);
        assert(instsInProgress[tid] >=0);
    }

    increaseFreeEntries();

    passLB(HPT);

    if (this->countSlot(HPT, SlotsUse::Base) != toIEWNum[HPT]) {
        this->printSlotRow(this->slotUseRow[HPT], renameWidth);
        panic("Slots [%i] and Insts [%i] are not coherence!\n",
                this->countSlot(HPT, SlotsUse::Base), toIEWNum[HPT]);
    }
    toIEW->slotPass = this->slotUseRow[HPT];

    if (this->checkSlots(HPT)) {
        this->sumLocalSlots(HPT);
    }
}

template<class Impl>
void
DefaultRename<Impl>::rename(bool &status_change, ThreadID tid)
{
    // If status is Running or idle,
    //     call renameInsts()
    // If status is Unblocking,
    //     buffer any instructions coming from decode
    //     continue trying to empty skid buffer
    //     check if stall conditions have passed

    if (tid == HPT) {
        switch(renameStatus[tid]) {
            case Running:
                DPRINTF(RenameBreakdown, "Thread [%i] status now:" "Running\n", tid);
                break;
            case Idle:
                DPRINTF(RenameBreakdown, "Thread [%i] status now:" "Idle\n", tid);
                break;
            case StartSquash:
                DPRINTF(RenameBreakdown, "Thread [%i] status now:" "StartSquash\n", tid);
                break;
            case Squashing:
                DPRINTF(RenameBreakdown, "Thread [%i] status now:" "Squashing\n", tid);
                break;
            case Blocked:
                DPRINTF(RenameBreakdown, "Thread [%i] status now:" "Blocked\n", tid);
                break;
            case Unblocking:
                DPRINTF(RenameBreakdown, "Thread [%i] status now:" "Unblocking\n", tid);
                break;
            case SerializeStall:
                DPRINTF(RenameBreakdown, "Thread [%i] status now:" "SerializeStall\n", tid);
                DPRINTF(SI, "T[%d] serialize on %llu\n", tid, curTick());
                break;
            default:
                break;
        }
    }

    if (renameStatus[tid] == Blocked) {
        ++renameBlockCycles[tid];
        if (fromIEW->iewInfo[HPT].BLB) {
            DPRINTF(RenameBreakdown, "[Block Reason] LPT cause IEW stall\n");
        }
    } else if (renameStatus[tid] == Squashing) {
        ++renameSquashCycles[tid];

    } else if (renameStatus[tid] == SerializeStall) {
        ++renameSerializeStallCycles[tid];
        // If we are currently in SerializeStall and resumeSerialize
        // was set, then that means that we are resuming serializing
        // this cycle.  Tell the previous stages to block.
        if (resumeSerialize) {
            resumeSerialize = false;
            block(tid);
            toDecode->renameUnblock[tid] = false;
        }
    } else if (renameStatus[tid] == Unblocking) {
        if (resumeUnblocking) {
            panic("Unexpected branch taken!\n");
            block(tid);
            resumeUnblocking = false;
            toDecode->renameUnblock[tid] = false;
        }
    }

    if (renameStatus[tid] == Running ||
        renameStatus[tid] == Idle) {
        DPRINTF(Rename, "[tid:%u]: Not blocked, so attempting to run "
                "stage.\n", tid);

        renameInsts(tid);
    } else if (renameStatus[tid] == Unblocking) {
        renameInsts(tid);

        if (validInsts()) {
            // Add the current inputs to the skid buffer so they can be
            // reprocessed when this stage unblocks.
            skidInsert(tid);
        }

        // If we switched over to blocking, then there's a potential for
        // an overall status change.
        status_change = unblock(tid) || status_change || blockThisCycle;
    }
}

template <class Impl>
void
DefaultRename<Impl>::printROBHeadStatus(ThreadID tid) const
{
    return;
    /*
    for (ThreadID t = 0; t < numThreads; t++) {
        const char *mark = t == tid ? "*" : " ";
        auto rob = commit_ptr->rob;
        auto inst = rob->readHeadInst(t);
        DPRINTF(Pard, "[%s] tid %d: %s, [%s], entries %d\n", mark, t,
                inst ? inst->statusToString().c_str() : "nil",
                inst ? Enums::OpClassStrings[inst->opClass()] : "nil",
                rob->getThreadEntries(t));
    }
    */
}

template <class Impl>
void
DefaultRename<Impl>::renameInsts(ThreadID tid)
{
    // Instructions can be either in the skid buffer or the queue of
    // instructions coming from decode, depending on the status.
    int skid_size = 0;
    if(skidBuffer[tid].size() != 0) {
        skid_size = skidBuffer[tid].front().size();
        assert(skid_size > 0);
    }

    int insts_available = renameStatus[tid] == Unblocking ?
                          skid_size : (int) insts[tid].size();

    // Check the decode queue to see if instructions are available.
    // If there are no available instructions to rename, then do nothing.
    if (insts_available == 0) {
        assert(renameStatus[tid] != Unblocking);
        curCycleRow[tid] = fromDecode->slotPass;
        DPRINTF(RenameBreakdown, "[tid:%u]: Nothing to do, breaking out early.\n", tid);
        // Should I change status to idle?
        ++renameIdleCycles[tid];
        return;
    } else if (renameStatus[tid] == Unblocking) {
        assert(!skidSlotBuffer[tid].empty());
        assert(!skidSlotBuffer[tid].front().empty());
        assert(skidSlotTick[tid].front() == skidInstTick[tid].front());
        curCycleRow[tid] = skidSlotBuffer[tid].front();
        ++renameUnblockCycles[tid];
    } else if (renameStatus[tid] == Running) {
        curCycleRow[tid] = fromDecode->slotPass;
        ++renameRunCycles[tid];
    }

    DynInstPtr inst;

    // Will have to do a different calculation for the number of free
    // entries.
    int free_rob_entries = calcFreeROBEntries(tid);

    // Check if there's any space left.
    if (free_rob_entries <= 0) {
        DPRINTF(RenameBreakdown, "[tid:%u]: Blocking due to no free ROB "
                "entries.\nROB has %i free entries.\n",
                tid, free_rob_entries);

        blockThisCycle = true;
        fullSource[tid] = SlotConsm::ROB;
        block(tid);
        return;

    } else if (free_rob_entries < insts_available) {
        DPRINTF(RenameBreakdown, "[tid:%u]: Will have to block this cycle."
                "%i insts available, but only %i insts can be "
                "renamed due to ROB limits.\n",
                tid, insts_available, free_rob_entries);

        insts_available = free_rob_entries;
        blockThisCycle = true;
        fullSource[tid] = SlotConsm::ROB;
    }

    InstRow &insts_to_rename = renameStatus[tid] == Unblocking ?
        skidBuffer[tid].front() : insts[tid];

    DPRINTF(Rename, "[tid:%u]: %i available instructions to "
            "send iew.\n", tid, insts_available);

    ThreadStatus old_status = renameStatus[tid];

    DPRINTF(Rename, "[tid:%u]: %i insts pipelining from Rename | %i insts "
            "dispatched to IQ last cycle.\n",
            tid, instsInProgress[tid], fromIEW->iewInfo[tid].dispatched);

    // Handle serializing the next instruction if necessary.
    if (serializeOnNextInst[tid]) {
        if (emptyROB[tid] && instsInProgress[tid] == 0) {
            // ROB already empty; no need to serialize.
            serializeOnNextInst[tid] = false;
        } else if (!insts_to_rename.empty()) {
            insts_to_rename.front()->setSerializeBefore();
        }
    }

    int renamed_insts = 0;

    //<editor-fold desc="Main loop to rename instructions">
    while (insts_available > 0 &&  toIEWIndex < renameWidth) {
        DPRINTF(Rename, "[tid:%u]: Sending instructions to IEW.\n", tid);

        assert(!insts_to_rename.empty());

        inst = insts_to_rename.front();

        insts_to_rename.pop_front();

        if (renameStatus[tid] == Unblocking) {
            DPRINTF(Rename,"[tid:%u]: Removing [sn:%lli] PC:%s from rename "
                    "skidBuffer\n", tid, inst->seqNum, inst->pcState());
        }

        if (inst->isSquashed()) {
            DPRINTF(Rename, "[tid:%u]: instruction %i with PC %s is "
                    "squashed, skipping.\n", tid, inst->seqNum,
                    inst->pcState());

            ++renameSquashedInsts;
            // Decrement how many instructions are available.
            --insts_available;
            continue;
        }

        DPRINTF(Rename, "[tid:%u]: Processing instruction [sn:%lli] with "
                "PC %s.\n", tid, inst->seqNum, inst->pcState());

        // Check here to make sure there are enough destination registers
        // to rename to.  Otherwise block.
        if (!(renameMap[tid]->canRename(inst->numIntDestRegs(),
                                        inst->numFPDestRegs(),
                                        inst->numCCDestRegs())
              && nrFreeRegs[tid] >= inst->numIntDestRegs())) { // can handle 0
            DPRINTF(RenameBreakdown, "Blocking due to lack of free "
                    "physical registers to rename to.\n");
            blockThisCycle = true;
            insts_to_rename.push_front(inst);
            ++renameFullRegistersEvents;
            fullSource[tid] = SlotConsm::Register;
            break;
        }

        // Handle serializeAfter/serializeBefore instructions.
        // serializeAfter marks the next instruction as serializeBefore.
        // serializeBefore makes the instruction wait in rename until the ROB
        // is empty.

        // In this model, IPR accesses are serialize before
        // instructions, and store conditionals are serialize after
        // instructions.  This is mainly due to lack of support for
        // out-of-order operations of either of those classes of
        // instructions.
        //<editor-fold desc="SI related process">
        if ((inst->isIprAccess() || inst->isSerializeBefore()) &&
            !inst->isSerializeHandled()) {
            DPRINTF(RenameBreakdown, "Serialize before instruction encountered.\n");

            if (!inst->isTempSerializeBefore()) {
                renamedSerializing++;
                inst->setSerializeHandled();
            } else {
                renamedTempSerializing++;
            }

            // Change status over to SerializeStall so that other stages know
            // what this is blocked on.
            renameStatus[tid] = SerializeStall;
            fullSource[tid] = SlotConsm::SI;
            toIEW->serialize[tid] = true;
            DPRINTF(RenameBreakdown, "Thread [%i] Rename status switched to SerializeStall\n",
                    tid);

            serializeInst[tid] = inst;
            if (insts_to_rename.empty()) {
                tailSINext[tid] = true;
            }
            blockThisCycle = true;
            break;
        } else if ((inst->isStoreConditional() || inst->isSerializeAfter()) &&
                   !inst->isSerializeHandled()) {
            DPRINTF(Rename, "Serialize after instruction encountered.\n");

            renamedSerializing++;

            inst->setSerializeHandled();

            serializeAfter(insts_to_rename, tid);
        }
        //</editor-fold>

        renameSrcRegs(inst, inst->threadNumber);

        renameDestRegs(inst, inst->threadNumber);

        if (inst->isLoad()) {
            loadsInProgress[tid]++;
        }
        if (inst->isStore()) {
            storesInProgress[tid]++;
        }
        ++renamed_insts;


        // Put instruction in rename queue.
        toIEW->insts[toIEWIndex] = inst;
        ++(toIEW->size);

        // Increment which instruction we're on.
        ++toIEWIndex;
        ++toIEWNum[tid];

        // Decrement how many instructions are available.
        --insts_available;
    }
    //</editor-fold>

    instsInProgress[tid] += renamed_insts;
    renameRenamedInsts += renamed_insts;

    if (HPT == tid && inShadow) {
        inst->inShadowROB = true;
        shadowROB -= renamed_insts;
        inst->blockedCycles += blockedCycles;
        blockCycles = 0;
        if (shadowROB <= 0) {
            toIEW->shine = true;
            shine("use up ROB");
        }
    }

    // If we wrote to the time buffer, record this.
    if (toIEWIndex) {
        wroteToTimeBuffer = true;
    }

    // Check if there's any instructions left that haven't yet been renamed.
    // If so then block.
    if (insts_available) {
        blockThisCycle = true;
    }

    if (insts_to_rename.empty() && old_status == Unblocking) {
        DPRINTF(RenameBreakdown, "Pop empty row from skidBuffer of T[%i]\n", tid);
        skidBuffer[tid].pop();
        skidInstTick[tid].pop();
        skidSlotBuffer[tid].pop();
        skidSlotTick[tid].pop();
    }

    if (blockThisCycle) {
        block(tid);
        toDecode->renameUnblock[tid] = false;
    }
}

template<class Impl>
void
DefaultRename<Impl>::skidInsert(ThreadID tid)
{
    //rewrite

    if (insts[tid].empty()) {
        return;
    }
    DPRINTF(RenameBreakdown, "Inserting sn[%lli] into skidBuffer of T[%i]\n",
            insts[tid].front()->seqNum, tid);
    skidBuffer[tid].push(insts[tid]);
    insts[tid].clear();

    skidInstTick[tid].push(curTick());

    skidSlotBuffer[tid].push(fromDecode->slotPass);
    DPRINTF(RenameBreakdown, "skid Slot Row:\n");
    this->printSlotRow(fromDecode->slotPass, renameWidth);

    skidSlotTick[tid].push(curTick());
    assert(skidBuffer[tid].size() <= skidBufferMax);
}

template <class Impl>
void
DefaultRename<Impl>::sortInsts()
{
    int insts_from_decode = fromDecode->size;
    for (int i = 0; i < insts_from_decode; ++i) {
        DynInstPtr inst = fromDecode->insts[i];
        insts[inst->threadNumber].push_back(inst);
#if TRACING_ON
        if (DTRACE(O3PipeView)) {
            inst->renameTick = curTick() - inst->fetchTick;
        }
#endif
    }
    DPRINTF(RenameBreakdown, "Total number of insts from decode is %i.\n", insts_from_decode);
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        DPRINTF(RenameBreakdown, "Number of insts of Thread %i from decode is %i.\n",
                tid, insts[tid].size());
    }
}

template<class Impl>
bool
DefaultRename<Impl>::skidsEmpty()
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

template<class Impl>
void
DefaultRename<Impl>::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (renameStatus[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Rename will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(Activity, "Activating stage.\n");

            cpu->activateStage(O3CPU::RenameIdx);
        }
    } else {
        // If it's not unblocking, then rename will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(Activity, "Deactivating stage.\n");

            cpu->deactivateStage(O3CPU::RenameIdx);
        }
    }
}

template <class Impl>
bool
DefaultRename<Impl>::block(ThreadID tid)
{
    DPRINTF(Rename, "[tid:%u]: Blocking.\n", tid);
    DPRINTF(RenameBreakdown, "[tid:%u]: Blocking.\n", tid);

    // Add the current inputs onto the skid buffer, so they can be
    // reprocessed when this stage unblocks.
    skidInsert(tid);

    // Only signal backwards to block if the previous stages do not think
    // rename is already blocked.
    if (renameStatus[tid] != Blocked) {
        // If resumeUnblocking is set, we unblocked during the squash,
        // but now we're have unblocking status. We need to tell earlier
        // stages to block.
        if (resumeUnblocking || renameStatus[tid] != Unblocking) {
            toDecode->renameBlock[tid] = true;
            toDecode->renameUnblock[tid] = false;
            wroteToTimeBuffer = true;

            if (tid == HPT) {
                blockCycles++;
            }

            if (HPT == tid && (LB_all)) {
                if (!fromIEW->iewInfo[HPT].genShadow) {
                    toIEW->genShadow = true;
                }
                if (!inShadow) {
                    genShadow();
                } else {
                    shine("Blocking again");
                }
            }
        }

        // Rename can not go from SerializeStall to Blocked, otherwise
        // it would not know to complete the serialize stall.
        if (renameStatus[tid] != SerializeStall) {
            // Set status to Blocked.
            renameStatus[tid] = Blocked;

            BLBlocal = tid == HPT ?
                fromIEW->iewInfo[HPT].BLB || LB_all : BLBlocal;

            DPRINTF(RenameBreakdown, "Thread [%i] Rename status switched to Blocked\n", tid);
            return true;
        } else {
            DPRINTF(RenameBreakdown, "Thread [%i] Rename status remains SerializeStall\n",
                    tid);
        }
    }

    return false;
}

template <class Impl>
bool
DefaultRename<Impl>::unblock(ThreadID tid)
{
    DPRINTF(Rename, "[tid:%u]: Trying to unblock.\n", tid);
    DPRINTF(RenameBreakdown, "[tid:%u]: Trying to unblock.\n", tid);

    if (blockCycles != 0)
        blockedCycles = blockCycles;
    blockCycles = 0;

    // Rename is done unblocking if the skid buffer is empty.
    if (skidBuffer[tid].empty() && renameStatus[tid] != SerializeStall) {

        DPRINTF(Rename, "[tid:%u]: Done unblocking.\n", tid);

        toDecode->renameUnblock[tid] = true;
        BLBlocal = tid == HPT ? false : BLBlocal;
        wroteToTimeBuffer = true;

        DPRINTF(RenameBreakdown, "Rename Status of Thread [%u] switched to Running.\n", tid);
        renameStatus[tid] = Running;

        return true;
    }
    DPRINTF(RenameBreakdown, "Rename Status of Thread [%u] unchanged.\n", tid);

    return false;
}

template <class Impl>
void
DefaultRename<Impl>::doSquash(const InstSeqNum &squashed_seq_num, ThreadID tid)
{
    typename std::list<RenameHistory>::iterator hb_it =
        historyBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    if (historyBuffer[tid].empty()) {
        return;
    }

    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!historyBuffer[tid].empty() &&
           hb_it->instSeqNum > squashed_seq_num) {
        assert(hb_it != historyBuffer[tid].end());

        DPRINTF(Rename, "[tid:%u]: Removing history entry with sequence "
                "number %i.\n", tid, hb_it->instSeqNum);

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (hb_it->newPhysReg != hb_it->prevPhysReg) {
            // Tell the rename map to set the architected register to the
            // previous physical register that it was renamed to.
            renameMap[tid]->setEntry(hb_it->archReg, hb_it->prevPhysReg);

            // Put the renamed physical register back on the free list.
            freeList->addReg(hb_it->newPhysReg);
            nrFreeRegs[tid]++;
        }

        historyBuffer[tid].erase(hb_it++);

        ++renameUndoneMaps;
    }
}

template<class Impl>
void
DefaultRename<Impl>::removeFromHistory(InstSeqNum inst_seq_num, ThreadID tid)
{
    DPRINTF(Rename, "[tid:%u]: Removing a committed instruction from the "
            "history buffer %u (size=%i), until [sn:%lli].\n",
            tid, tid, historyBuffer[tid].size(), inst_seq_num);

    typename std::list<RenameHistory>::iterator hb_it =
        historyBuffer[tid].end();

    --hb_it;

    if (historyBuffer[tid].empty()) {
        DPRINTF(Rename, "[tid:%u]: History buffer is empty.\n", tid);
        return;
    } else if (hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(Rename, "[tid:%u]: Old sequence number encountered.  Ensure "
                "that a syscall happened recently.\n", tid);
        return;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!historyBuffer[tid].empty() &&
           hb_it != historyBuffer[tid].end() &&
           hb_it->instSeqNum <= inst_seq_num) {

        DPRINTF(Rename, "[tid:%u]: Freeing up older rename of reg %i, "
                "[sn:%lli].\n",
                tid, hb_it->prevPhysReg, hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (hb_it->newPhysReg != hb_it->prevPhysReg) {
            freeList->addReg(hb_it->prevPhysReg);
            nrFreeRegs[tid]++;
        }

        ++renameCommittedMaps;

        historyBuffer[tid].erase(hb_it--);
    }
}

template <class Impl>
inline void
DefaultRename<Impl>::renameSrcRegs(DynInstPtr &inst, ThreadID tid)
{
    ThreadContext *tc = inst->tcBase();
    RenameMap *map = renameMap[tid];
    unsigned num_src_regs = inst->numSrcRegs();

    // Get the architectual register numbers from the source and
    // operands, and redirect them to the right physical register.
    for (int src_idx = 0; src_idx < num_src_regs; src_idx++) {
        RegIndex src_reg = inst->srcRegIdx(src_idx);
        RegIndex rel_src_reg;
        RegIndex flat_rel_src_reg;
        PhysRegIndex renamed_reg;

        switch (regIdxToClass(src_reg, &rel_src_reg)) {
          case IntRegClass:
            flat_rel_src_reg = tc->flattenIntIndex(rel_src_reg);
            renamed_reg = map->lookupInt(flat_rel_src_reg);
            intRenameLookups++;
            break;

          case FloatRegClass:
            flat_rel_src_reg = tc->flattenFloatIndex(rel_src_reg);
            renamed_reg = map->lookupFloat(flat_rel_src_reg);
            fpRenameLookups++;
            break;

          case CCRegClass:
            flat_rel_src_reg = tc->flattenCCIndex(rel_src_reg);
            renamed_reg = map->lookupCC(flat_rel_src_reg);
            break;

          case MiscRegClass:
            // misc regs don't get flattened
            flat_rel_src_reg = rel_src_reg;
            renamed_reg = map->lookupMisc(flat_rel_src_reg);
            break;

          default:
            panic("Reg index is out of bound: %d.", src_reg);
        }

        DPRINTF(Rename, "[tid:%u]: Looking up %s arch reg %i (flattened %i), "
                "got phys reg %i\n", tid, RegClassStrings[regIdxToClass(src_reg)],
                (int)src_reg, (int)flat_rel_src_reg, (int)renamed_reg);

        inst->renameSrcReg(src_idx, renamed_reg);

        // See if the register is ready or not.
        if (scoreboard->getReg(renamed_reg)) {
            DPRINTF(Rename, "[tid:%u]: Register %d is ready.\n",
                    tid, renamed_reg);

            inst->markSrcRegReady(src_idx);
        } else {
            DPRINTF(Rename, "[tid:%u]: Register %d is not ready.\n",
                    tid, renamed_reg);
        }

        ++renameRenameLookups;
    }
}

template <class Impl>
inline void
DefaultRename<Impl>::renameDestRegs(DynInstPtr &inst, ThreadID tid)
{
    ThreadContext *tc = inst->tcBase();
    RenameMap *map = renameMap[tid];
    unsigned num_dest_regs = inst->numDestRegs();

    // Rename the destination registers.
    for (int dest_idx = 0; dest_idx < num_dest_regs; dest_idx++) {
        RegIndex dest_reg = inst->destRegIdx(dest_idx);
        RegIndex rel_dest_reg;
        RegIndex flat_rel_dest_reg;
        RegIndex flat_uni_dest_reg;
        typename RenameMap::RenameInfo rename_result;

        switch (regIdxToClass(dest_reg, &rel_dest_reg)) {
          case IntRegClass:
            flat_rel_dest_reg = tc->flattenIntIndex(rel_dest_reg);
            rename_result = map->renameInt(flat_rel_dest_reg);
            flat_uni_dest_reg = flat_rel_dest_reg;  // 1:1 mapping
            nrFreeRegs[tid]--;
            assert(nrFreeRegs[tid] >= 0);
            break;

          case FloatRegClass:
            flat_rel_dest_reg = tc->flattenFloatIndex(rel_dest_reg);
            rename_result = map->renameFloat(flat_rel_dest_reg);
            flat_uni_dest_reg = flat_rel_dest_reg + TheISA::FP_Reg_Base;
            break;

          case CCRegClass:
            flat_rel_dest_reg = tc->flattenCCIndex(rel_dest_reg);
            rename_result = map->renameCC(flat_rel_dest_reg);
            flat_uni_dest_reg = flat_rel_dest_reg + TheISA::CC_Reg_Base;
            break;

          case MiscRegClass:
            // misc regs don't get flattened
            flat_rel_dest_reg = rel_dest_reg;
            rename_result = map->renameMisc(flat_rel_dest_reg);
            flat_uni_dest_reg = flat_rel_dest_reg + TheISA::Misc_Reg_Base;
            break;

          default:
            panic("Reg index is out of bound: %d.", dest_reg);
        }

        inst->flattenDestReg(dest_idx, flat_uni_dest_reg);

        // Mark Scoreboard entry as not ready
        scoreboard->unsetReg(rename_result.first);

        DPRINTF(Rename, "[tid:%u]: Renaming arch reg %i to physical "
                "reg %i.\n", tid, (int)flat_rel_dest_reg,
                (int)rename_result.first);

        // Record the rename information so that a history can be kept.
        RenameHistory hb_entry(inst->seqNum, flat_uni_dest_reg,
                               rename_result.first,
                               rename_result.second);

        historyBuffer[tid].push_front(hb_entry);

        DPRINTF(Rename, "[tid:%u]: Adding instruction to history buffer "
                "(size=%i), [sn:%lli].\n",tid,
                historyBuffer[tid].size(),
                (*historyBuffer[tid].begin()).instSeqNum);

        // Tell the instruction to rename the appropriate destination
        // register (dest_idx) to the new physical register
        // (rename_result.first), and record the previous physical
        // register that the same logical register was renamed to
        // (rename_result.second).
        inst->renameDestReg(dest_idx,
                            rename_result.first,
                            rename_result.second);

        ++renameRenamedOperands;
    }
}

template <class Impl>
inline int
DefaultRename<Impl>::calcFreeROBEntries(ThreadID tid)
{
    if (commit_ptr->isROBPolicyDynamic() ||
            commit_ptr->isROBPolicyProgrammable()) {
        // Calc number of all instructions in flight.
        int numInstsInFlight = 0;
        for (ThreadID t = 0; t < numThreads; t++) {
            numInstsInFlight += toIEWNum[t] + toROBNum[t];
        }
        return freeEntries[tid].robEntries - numInstsInFlight;

    } else {
        return freeEntries[tid].robEntries -
               toIEWNum[tid] - toROBNum[tid];
    }
}

template <class Impl>
inline int
DefaultRename<Impl>::calcFreeIQEntries(ThreadID tid)
{
    int num_free = freeEntries[tid].iqEntries -
                  (instsInProgress[tid] - fromIEW->iewInfo[tid].dispatched);

    //DPRINTF(Rename,"[tid:%i]: %i iq free\n",tid,num_free);

    return num_free;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcFreeLQEntries(ThreadID tid)
{
        int num_free = freeEntries[tid].lqEntries -
                                  (loadsInProgress[tid] - fromIEW->iewInfo[tid].dispatchedToLQ);
        DPRINTF(Rename, "calcFreeLQEntries: free lqEntries: %d, loadsInProgress: %d, "
                "loads dispatchedToLQ: %d\n", freeEntries[tid].lqEntries,
                loadsInProgress[tid], fromIEW->iewInfo[tid].dispatchedToLQ);
        return num_free;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcFreeSQEntries(ThreadID tid)
{
        int num_free = freeEntries[tid].sqEntries -
                                  (storesInProgress[tid] - fromIEW->iewInfo[tid].dispatchedToSQ);
        DPRINTF(Rename, "calcFreeSQEntries: free sqEntries: %d, storesInProgress: %d, "
                "stores dispatchedToSQ: %d\n", freeEntries[tid].sqEntries,
                storesInProgress[tid], fromIEW->iewInfo[tid].dispatchedToSQ);
        return num_free;
}

template <class Impl>
unsigned
DefaultRename<Impl>::validInsts()
{
    unsigned inst_count = 0;

    for (int i=0; i<fromDecode->size; i++) {
        if (!fromDecode->insts[i]->isSquashed())
            inst_count++;
    }

    return inst_count;
}

template <class Impl>
void
DefaultRename<Impl>::readStallSignals(ThreadID tid)
{
    if (fromIEW->iewBlock[tid]) {
        stalls[tid].iew = true;
    }

    if (fromIEW->iewUnblock[tid]) {
        assert(stalls[tid].iew);
        stalls[tid].iew = false;
    }
}

template <class Impl>
bool
DefaultRename<Impl>::checkStall(ThreadID tid)
{
    bool ret_val = false;

    unsigned numAvailInsts = 0;
    if (tid == HPT) {
        switch (renameStatus[tid]) {
            //这里的状态是周期开始时的状态
            case Running:
            case Idle:
                numAvailInsts = (unsigned) insts[tid].size();
                break;
            case Blocked:
            case Unblocking:
                numAvailInsts = (unsigned) skidBuffer[tid].front().size();
                break;
            default:
                break;
        }
        numLPTcause = std::min(numAvailInsts, renameWidth);
        DPRINTF(RenameBreakdown, "T[0]: %i insts in skidbuffer, %i insts from decode\n",
                skidBuffer[tid].empty() ? 0 : skidBuffer[tid].front().size(),
                insts[tid].size());
    }

    if (stalls[tid].iew) {
        DPRINTF(Rename,"[tid:%i]: Stall from IEW stage detected.\n", tid);
        fullSource[tid] = SlotConsm::IEWStage;
        ret_val = true;
        if (tid == HPT) {
            DPRINTF(RenameBreakdown, "HPT stall from IEW stage detected.\n");
        }

    } else if (calcFreeROBEntries(tid) <= 0) {
        DPRINTF(Rename,"[tid:%i]: Stall: ROB has 0 free entries.\n", tid);
        fullSource[tid] = SlotConsm::ROB;
        ret_val = true;

        toIEW->incVROB[tid] = true;

        if (tid == HPT) {
            DPRINTF(RenameBreakdown, "HPT stall because no ROB.\n");
        }

    } else if (renameMap[tid]->numFreeEntries() <= 0) {
        fullSource[tid] = SlotConsm::Register;
        DPRINTF(Rename,"[tid:%i]: Stall: RenameMap has 0 free entries.\n", tid);
        ret_val = true;
    } else if (renameStatus[tid] == SerializeStall &&
               (!emptyROB[tid] || instsInProgress[tid])) {
        DPRINTF(SI, "T[%i] %i instructions in rob, %i instructions in progress.\n",
                tid, calcFreeROBEntries(tid), instsInProgress[tid]);
        toIEW->serialize[tid] = true;
        DPRINTF(Rename,"[tid:%i]: Stall: Serialize stall and ROB is not "
                "empty.\n",
                tid);
        ret_val = true;
    }

    return ret_val;
}

template <class Impl>
void
DefaultRename<Impl>::readFreeEntries(ThreadID tid)
{
    if (fromIEW->iewInfo[tid].usedIQ) {
        freeEntries[tid].iqEntries = fromIEW->iewInfo[tid].freeIQEntries;
        maxEntries[tid].iqEntries = fromIEW->iewInfo[tid].maxIQEntries;
        busyEntries[tid].iqEntries = fromIEW->iewInfo[tid].busyIQEntries;
    }

    if (fromIEW->iewInfo[tid].usedLSQ) {
        freeEntries[tid].lqEntries = fromIEW->iewInfo[tid].freeLQEntries;
        freeEntries[tid].sqEntries = fromIEW->iewInfo[tid].freeSQEntries;

        maxEntries[tid].lqEntries = fromIEW->iewInfo[tid].maxLQEntries;
        maxEntries[tid].sqEntries = fromIEW->iewInfo[tid].maxSQEntries;

        busyEntries[tid].lqEntries = fromIEW->iewInfo[tid].busyLQEntries;
        busyEntries[tid].sqEntries = fromIEW->iewInfo[tid].busySQEntries;
    }

    if (fromCommit->commitInfo[tid].usedROB) {
        emptyROB[tid] = fromCommit->commitInfo[tid].emptyROB;
        freeEntries[tid].robEntries = fromCommit->commitInfo[tid].freeROBEntries;
        maxEntries[tid].robEntries = fromCommit->commitInfo[tid].maxROBEntries;
        busyEntries[tid].robEntries = fromCommit->commitInfo[tid].busyROBEntries;
        ROBHead[tid] = fromCommit->commitInfo[tid].ROBHead;
        numVROB[tid] = fromCommit->commitInfo[tid].numVROB;
    }

    LQHead[tid] = fromIEW->iewInfo[tid].LQHead;
    SQHead[tid] = fromIEW->iewInfo[tid].SQHead;

    DPRINTF(Rename, "[tid:%i]: Free IQ: %i, Free ROB: %i, "
                    "Free LQ: %i, Free SQ: %i\n",
            tid,
            freeEntries[tid].iqEntries,
            freeEntries[tid].robEntries,
            freeEntries[tid].lqEntries,
            freeEntries[tid].sqEntries);

    DPRINTF(Rename, "[tid:%i]: %i instructions not yet in ROB\n",
            tid, instsInProgress[tid]);
}

template <class Impl>
bool
DefaultRename<Impl>::checkSignalsAndUpdate(ThreadID tid)
{
    // Check if there's a squash signal, squash if there is
    // Check stall signals, block if necessary.
    // If status was blocked
    //     check if stall conditions have passed
    //         if so then go to unblocking
    // If status was Squashing
    //     check if squashing is not high.  Switch to running this cycle.
    // If status was serialize stall
    //     check if ROB is empty and no insts are in flight to the ROB

    readFreeEntries(tid);
    readStallSignals(tid);

    if (fromCommit->commitInfo[tid].squash) {
        DPRINTF(Rename, "[tid:%u]: Squashing instructions due to squash from "
                "commit.\n", tid);

        squash(fromCommit->commitInfo[tid].doneSeqNum, tid);

        return true;
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (renameStatus[tid] == Blocked) {
        DPRINTF(Rename, "[tid:%u]: Done blocking, switching to unblocking.\n",
                tid);

        renameStatus[tid] = Unblocking;
        DPRINTF(RenameBreakdown, "Rename Status of Thread [%u] switched to unblocking.\n",
                tid);

        unblock(tid);

        return true;
    }

    if (renameStatus[tid] == Squashing) {
        // Switch status to running if rename isn't being told to block or
        // squash this cycle.
        if (resumeSerialize) {
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to serialize.\n",
                    tid);

            renameStatus[tid] = SerializeStall;
            DPRINTF(RenameBreakdown, "Rename Status of Thread [%u] switched to"
                    " SerializeStall.\n", tid);
            return true;
        } else if (resumeUnblocking) {
            assert(0);
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to unblocking.\n",
                    tid);
            renameStatus[tid] = Unblocking;
            DPRINTF(RenameBreakdown, "Rename Status of Thread [%u] switched to"
                    " unblocking.\n", tid);
            return true;
        } else {
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to running.\n",
                    tid);

            renameStatus[tid] = Running;
            DPRINTF(RenameBreakdown, "Rename Status of Thread [%u] switched to"
                    " running.\n", tid);
            return false;
        }
    }

    if (renameStatus[tid] == SerializeStall) {
        // Stall ends once the ROB is free.
        DPRINTF(Rename, "[tid:%u]: Done with serialize stall, switching to "
                "unblocking.\n", tid);

        DynInstPtr serial_inst = serializeInst[tid];

        renameStatus[tid] = Unblocking;
        DPRINTF(RenameBreakdown, "Rename Status of Thread [%u] switched to"
                " unblocking.\n", tid);

        unblock(tid);

        DPRINTF(Rename, "[tid:%u]: Processing instruction [%lli] with "
                "PC %s.\n", tid, serial_inst->seqNum, serial_inst->pcState());

        // Put instruction into queue here.
        serial_inst->clearSerializeBefore();

        if (!skidBuffer[tid].empty()) {
            skidBuffer[tid].front().push_front(serial_inst);
        } else {
            insts[tid].push_front(serial_inst);
        }

        DPRINTF(Rename, "[tid:%u]: Instruction must be processed by rename."
                " Adding to front of list.\n", tid);

        serializeInst[tid] = NULL;
        tailSINext[tid] = false;

        finishSerialize[tid] = true;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause rename to change its status.  Rename remains the same as before.
    return false;
}

template<class Impl>
void
DefaultRename<Impl>::serializeAfter(InstRow &inst_list, ThreadID tid)
{
    if (inst_list.empty()) {
        // Mark a bit to say that I must serialize on the next instruction.
        serializeOnNextInst[tid] = true;
        return;
    }

    // Set the next instruction as serializing.
    inst_list.front()->setSerializeBefore();
}

template <class Impl>
inline void
DefaultRename<Impl>::incrFullStat(const typename SlotConsm::FullSource &source,
        ThreadID tid)
{
    switch (source) {
      case SlotConsm::ROB:
        ++renameROBFullEvents[tid];
        ++numROBFull[tid];
        break;
      case SlotConsm::Register:
        ++renameFullRegistersEvents;
        break;
      default:
        panic("Rename full stall stat should be incremented for a reason!");
        break;
    }
}

template <class Impl>
void
DefaultRename<Impl>::dumpHistory()
{
    typename std::list<RenameHistory>::iterator buf_it;

    for (ThreadID tid = 0; tid < numThreads; tid++) {

        buf_it = historyBuffer[tid].begin();

        while (buf_it != historyBuffer[tid].end()) {
            cprintf("Seq num: %i\nArch reg: %i New phys reg: %i Old phys "
                    "reg: %i\n", (*buf_it).instSeqNum, (int)(*buf_it).archReg,
                    (int)(*buf_it).newPhysReg, (int)(*buf_it).prevPhysReg);

            buf_it++;
        }
    }
}

template <class Impl>
void
DefaultRename<Impl>::setNrFreeRegs(unsigned _nrFreeRegs[], ThreadID _numThreads)
{
    assert(_numThreads == this->numThreads);
    for (ThreadID tid = 0; tid < _numThreads; ++tid) {
        // Use `+=' to work around with the pre-assignment for architectual regs.
        // nrFreeRegs[tid] might become negtive in that procedure.
        nrFreeRegs[tid] += _nrFreeRegs[tid];
    }
    // TODO Support set in runtime, which needs more checks for correctness.
    // TODO Support more register classes.
}

template <class Impl>
void
DefaultRename<Impl>::increaseFreeEntries()
{
    numFreeIntEntries += renameMap[0]->numFreeIntEntries();
    numFreeFloatEntries += renameMap[0]->numFreeFloatEntries();
}

template <class Impl>
void
DefaultRename<Impl>::resetFreeEntries()
{
    numFreeIntEntries = 0;
    numFreeFloatEntries = 0;
}

template <class Impl>
void
DefaultRename<Impl>::dumpFreeEntries()
{
    intRegUtilization = 1 - ((double) numFreeIntEntries /
        double(cpu->numPhysIntRegs*cpu->policyWindowSize));
    intRegUtilization = 1 - ((double) numFreeFloatEntries /
        double(cpu->numPhysFloatRegs*cpu->policyWindowSize));
    resetFreeEntries();
}


template <class Impl>
inline int
DefaultRename<Impl>::calcOwnROBEntries(ThreadID tid)
{
    int numInstsInFlight = 0;

    numInstsInFlight += toIEWNum[tid] + toROBNum[tid];

    return busyEntries[tid].robEntries + numInstsInFlight;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcOwnLQEntries(ThreadID tid)
{
    return busyEntries[tid].lqEntries + loadsInProgress[tid] -
        fromIEW->iewInfo[tid].dispatchedToLQ;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcOwnSQEntries(ThreadID tid)
{
    return busyEntries[tid].sqEntries + storesInProgress[tid] -
        fromIEW->iewInfo[tid].dispatchedToSQ;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcOwnIQEntries(ThreadID tid)
{
    return busyEntries[tid].iqEntries + instsInProgress[tid] -
        fromIEW->iewInfo[tid].dispatched;
}

template <class Impl>
void
DefaultRename<Impl>::passLB(ThreadID tid)
{
    MissDescriptor md;

    if (fullSource[tid] == SlotConsm::FullSource::ROB) {
        if (!ROBHead[tid]) {
            slotConsumer.queueHeadState[tid][SlotConsm::FullSource::ROB] =
                HeadInstrState::Normal;
            normalNoROBHead[tid] += 1;
            DPRINTF(missTry, "ROBHead[T%i] is NULL\n", tid);
        } else {
            DPRINTFR(missTry, "ROB Head[T%i][sn:%llu] ----: %s\n",
                    tid, ROBHead[tid]->seqNum ,dis(ROBHead[tid]));
            if (!ROBHead[tid]->readMiss) {
                ROBHead[tid]->DCacheMiss = missTables.isSpecifiedMiss(
                        ROBHead[tid]->physEffAddr, true, md);
                ROBHead[tid]->readMiss = true;
            }

            bool is_miss = ROBHead[tid]->DCacheMiss ||
                missTables.isSpecifiedMiss(ROBHead[tid]->physEffAddr, true, md) ||
                (ROBHead[tid]->isLoad() && ROBHead[tid]->physEffAddr == 0);

            if (!is_miss) {
                slotConsumer.queueHeadState[tid][SlotConsm::FullSource::ROB] =
                    HeadInstrState::Normal;
                normalHeadNotMiss[tid] += 1;
                DPRINTF(missTry3, "ROBHead[T%i] is not miss\n", tid);

#if 0
                printf("DEBUG--ROB Head[T%i][sn:%llu] ----: %s\n",
                         tid, ROBHead[tid]->seqNum ,dis(ROBHead[tid]));
                printf("Dcache miss flags: %i, miss table results: %i, "
                               "ROB Head addr is nulptr: %i",
                       ROBHead[tid]->DCacheMiss,
                       missTables.isSpecifiedMiss(ROBHead[tid]->physEffAddr, true, md),
                       (ROBHead[tid]->isLoad() && ROBHead[tid]->physEffAddr == 0)
                );
#endif
            } else {
                // trick
                if (!md.valid) {
                    slotConsumer.queueHeadState[tid][SlotConsm::FullSource::ROB] =
                            HeadInstrState::WaitingAddress;
                } else if (md.isCacheInterference) {
                    slotConsumer.queueHeadState[tid][SlotConsm::FullSource::ROB] =
                        static_cast<HeadInstrState>(
                                HeadInstrState::L1DCacheWait + md.missCacheLevel - 1);
                    DPRINTF(missTry, "ROBHead[T%i] miss is cache interference\n", tid);
                } else {
                    slotConsumer.queueHeadState[tid][SlotConsm::FullSource::ROB] =
                        static_cast<HeadInstrState>(
                                HeadInstrState::L1DCacheMiss + md.missCacheLevel - 1);
                    DPRINTF(missTry, "ROBHead[T%i] miss is not cache interference\n",
                            tid);
                }
            }
        }

        int in_flight = toIEWNum[tid] + toROBNum[tid];
        float calculated_VROB = numVROB[tid] + in_flight;
        bool VROB_full = calculated_VROB > maxEntries[tid].robEntries - 0.1;
        slotConsumer.vqState[tid][SlotConsm::FullSource::ROB] =
                VROB_full ? VQState::VQFull : VQState::VQNotFull;
        if (!VROB_full) {
            DPRINTF(missTry3, "ROB[T%i] from commit is %i, %i in flight\n", tid,
                    busyEntries[tid].robEntries, in_flight);
            DPRINTF(missTry3, "VROB[T%i] from commit is %f, %i in flight\n", tid,
                    numVROB[tid], in_flight);
            DPRINTF(missTry3, "VROB[T%i] is%s full\n", tid, VROB_full ? "" : " not");
        }

        if (slotConsumer.queueHeadState[tid][SlotConsm::FullSource::ROB]
                == HeadInstrState::Normal) {
            normalCount[tid]++;
        }
    }

    toIEW->loadRate = fromDecode->loadRate;
    toIEW->storeRate = fromDecode->storeRate;

    bool LB_to_decode;
    slotConsumer.cycleEnd(
            tid, toIEWNum, fullSource[tid], curCycleRow[tid],
            skidSlotBuffer[tid], this, true, fromIEW->iewInfo[HPT].BLB,
            renameStatus[tid] == SerializeStall, finishSerialize[tid],
            tailSI[tid], tailSINext[tid], LB_to_decode
    );
    tailSI[tid] = tailSINext[tid];
    toDecode->renameInfo[0].BLB = LB_to_decode;
}

template<class Impl>
void
DefaultRename<Impl>::computeMiss(ThreadID tid)
{
    if (tid != HPT) return;

    switch (renameStatus[tid]) {
        case Running:
        case Idle:
        case Unblocking:
        case Blocked:
            // wait or miss is uncertain yet
            break;

        case Squashing:
        case StartSquash:
            slotConsumer.consumeSlots(renameWidth, HPT, SlotsUse::SquashMiss);
            break;

        case SerializeStall:
            slotConsumer.consumeSlots(renameWidth, HPT, SlotsUse::SerializeMiss);
            break;

        default:
            break;
    }
}

template<class Impl>
void
DefaultRename<Impl>::missTry()
{

    DPRINTF(missTry, "====== HPT blocked ======!\n");

    MissStat &ms = missTables.missStat;
    ms.numL2DataMiss[HPT] = ms.numL2DataMiss[HPT];

    DPRINTFR(missTry, "Number of pending misses:\n"
            "L2 cache ---- T0: %i, T1: %i;\n"
            "L1 cache ---- T0: %i, T1: %i;\n",
             ms.numL2DataMiss[HPT], ms.numL2DataMiss[LPT],
             ms.numL1LoadMiss[HPT] + ms.numL1StoreMiss[HPT],
             ms.numL1LoadMiss[LPT] + ms.numL1StoreMiss[LPT]);

    DPRINTFR(missTry, "Queue utilization:\n"
            "ROB: T0 ---- %i,\t T1: %i;\n"
            "IQ: T0 ---- %i,\t T1: %i;\n"
            "LQ: T0 ---- %i,\t T1: %i;\n"
            "SQ: T0 ---- %i,\t T1: %i;\n",
            calcOwnROBEntries(HPT), calcOwnROBEntries(LPT),
            calcOwnIQEntries(HPT), calcOwnIQEntries(LPT),
            calcOwnLQEntries(HPT), calcOwnLQEntries(LPT),
            calcOwnSQEntries(HPT), calcOwnSQEntries(LPT));

    DPRINTFR(missTry, "ROB Head ---- T0: %s,\t  T1: %s\n"
            "LQ Head ---- T0: %s,\t  T1: %s\n"
            "SQ Head ---- T0: %s,\t  T1: %s\n",
            dis(ROBHead[HPT]), dis(ROBHead[LPT]),
            dis(LQHead[HPT]), dis(LQHead[LPT]),
            dis(SQHead[HPT]), dis(SQHead[LPT]));

    switch (fullSource[HPT]) {
        case SlotConsm::ROB:
            DPRINTF(missTry, "Block reason: ROB\n");
            break;
        case SlotConsm::IQ:
            DPRINTF(missTry, "Block reason: IQ\n");
            break;
        case SlotConsm::LQ:
            DPRINTF(missTry, "Block reason: LQ\n");
            break;
        case SlotConsm::SQ:
            DPRINTF(missTry, "Block reason: SQ\n");
            break;
        case SlotConsm::Register:
            DPRINTF(missTry, "Block reason: Register\n");
            break;
        case SlotConsm::IEWStage:
            DPRINTF(missTry, "Block reason: IEW blocked\n");
            break;
        default:
            DPRINTF(missTry, "Other reason\n");
    }

}

template<class Impl>
void
DefaultRename<Impl>::genShadow()
{
    DPRINTF(BMT, "genShadowing\n");

    DPRINTF(BMT, "HPT LQ full,  HPT: %i,  LPT %i\n",
            calcOwnLQEntries(HPT), calcOwnLQEntries(LPT));

    inShadow = true;
    shadowROB = maxEntries[HPT].robEntries - calcOwnROBEntries(HPT);

    InstSeqNum start = ~0, end = 0;

    for (MissTable::const_iterator it = missTables.l2MissTable.begin();
            it != missTables.l2MissTable.end(); it++) {
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
DefaultRename<Impl>::shine(const char *reason)
{
    DPRINTF(BMT, "Shining because of %s\n", reason);
    inShadow = false;
    shadowROB = 0;
}

template<class Impl>
void
DefaultRename<Impl>::clearFull()
{
    for(ThreadID tid = 0; tid < numThreads; tid++) {
        numROBFull[tid] = 0;
        numLQFull[tid] = 0;
        numSQFull[tid] = 0;
        numIQFull[tid] = 0;
    }
}

template<class Impl>
void
DefaultRename<Impl>::dumpStats()
{
    SlotCounter<Impl>::dumpStats();
}


template<class Impl>
void
DefaultRename<Impl>::clearLocalSignals()
{
    std::fill(finishSerialize.begin(), finishSerialize.end(), false);

    toIEWIndex = 0;
    std::fill(squashedThisCycle.begin(), squashedThisCycle.end(), false);

    LBLC = LB_all;

    LB_all = false;
    LB_part = false;
    numLPTcause = 0;
    blockedCycles = 0;

    toROBNum = toIEWNum;

    std::fill(toIEWNum.begin(), toIEWNum.end(), 0);
    std::fill(fullSource.begin(), fullSource.end(), SlotConsm::NONE);
}

#undef dis

#endif//__CPU_O3_RENAME_IMPL_HH__
