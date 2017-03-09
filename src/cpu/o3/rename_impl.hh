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
#include "debug/FmtSlot2.hh"
#include "debug/LB.hh"
#include "debug/InstPass.hh"
#include "debug/SI.hh"
#include "params/DerivO3CPU.hh"
#include "enums/OpClass.hh"

using namespace std;

template <class Impl>
DefaultRename<Impl>::DefaultRename(O3CPU *_cpu, DerivO3CPUParams *params)
    : SlotCounter<Impl>(params, params->renameWidth / params->numThreads),
      cpu(_cpu),
      iewToRenameDelay(params->iewToRenameDelay),
      decodeToRenameDelay(params->decodeToRenameDelay),
      commitToRenameDelay(params->commitToRenameDelay),
      renameWidth(params->renameWidth),
      renameWidths(params->numThreads, params->renameWidth / params->numThreads),
      commitWidth(params->commitWidth),
      toIEWNum(params->numThreads, 0),
      numThreads(params->numThreads),
      maxPhysicalRegs(params->numPhysIntRegs + params->numPhysFloatRegs
                      + params->numPhysCCRegs),
      availableInstCount(0),
      BLBlocal(false),
      renamable(params->numThreads, 0),
      LLmiss(params->numThreads, false),
      LLMInstSeq(params->numThreads, 0)
{
    if (renameWidth > Impl::MaxWidth)
        fatal("renameWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             renameWidth, static_cast<int>(Impl::MaxWidth));

    // @todo: Make into a parameter.
    skidBufferMax = (decodeToRenameDelay + 1) * params->decodeWidth;

    for (int i = 0; i < sizeof(this->nrFreeRegs) / sizeof(this->nrFreeRegs[0]); i++) {
        nrFreeRegs[i] = 0;
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

        instsInProgress[tid] = 0;
        loadsInProgress[tid] = 0;
        storesInProgress[tid] = 0;

        serializeOnNextInst[tid] = false;

        numROBFull[tid] = 0;
        numLQFull[tid] = 0;
        numSQFull[tid] = 0;
        numIQFull[tid] = 0;
    }
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
DefaultRename<Impl>::squash(const InstSeqNum &squash_seq_num, ThreadID tid)
{
    DPRINTF(Rename, "[tid:%u]: Squashing instructions.\n",tid);

    // Clear the stall signal if rename was blocked or unblocking before.
    // If it still needs to block, the blocking should happen the next
    // cycle and there should be space to hold everything due to the squash.
    if (renameStatus[tid] == Blocked ||
        renameStatus[tid] == Unblocking) {
        toDecode->renameUnblock[tid] = 1;
        BLBlocal = tid == HPT ? false : BLBlocal;

        resumeSerialize = false;
        serializeInst[tid] = NULL;
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
        }
    }

    // Set the status to Squashing.
    renameStatus[tid] = Squashing;
    DPRINTF(FmtSlot2, "Thread [%i] Rename status switched to Squashing\n", tid);

    // Squash any instructions from decode.
    for (int i=0; i<fromDecode->size; i++) {
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
    skidBuffer[tid].clear();

    doSquash(squash_seq_num, tid);
}

template <class Impl>
void
DefaultRename<Impl>::tick()
{
    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        toIEW->serialize[tid] = false;
        toIEW->unSerialize[tid] = false;
    }

    toIEWIndex = 0;

    LBLC = LB_all || LB_part;

    LB_all = false;
    LB_part = false;
    numLPTcause = 0;

    std::fill(toIEWNum.begin(), toIEWNum.end(), 0);

    sortInsts();

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    // Check stall and squash signals.
    for (ThreadID tid = 0; tid < numThreads; tid++) {

        DPRINTF(FmtSlot2, "Processing [tid:%i]\n", tid);
        LLmiss[tid] = fromIEW->iewInfo[tid].LLmiss;
        LLMInstSeq[tid] = fromIEW->iewInfo[tid].LLMInstSeq;
        status_change = checkSignalsAndUpdate(tid) || status_change;
    }

    getRenamable();

    for (ThreadID tid = 0; tid < numThreads; tid++) {

        computeMiss(tid);
        rename(status_change, tid);
    }

    if (renameStatus[HPT] == Blocked) {
        missTry();
    }

    clearAvailableInstCount();

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

    if (this->checkSlots(HPT)) {
        this->sumLocalSlots(HPT);
    }

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (toIEWNum[tid]) {
            DPRINTF(InstPass, "T[%i] send %i insts to IEW\n", tid, toIEWNum[tid]);
            this->assignSlots(tid, getHeadInst(tid));
        }
    }

    passLB(HPT);
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
                DPRINTF(FmtSlot2, "Thread [%i] status now:" "Running\n", tid);
                break;
            case Idle:
                DPRINTF(FmtSlot2, "Thread [%i] status now:" "Idle\n", tid);
                break;
            case StartSquash:
                DPRINTF(FmtSlot2, "Thread [%i] status now:" "StartSquash\n", tid);
                break;
            case Squashing:
                DPRINTF(FmtSlot2, "Thread [%i] status now:" "Squashing\n", tid);
                break;
            case Blocked:
                DPRINTF(FmtSlot2, "Thread [%i] status now:" "Blocked\n", tid);
                break;
            case Unblocking:
                DPRINTF(FmtSlot2, "Thread [%i] status now:" "Unblocking\n", tid);
                break;
            case SerializeStall:
                DPRINTF(FmtSlot2, "Thread [%i] status now:" "SerializeStall\n", tid);
                DPRINTF(SI, "T[%d] serialize on %llu\n", tid, curTick());
                break;
            default:
                break;
        }
    }

    if (renameStatus[tid] == Blocked) {
        ++renameBlockCycles[tid];
        if (fromIEW->iewInfo[HPT].BLB) {
            DPRINTF(FmtSlot2, "[Block Reason] LPT cause IEW stall\n");
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
    LB_part = tid == HPT ? false : LB_part;
    // Instructions can be either in the skid buffer or the queue of
    // instructions coming from decode, depending on the status.
    int insts_available = renameStatus[tid] == Unblocking ?
        skidBuffer[tid].size() : insts[tid].size();

    // Check the decode queue to see if instructions are available.
    // If there are no available instructions to rename, then do nothing.
    if (insts_available == 0) {
        DPRINTF(FmtSlot2, "[tid:%u]: Nothing to do, breaking out early.\n", tid);
        // Should I change status to idle?
        ++renameIdleCycles[tid];
        return;
    } else if (renameStatus[tid] == Unblocking) {
        ++renameUnblockCycles[tid];
    } else if (renameStatus[tid] == Running) {
        ++renameRunCycles[tid];
    }

    DynInstPtr inst;

    // Will have to do a different calculation for the number of free
    // entries.
    int free_rob_entries = calcFreeROBEntries(tid);
    int free_iq_entries  = calcFreeIQEntries(tid);
    int min_free_entries = free_rob_entries;

    int ROBcause = 0, IQcause = 0;
    // 当ROB或者IQ瓶颈时，造成的无法重命名的指令的数量

    FullSource source = ROB;
    int shortfall = 0;

    if (free_iq_entries < min_free_entries) {
        min_free_entries = free_iq_entries;
        source = IQ;
        // 如果空闲项足够，那么cause设置为0
        IQcause = std::max(renamable[tid] - min_free_entries, 0);
        shortfall = IQcause;
    } else {
        ROBcause = std::max(renamable[tid] - min_free_entries, 0);
        shortfall = ROBcause;
    }


    // Check if there's any space left.
    if (min_free_entries <= 0) {
        DPRINTF(FmtSlot2, "[tid:%u]: Blocking due to no free ROB/IQ/ "
                "entries.\n"
                "ROB has %i free entries.\n"
                "IQ has %i free entries.\n",
                tid,
                free_rob_entries,
                free_iq_entries);

        blockThisCycle = true;

        block(tid);

        incrFullStat(source, tid);

        if (tid == HPT) {
            if (source == IQ && calcOwnIQEntries(LPT)) {
                // 如果空闲项足够，那么矫正值为0
                numLPTcause = std::min(calcOwnIQEntries(LPT), shortfall);
                LB_all = numLPTcause == shortfall;
                LB_part = !LB_all && calcOwnIQEntries(LPT) > 0;

            } else if (source == ROB && calcOwnROBEntries(LPT)) {
                numLPTcause = std::min(calcOwnROBEntries(LPT), shortfall);
                LB_all = numLPTcause == shortfall;
                LB_part = !LB_all && calcOwnROBEntries(LPT) > 0;

            } else {
                LB_all = false;
                LB_part = false;
            }

            if (LLmiss[LPT]) {
                this->incLocalSlots(tid, EntryWait, shortfall);

            } else if (LLmiss[HPT]) {
                this->incLocalSlots(tid, EntryMiss, shortfall);

            } else if (LB_all) {
                this->incLocalSlots(tid, EntryWait, shortfall);

            } else if (LB_part) {
                this->incLocalSlots(tid, ComputeEntryWait, numLPTcause);
                this->incLocalSlots(tid, ComputeEntryMiss,
                        shortfall - numLPTcause);
            } else {
                this->incLocalSlots(tid, EntryMiss, shortfall);
            }

            renamable[tid] -= shortfall;
        }

        return;

    } else if (min_free_entries < insts_available) {
        DPRINTF(FmtSlot2, "[tid:%u]: Will have to block this cycle."
                "%i insts available, but only %i insts can be "
                "renamed due to ROB/IQ/LSQ limits.\n",
                tid, insts_available, min_free_entries);

        insts_available = min_free_entries;

        blockThisCycle = true;

        incrFullStat(source, tid);

        if (tid == HPT) {
            if (source == IQ && calcOwnIQEntries(LPT)) {
                numLPTcause = std::min(calcOwnIQEntries(LPT), shortfall);
                LB_part = calcOwnIQEntries(LPT) > 0;

            } else if (source == ROB && calcOwnROBEntries(LPT)) {
                numLPTcause = std::min(calcOwnROBEntries(LPT), shortfall);
                LB_part = calcOwnROBEntries(LPT) > 0;

            } else {
                LB_part = false;
            }

            if (LLmiss[LPT]) {
                this->incLocalSlots(tid, EntryWait, shortfall);

            } else if (LLmiss[HPT]) {
                this->incLocalSlots(tid, EntryMiss, shortfall);

            } else if (LB_part) {
                this->incLocalSlots(tid, ComputeEntryWait, numLPTcause);
                this->incLocalSlots(tid, ComputeEntryMiss,
                        shortfall - numLPTcause);
            } else {
                this->incLocalSlots(tid, EntryMiss, shortfall);
            }

            renamable[tid] -= shortfall;
        }
    }

    InstQueue &insts_to_rename = renameStatus[tid] == Unblocking ?
        skidBuffer[tid] : insts[tid];

    DPRINTF(Rename, "[tid:%u]: %i available instructions to "
            "send iew.\n", tid, insts_available);

    incAvailableInstCount(insts_available);

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

    while (insts_available > 0 &&  toIEWIndex < renameWidth &&
            toIEWNum[tid] < renameWidths[tid]) {
        DPRINTF(Rename, "[tid:%u]: Sending instructions to IEW.\n", tid);

        assert(!insts_to_rename.empty());

        inst = insts_to_rename.front();

        //For all kind of instructions, check ROB and IQ first

        //For load instruction, check LQ size
        //and take into account the inflight loads

        //For store instruction, check SQ size
        //and take into account the inflight stores

        if (inst->isLoad()) {
            if(calcFreeLQEntries(tid) <= 0) {
                DPRINTF(FmtSlot2, "[tid:%u]: Cannot rename due to no free LQ\n", tid);
                source = LQ;
                incrFullStat(source, tid);
                break;
            }
        }

        if (inst->isStore()) {
            if(calcFreeSQEntries(tid) <= 0) {
                DPRINTF(FmtSlot2, "[tid:%u]: Cannot rename due to no free SQ\n", tid);
                source = SQ;
                incrFullStat(source, tid);
                break;
            }
        }

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
            --renamable[tid];

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
            DPRINTF(FmtSlot2, "Blocking due to lack of free "
                    "physical registers to rename to.\n");
            blockThisCycle = true;
            insts_to_rename.push_front(inst);
            ++renameFullRegistersEvents;

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
        if ((inst->isIprAccess() || inst->isSerializeBefore()) &&
            !inst->isSerializeHandled()) {
            DPRINTF(FmtSlot2, "Serialize before instruction encountered.\n");

            if (!inst->isTempSerializeBefore()) {
                renamedSerializing++;
                inst->setSerializeHandled();
            } else {
                renamedTempSerializing++;
            }

            // Change status over to SerializeStall so that other stages know
            // what this is blocked on.
            renameStatus[tid] = SerializeStall;
            toIEW->serialize[tid] = true;
            DPRINTF(FmtSlot2, "Thread [%i] Rename status switched to SerializeStall\n",
                    tid);

            serializeInst[tid] = inst;

            blockThisCycle = true;

            break;
        } else if ((inst->isStoreConditional() || inst->isSerializeAfter()) &&
                   !inst->isSerializeHandled()) {
            DPRINTF(Rename, "Serialize after instruction encountered.\n");

            renamedSerializing++;

            inst->setSerializeHandled();

            serializeAfter(insts_to_rename, tid);
        }

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

        if (tid == HPT) {
            this->incLocalSlots(HPT, Base, 1);
        }

        // Decrement how many instructions are available.
        --insts_available;
        --renamable[tid];
    }

    if (tid == HPT && renamable[tid]) {
        //可能有很多指令因为LSQ满了而无法rename，要在此处进行判断
        if (source == LQ && calcOwnLQEntries(LPT)) {
            LB_part = true;
            numLPTcause = renamable[tid];
            DPRINTF(FmtSlot2, "[Block reason] %i insts cannot be renamed,"
                    " because of LQ\n", renamable[tid]);
        } else if (source == SQ && calcOwnSQEntries(LPT)) {
            LB_part = true;
            numLPTcause = renamable[tid];
            DPRINTF(FmtSlot2, "[Block reason] %i insts cannot be renamed,"
                    " because of SQ\n", renamable[tid]);
        } else {
            LB_part = false;
        }

        if (LLmiss[LPT] && numLPTcause) {
            this->incLocalSlots(tid, EntryWait, renamable[tid]);

        } else if (LLmiss[HPT]) {
            this->incLocalSlots(tid, EntryMiss, renamable[tid]);

        } else if(LB_part) {
            this->incLocalSlots(tid, ComputeEntryWait, numLPTcause);
            this->incLocalSlots(tid, ComputeEntryMiss, renamable[tid] - numLPTcause);
        } else {
            this->incLocalSlots(tid, ComputeEntryMiss, renamable[tid]);
        }
    }

    instsInProgress[tid] += renamed_insts;
    renameRenamedInsts += renamed_insts;

    // If we wrote to the time buffer, record this.
    if (toIEWIndex) {
        wroteToTimeBuffer = true;
    }

    // Check if there's any instructions left that haven't yet been renamed.
    // If so then block.
    if (insts_available) {
        blockThisCycle = true;
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
    DynInstPtr inst = NULL;

    while (!insts[tid].empty()) {
        inst = insts[tid].front();

        insts[tid].pop_front();

        assert(tid == inst->threadNumber);

        DPRINTF(Rename, "[tid:%u]: Inserting [sn:%lli] PC: %s into Rename "
                "skidBuffer\n", tid, inst->seqNum, inst->pcState());

        ++renameSkidInsts;

        /**这条指令即将被阻塞，说明它提前到达此阶段也无法提前被处理*/
        if (inst->getWaitSlot() > 0 && !skidBuffer[tid].empty()) {
            this->reshape(inst);
        }

        skidBuffer[tid].push_back(inst);
    }

    if (skidBuffer[tid].size() > skidBufferMax)
    {
        typename InstQueue::iterator it;
        warn("Skidbuffer contents:\n");
        for(it = skidBuffer[tid].begin(); it != skidBuffer[tid].end(); it++)
        {
            warn("[tid:%u]: %s [sn:%i].\n", tid,
                    (*it)->staticInst->disassemble(inst->instAddr()),
                    (*it)->seqNum);
        }
        panic("Skidbuffer Exceeded Max Size");
    }
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
    DPRINTF(FmtSlot2, "Total number of insts from decode is %i.\n", insts_from_decode);
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        DPRINTF(FmtSlot2, "Number of insts of Thread %i from decode is %i.\n",
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
    DPRINTF(FmtSlot2, "[tid:%u]: Blocking.\n", tid);

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
        }

        // Rename can not go from SerializeStall to Blocked, otherwise
        // it would not know to complete the serialize stall.
        if (renameStatus[tid] != SerializeStall) {
            // Set status to Blocked.
            renameStatus[tid] = Blocked;

            BLBlocal = tid == HPT ?
                fromIEW->iewInfo[0].BLB || LB_all : BLBlocal;

            DPRINTF(FmtSlot2, "Thread [%i] Rename status switched to Blocked\n", tid);
            return true;
        } else {
            DPRINTF(FmtSlot2, "Thread [%i] Rename status remains SerializeStall\n",
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
    DPRINTF(FmtSlot2, "[tid:%u]: Trying to unblock.\n", tid);

    // Rename is done unblocking if the skid buffer is empty.
    if (skidBuffer[tid].empty() && renameStatus[tid] != SerializeStall) {

        DPRINTF(Rename, "[tid:%u]: Done unblocking.\n", tid);

        toDecode->renameUnblock[tid] = true;
        BLBlocal = tid == HPT ? false : BLBlocal;
        wroteToTimeBuffer = true;

        DPRINTF(FmtSlot2, "Rename Status of Thread [%u] switched to Running.\n", tid);
        renameStatus[tid] = Running;

        return true;
    }
    DPRINTF(FmtSlot2, "Rename Status of Thread [%u] unchanged.\n", tid);

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
        int numInstsInFlight = availableInstCount;
        for (ThreadID t = 0; t < numThreads; t++) {
            numInstsInFlight +=
                (instsInProgress[t] - fromIEW->iewInfo[t].dispatched);
        }
        return freeEntries[tid].robEntries - numInstsInFlight;

    } else {
        return freeEntries[tid].robEntries
            - (instsInProgress[tid] - fromIEW->iewInfo[tid].dispatched);
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
                numAvailInsts = insts[tid].size();
                break;
            case Blocked:
            case Unblocking:
                numAvailInsts = skidBuffer[tid].size();
                break;
            default:
                break;
        }
        numLPTcause = std::min(numAvailInsts, renameWidths[tid]);
        DPRINTF(FmtSlot2, "T[0]: %i insts in skidbuffer, %i insts from decode\n",
                skidBuffer[tid].size(), insts[tid].size());
    }

    if (stalls[tid].iew) {
        DPRINTF(Rename,"[tid:%i]: Stall from IEW stage detected.\n", tid);
        ret_val = true;
        if (tid == HPT) {
            LB_all = fromIEW->iewInfo[0].BLB;
            DPRINTF(FmtSlot2, "HPT stall from IEW stage detected.\n");
        }

    } else if (calcFreeROBEntries(tid) <= 0) {
        DPRINTF(Rename,"[tid:%i]: Stall: ROB has 0 free entries.\n", tid);
        ret_val = true;

        if (tid == HPT) {
            /**即使没有LPT， HPT一样会stall*/
            LB_all = calcOwnROBEntries(LPT) >= renameWidths[HPT];
            LB_part = !LB_all && calcOwnROBEntries(LPT) > 0;
            numLPTcause = std::min(calcOwnROBEntries(LPT), numLPTcause);

            DPRINTF(FmtSlot2, "HPT stall because no ROB.\n");
        }

    } else if (calcFreeIQEntries(tid) <= 0) {
        /**为什么这里要检测？可不可以不检测？*/
        DPRINTF(Rename,"[tid:%i]: Stall: IQ has 0 free entries.\n", tid);
        ret_val = true;

        if (tid == HPT) {
            LB_all = calcOwnIQEntries(LPT) >= renameWidths[HPT];
            LB_part = !LB_all && calcOwnIQEntries(LPT) > 0;
            numLPTcause = std::min(calcOwnIQEntries(LPT), numLPTcause);

            DPRINTF(FmtSlot2, "HPT stall because no IQ.\n");
        }

    } else if (calcFreeLQEntries(tid) <= 0 && calcFreeSQEntries(tid) <= 0) {
        /**为什么这里要检测？可不可以不检测？*/
        DPRINTF(Rename,"[tid:%i]: Stall: LSQ has 0 free entries.\n", tid);
        ret_val = true;

        if (tid == HPT) {
            LB_all = (calcOwnLQEntries(LPT) > renameWidths[HPT]) ||
                (calcOwnSQEntries(LPT) > renameWidths[HPT]);
            LB_part = !LB_all && ((calcOwnLQEntries(LPT) > 0) ||
                (calcOwnSQEntries(LPT) > 0));
            numLPTcause = std::min(std::min(calcOwnLQEntries(LPT),
                        calcOwnSQEntries(LPT)), numLPTcause);

            DPRINTF(FmtSlot2, "HPT stall because no LSQ.\n");
        }

    } else if (renameMap[tid]->numFreeEntries() <= 0) {
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
    }

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
        DPRINTF(FmtSlot2, "Rename Status of Thread [%u] switched to unblocking.\n",
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
            DPRINTF(FmtSlot2, "Rename Status of Thread [%u] switched to"
                    " SerializeStall.\n", tid);
            return true;
        } else if (resumeUnblocking) {
            assert(0);
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to unblocking.\n",
                    tid);
            renameStatus[tid] = Unblocking;
            DPRINTF(FmtSlot2, "Rename Status of Thread [%u] switched to"
                    " unblocking.\n", tid);
            return true;
        } else {
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to running.\n",
                    tid);

            renameStatus[tid] = Running;
            DPRINTF(FmtSlot2, "Rename Status of Thread [%u] switched to"
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
        DPRINTF(FmtSlot2, "Rename Status of Thread [%u] switched to"
                " unblocking.\n", tid);

        unblock(tid);

        DPRINTF(Rename, "[tid:%u]: Processing instruction [%lli] with "
                "PC %s.\n", tid, serial_inst->seqNum, serial_inst->pcState());

        // Put instruction into queue here.
        serial_inst->clearSerializeBefore();

        if (!skidBuffer[tid].empty()) {
            skidBuffer[tid].push_front(serial_inst);
        } else {
            insts[tid].push_front(serial_inst);
        }

        DPRINTF(Rename, "[tid:%u]: Instruction must be processed by rename."
                " Adding to front of list.\n", tid);

        serializeInst[tid] = NULL;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause rename to change its status.  Rename remains the same as before.
    return false;
}

template<class Impl>
void
DefaultRename<Impl>::serializeAfter(InstQueue &inst_list, ThreadID tid)
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
DefaultRename<Impl>::incrFullStat(const FullSource &source,
        ThreadID tid)
{
    switch (source) {
      case ROB:
        ++renameROBFullEvents[tid];
        ++numROBFull[tid];
        break;
      case IQ:
        ++renameIQFullEvents[tid];
        ++numIQFull[tid];
        break;
      case LQ:
        ++renameLQFullEvents[tid];
        ++numLQFull[tid];
        break;
      case SQ:
        ++renameSQFullEvents[tid];
        ++numSQFull[tid];
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
        double(cpu->numPhysIntRegs*cpu->windowSize));
    intRegUtilization = 1 - ((double) numFreeFloatEntries /
        double(cpu->numPhysFloatRegs*cpu->windowSize));
    resetFreeEntries();
}

template <class Impl>
inline int
DefaultRename<Impl>::calcOwnROBEntries(ThreadID tid)
{
    return busyEntries[tid].robEntries;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcOwnLQEntries(ThreadID tid)
{
    return busyEntries[tid].lqEntries;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcOwnSQEntries(ThreadID tid)
{
    return busyEntries[tid].sqEntries;
}

template <class Impl>
inline int
DefaultRename<Impl>::calcOwnIQEntries(ThreadID tid)
{
    return busyEntries[tid].iqEntries;
}

template <class Impl>
void
DefaultRename<Impl>::passLB(ThreadID tid)
{
    switch(renameStatus[tid]) {
        case Blocked:
            toIEW->frontEndMiss = fromDecode->frontEndMiss;

            toDecode->renameInfo[tid].BLB =
                    LLmiss[LPT] || (!LLmiss[HPT] &&
                        (fromIEW->iewInfo[tid].BLB || LB_all));

            /**如果LB_part，那么这个周期没有LPT也会阻塞，肯定会阻塞上一个stage*/

            if (toDecode->renameInfo[tid].BLB) {
                if (LB_all) {
                    DPRINTF(LB, "Send BLB to Decode because of local detection\n");
                } else {
                    DPRINTF(LB, "Forward BLB from IEW to Decode\n");
                }
            }
            break;

        case Running:
        case Idle:
            toIEW->frontEndMiss = fromDecode->frontEndMiss;
            assert(!fromIEW->iewInfo[tid].BLB);
            assert(!(LB_all || LB_part));
            toDecode->renameInfo[tid].BLB = false;
            /** 在rename或者decode的running阶段，有可能带宽是没有用满的，
              * 但是没关系，因为如果是wait，下面的指令一定记录了，
              * 如果是miss，我们不用管
              */
            break;

        case Unblocking:
            toIEW->frontEndMiss = fromDecode->frontEndMiss;
            assert(!fromIEW->iewInfo[tid].BLB);
            assert(!(LB_all || LB_part));
            /**如果此前有BLB，那么此时应该让告诉decode：这是LPT的锅*/
            toDecode->renameInfo[tid].BLB = BLBlocal;

            if (BLBlocal) {
                DPRINTF(LB, "Send BLB to Decode becaues of BLBlocal\n");
            }
            if (toIEWNum[tid] < renameWidths[tid] && LBLC) {
                DPRINTF(LB, "LPT blocked HPT in last cycle, leads to bandwidth waste,"
                        "in this cycle\n");
            }
            break;

        case StartSquash:
        case Squashing:
            toIEW->frontEndMiss = true;
            toDecode->renameInfo[tid].BLB = false;
            DPRINTF(LB, "No BLB because of Squashing\n");
            break;

        case SerializeStall:
            toIEW->frontEndMiss = true; // 这里是1或者tid有待进一步思考
            toDecode->renameInfo[tid].BLB = false;
            DPRINTF(LB, "No BLB because of SerializeStall\n");
            break;
    }
}

template <class Impl>
void
DefaultRename<Impl>::getRenamable() {
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        switch (renameStatus[tid]) {
            case Running:
            case Idle:
                renamable[tid] = insts[tid].size();
                DPRINTF(FmtSlot2, "T[%i][Running] has %i insts to rename\n",
                        tid, renamable[tid]);
                break;

            case Blocked:
                renamable[tid] = std::min((int) skidBuffer[tid].size(),
                        (int) renameWidths[tid]);
                DPRINTF(FmtSlot2, "T[%i][Blocked] has %i insts to rename\n",
                        tid, renamable[tid]);
                break;

            case Unblocking:
                renamable[tid] = std::min((int) skidBuffer[tid].size(),
                        (int) renameWidths[tid]);
                DPRINTF(FmtSlot2, "T[%i][Unblocking] has %i insts to rename\n",
                        tid, renamable[tid]);
                /**should rename*/
                if(!fromDecode->frontEndMiss && LBLC) {
                    DPRINTF(FmtSlot2, "T[%i] has %i insts should rename\n",
                            tid, renameWidths[tid]);
                }
                break;

            case Squashing:
            case StartSquash:
                renamable[tid] = 0;
                DPRINTF(FmtSlot2, "T[%i][Squash] has no insts to rename\n", tid);
                break;

            case SerializeStall:
                renamable[tid] = std::min((int) skidBuffer[tid].size(),
                        (int) renameWidths[tid]);
                DPRINTF(FmtSlot2, "T[%i][Serialize Blocked] has %i insts to rename\n",
                        tid, renamable[tid]);
                break;
        }
    }
}

template<class Impl>
void
DefaultRename<Impl>::computeMiss(ThreadID tid)
{
    if (tid != HPT) return;

    switch (renameStatus[tid]) {
        case Running:
        case Idle:
            if (renamable[tid] < renameWidths[tid]) {
                int wasted = renameWidths[tid] - renamable[tid];
                this->incLocalSlots(HPT, InstMiss, wasted);

                DPRINTF(FmtSlot2, "T[%i] wastes %i slots because insts not enough\n",
                        tid, wasted);
            }
            break;

        case Unblocking:
            if (renamable[tid] < renameWidths[tid]) {
                int wasted = renameWidths[tid] - renamable[tid];
                if (fromDecode->frontEndMiss || !LBLC) {
                    this->incLocalSlots(HPT, InstMiss, wasted);
                } else {
                    this->incLocalSlots(HPT, LBLCWait, wasted);
                }
            }
            break;

        case Blocked:
            /**block一定导致本周期miss */
            if (fromDecode->frontEndMiss) {
                this->incLocalSlots(HPT, InstMiss, renameWidths[tid]);
            } else {
                if (LLmiss[LPT]) {
                    this->incLocalSlots(HPT, EntryWait, renameWidths[tid]);

                } else if (LLmiss[HPT]) {
                    this->incLocalSlots(HPT, EntryMiss, renameWidths[tid]);

                } else if (fromIEW->iewInfo[0].BLB) {
                    this->incLocalSlots(HPT, InstMiss,
                            renameWidths[tid] - numLPTcause);
                    this->incLocalSlots(HPT, LaterWait, numLPTcause);

                } else if (LB_all) {
                    this->incLocalSlots(HPT, InstMiss,
                            renameWidths[tid] - numLPTcause);
                    this->incLocalSlots(HPT, EntryWait, numLPTcause);

                } else if (LB_part) {
                    this->incLocalSlots(HPT, ComputeEntryWait, numLPTcause);
                    this->incLocalSlots(HPT, ComputeEntryMiss,
                            renamable[HPT] - numLPTcause);
                    this->incLocalSlots(HPT, InstMiss,
                            renameWidths[HPT] - renamable[HPT]);
                } else {
                    this->incLocalSlots(HPT, EntryMiss, renamable[HPT]);
                    this->incLocalSlots(HPT, InstMiss,
                            renameWidths[HPT] - renamable[HPT]);
                }
            }
            break;

        case Squashing:
        case StartSquash:
            /**Squash一定导致本周期miss*/
            this->incLocalSlots(HPT, InstMiss, renameWidths[tid]);

            DPRINTF(FmtSlot2, "T[%i] wastes %i slots because blocked or squash\n",
                    tid, renameWidths[tid]);
            break;

        case SerializeStall:
            this->incLocalSlots(HPT, SerializeMiss, renameWidths[tid]);
            break;

        default:
            break;
    }
}

template<class Impl>
void
DefaultRename<Impl>::missTry()
{
}


#endif//__CPU_O3_RENAME_IMPL_HH__
