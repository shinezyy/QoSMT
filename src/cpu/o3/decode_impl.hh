/* 
 * Copyright (c) 2012, 2014 ARM Limited
 * All rights reserved
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

#ifndef __CPU_O3_DECODE_IMPL_HH__
#define __CPU_O3_DECODE_IMPL_HH__

#include <algorithm>

#include "arch/types.hh"
#include "base/trace.hh"
#include "config/the_isa.hh"
#include "cpu/o3/decode.hh"
#include "cpu/inst_seq.hh"
#include "debug/Activity.hh"
#include "debug/Decode.hh"
#include "debug/O3PipeView.hh"
#include "debug/FmtSlot2.hh"
#include "debug/LB.hh"
#include "debug/Pard.hh"
#include "debug/InstPass.hh"
#include "debug/DecodeBreakdown.hh"
#include "params/DerivO3CPU.hh"
#include "sim/full_system.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

template<class Impl>
DefaultDecode<Impl>::DefaultDecode(O3CPU *_cpu, DerivO3CPUParams *params)
    : SlotCounter<Impl>(params, params->decodeWidth),
      cpu(_cpu),
      renameToDecodeDelay(params->renameToDecodeDelay),
      iewToDecodeDelay(params->iewToDecodeDelay),
      commitToDecodeDelay(params->commitToDecodeDelay),
      fetchToDecodeDelay(params->fetchToDecodeDelay),
      decodeWidth(params->decodeWidth),
      numThreads((ThreadID) params->numThreads),
      BLBlocal(false),
      sampleLen(params->sampleLen)
{
    if (decodeWidth > Impl::MaxWidth)
        fatal("decodeWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             decodeWidth, static_cast<int>(Impl::MaxWidth));

    // @todo: Make into a parameter
    skidBufferMax = (unsigned)fetchToDecodeDelay + 1;

    for (int i = LDST::load; i < LDST::NumType; i++) {
        bzero((void *) ldstSample[i], sizeof(int) * sampleLen);
        ldstRate[i] = 0;
        ldstNum[i] = 0;
        ldstIndex[i] = 0;
    }
}

template<class Impl>
void
DefaultDecode<Impl>::startupStage()
{
    resetStage();
}

template<class Impl>
void
DefaultDecode<Impl>::resetStage()
{
    _status = Inactive;

    // Setup status, make sure stall signals are clear.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        decodeStatus[tid] = Idle;

        stalls[tid].rename = false;
    }
}

template <class Impl>
std::string
DefaultDecode<Impl>::name() const
{
    return cpu->name() + ".decode";
}

template <class Impl>
void
DefaultDecode<Impl>::regStats()
{
    SlotCounter<Impl>::regStats();
    decodeIdleCycles
        .name(name() + ".IdleCycles")
        .desc("Number of cycles decode is idle")
        .prereq(decodeIdleCycles);
    decodeBlockedCycles
        .name(name() + ".BlockedCycles")
        .desc("Number of cycles decode is blocked")
        .prereq(decodeBlockedCycles);
    threadDecodeBlockedCycles
        .init(cpu->numThreads)
        .name(name() + ".threadBlockedCycles")
        .desc("Number of cycles decode is blocked each thread")
        .flags(Stats::pdf);
    decodeRunCycles
        .name(name() + ".RunCycles")
        .desc("Number of cycles decode is running")
        .prereq(decodeRunCycles);
    decodeUnblockCycles
        .name(name() + ".UnblockCycles")
        .desc("Number of cycles decode is unblocking")
        .prereq(decodeUnblockCycles);
    decodeSquashCycles
        .name(name() + ".SquashCycles")
        .desc("Number of cycles decode is squashing")
        .prereq(decodeSquashCycles);
    decodeBranchResolved
        .name(name() + ".BranchResolved")
        .desc("Number of times decode resolved a branch")
        .prereq(decodeBranchResolved);
    decodeBranchMispred
        .name(name() + ".BranchMispred")
        .desc("Number of times decode detected a branch misprediction")
        .prereq(decodeBranchMispred);
    decodeControlMispred
        .name(name() + ".ControlMispred")
        .desc("Number of times decode detected an instruction incorrectly"
              " predicted as a control")
        .prereq(decodeControlMispred);
    decodeDecodedInsts
        .name(name() + ".DecodedInsts")
        .desc("Number of instructions handled by decode")
        .prereq(decodeDecodedInsts);
    decodeSquashedInsts
        .name(name() + ".SquashedInsts")
        .desc("Number of squashed instructions handled by decode")
        .prereq(decodeSquashedInsts);
}

template<class Impl>
void
DefaultDecode<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to write information back to fetch.
    toFetch = timeBuffer->getWire(0);

    // Create wires to get information from proper places in time buffer.
    fromRename = timeBuffer->getWire(- (int) renameToDecodeDelay);
    fromIEW = timeBuffer->getWire(- (int) iewToDecodeDelay);
    fromCommit = timeBuffer->getWire(- (int) commitToDecodeDelay);
}

template<class Impl>
void
DefaultDecode<Impl>::setDecodeQueue(TimeBuffer<DecodeStruct> *dq_ptr)
{
    decodeQueue = dq_ptr;

    // Setup wire to write information to proper place in decode queue.
    toRename = decodeQueue->getWire(0);
}

template<class Impl>
void
DefaultDecode<Impl>::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr)
{
    fetchQueue = fq_ptr;

    // Setup wire to read information from fetch queue.
    fromFetch = fetchQueue->getWire(- (int) fetchToDecodeDelay);
}

template<class Impl>
void
DefaultDecode<Impl>::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

template <class Impl>
void
DefaultDecode<Impl>::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        assert(insts[tid].empty());
        assert(skidBuffer[tid].empty());
    }
}

template <class Impl>
bool
DefaultDecode<Impl>::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (!insts[tid].empty() || !skidBuffer[tid].empty())
            return false;
    }
    return true;
}

template<class Impl>
bool
DefaultDecode<Impl>::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (stalls[tid].rename) {
        DPRINTF(Decode,"[tid:%i]: Stall fom Rename stage detected.\n", tid);
        ret_val = true;
    }

    return ret_val;
}

template<class Impl>
inline bool
DefaultDecode<Impl>::fetchInstsValid()
{
    return fromFetch->size > 0;
}

template<class Impl>
bool
DefaultDecode<Impl>::block(ThreadID tid)
{
    DPRINTF(Decode, "[tid:%u]: Blocking.\n", tid);

    // Add the current inputs to the skid buffer so they can be
    // reprocessed when this stage unblocks.
    skidInsert(tid);

    // If the decode status is blocked or unblocking then decode has not yet
    // signalled fetch to unblock. In that case, there is no need to tell
    // fetch to block.
    if (decodeStatus[tid] != Blocked) {
        // Set the status to Blocked.
        decodeStatus[tid] = Blocked;

        BLBlocal = tid == HPT ? fromRename->renameInfo[0].BLB : BLBlocal;

        if (toFetch->decodeUnblock[tid]) {
            toFetch->decodeUnblock[tid] = false;
        } else {
            toFetch->decodeBlock[tid] = true;
            wroteToTimeBuffer = true;
        }

        return true;
    }

    return false;
}

template<class Impl>
bool
DefaultDecode<Impl>::unblock(ThreadID tid)
{
    // Decode is done unblocking only if the skid buffer is empty.
    if (skidBuffer[tid].empty()) {
        DPRINTF(Decode, "[tid:%u]: Done unblocking.\n", tid);
        toFetch->decodeUnblock[tid] = true;
        BLBlocal = tid == HPT ? false : BLBlocal;
        wroteToTimeBuffer = true;

        decodeStatus[tid] = Running;
        return true;
    }

    DPRINTF(Decode, "[tid:%u]: Currently unblocking.\n", tid);

    return false;
}

template<class Impl>
void
DefaultDecode<Impl>::squash(DynInstPtr &inst, ThreadID tid)
{
    DPRINTF(Decode, "[tid:%i]: [sn:%i] Squashing due to incorrect branch "
            "prediction detected at decode.\n", tid, inst->seqNum);
    squashedThisCycle[tid] = true;

    // Send back mispredict information.
    toFetch->decodeInfo[tid].branchMispredict = true;
    toFetch->decodeInfo[tid].predIncorrect = true;
    toFetch->decodeInfo[tid].mispredictInst = inst;
    toFetch->decodeInfo[tid].squash = true;
    toFetch->decodeInfo[tid].doneSeqNum = inst->seqNum;
    toFetch->decodeInfo[tid].nextPC = inst->branchTarget();
    toFetch->decodeInfo[tid].branchTaken = inst->pcState().branching();
    toFetch->decodeInfo[tid].squashInst = inst;
    if (toFetch->decodeInfo[tid].mispredictInst->isUncondCtrl()) {
            toFetch->decodeInfo[tid].branchTaken = true;
    }

    InstSeqNum squash_seq_num = inst->seqNum;

    // Might have to tell fetch to unblock.
    if (decodeStatus[tid] == Blocked ||
        decodeStatus[tid] == Unblocking) {
        BLBlocal = tid == HPT ? false : BLBlocal;
        toFetch->decodeUnblock[tid] = 1;
    }

    // Set status to squashing.
    decodeStatus[tid] = Squashing;

    for (int i=0; i<fromFetch->size; i++) {
        if (fromFetch->insts[i]->threadNumber == tid &&
            fromFetch->insts[i]->seqNum > squash_seq_num) {
            fromFetch->insts[i]->setSquashed();
        }
    }

    // Clear the instruction list and skid buffer in case they have any
    // insts in them.
    while (!insts[tid].empty()) {
        insts[tid].pop();
    }

    while (!skidBuffer[tid].empty()) {
        if (!skidBuffer[tid].front().empty()) {
            DPRINTF(DecodeBreakdown, "Squash sn[%lli] from skidbuffer of T[%i]\n",
                    skidBuffer[tid].front().front()->seqNum, tid);
        }

#if 0
        InstRow &popped = skidBuffer[tid].front();
        while (!popped.empty()) {
            DynInstPtr inst = popped.front();
            DPRINTFR(DecodeBreakdown, "sn[%i] ", inst->seqNum);
            popped.pop();
        }

        DPRINTFR(DecodeBreakdown, "\nSquashed------------------\n");
#endif

        skidBuffer[tid].pop();
        skidInstTick[tid].pop();
    }

    while (!skidSlotBuffer[tid].empty()) {
        skidSlotBuffer[tid].pop();
        skidSlotTick[tid].pop();
    }

    // Squash instructions up until this one
    cpu->removeInstsUntil(squash_seq_num, tid);
}

template<class Impl>
unsigned
DefaultDecode<Impl>::squash(ThreadID tid)
{
    DPRINTF(Decode, "[tid:%i]: Squashing.\n",tid);
    squashedThisCycle[tid] = true;

    if (decodeStatus[tid] == Blocked ||
        decodeStatus[tid] == Unblocking) {
        if (FullSystem) {
            toFetch->decodeUnblock[tid] = 1;
        } else {
            // In syscall emulation, we can have both a block and a squash due
            // to a syscall in the same cycle.  This would cause both signals
            // to be high.  This shouldn't happen in full system.
            // @todo: Determine if this still happens.
            if (toFetch->decodeBlock[tid]) {
                toFetch->decodeBlock[tid] = 0;
            } else {
                toFetch->decodeUnblock[tid] = 1;
            }
        }
        BLBlocal = tid == HPT ? false : BLBlocal;
    }

    // Set status to squashing.
    decodeStatus[tid] = Squashing;

    // Go through incoming instructions from fetch and squash them.
    unsigned squash_count = 0;

    for (int i=0; i<fromFetch->size; i++) {
        if (fromFetch->insts[i]->threadNumber == tid) {
            fromFetch->insts[i]->setSquashed();
            squash_count++;
        }
    }

    // Clear the instruction list and skid buffer in case they have any
    // insts in them.
    while (!insts[tid].empty()) {
        insts[tid].pop();
    }

    while (!skidBuffer[tid].empty()) {
        if (!skidBuffer[tid].front().empty()) {
            DPRINTF(DecodeBreakdown, "Squash sn[%lli] from skidbuffer of T[%i]\n",
                    skidBuffer[tid].front().front()->seqNum, tid);
        }

#if 0
        InstRow &popped = skidBuffer[tid].front();
        while (!popped.empty()) {
            DynInstPtr inst = popped.front();
            DPRINTFR(DecodeBreakdown, "sn[%i] ", inst->seqNum);
            popped.pop();
        }

        DPRINTF(DecodeBreakdown, "\nSquashed------------------\n");
#endif

        skidBuffer[tid].pop();
        skidInstTick[tid].pop();
    }

    while (!skidSlotBuffer[tid].empty()) {
        skidSlotBuffer[tid].pop();
        skidSlotTick[tid].pop();
    }

    return squash_count;
}

template<class Impl>
void
DefaultDecode<Impl>::skidInsert(ThreadID tid)
{
    if (insts[tid].size() > 0) {
        DPRINTF(DecodeBreakdown, "Inserting sn[%lli] into skidbuffer of T[%i]\n",
                insts[tid].front()->seqNum, tid);
        skidBuffer[tid].push(insts[tid]);
        skidInstTick[tid].push(curTick());

        InstRow &popped = insts[tid];
        while (!popped.empty()) {
#if 0
            DynInstPtr inst = popped.front();
            DPRINTFR(DecodeBreakdown, "sn[%i] ", inst->seqNum);
#endif
            popped.pop();
        }

        DPRINTFR(DecodeBreakdown, "\nInserted------------------\n");


        skidSlotBuffer[tid].push(fromFetch->slotPass);
        skidSlotTick[tid].push(curTick());
        assert(skidBuffer[tid].size() <= skidBufferMax);
    }
}

template<class Impl>
bool
DefaultDecode<Impl>::skidsEmpty()
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
DefaultDecode<Impl>::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (decodeStatus[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Decode will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(Activity, "Activating stage.\n");

            cpu->activateStage(O3CPU::DecodeIdx);
        }
    } else {
        // If it's not unblocking, then decode will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(Activity, "Deactivating stage.\n");

            cpu->deactivateStage(O3CPU::DecodeIdx);
        }
    }
}

template <class Impl>
void
DefaultDecode<Impl>::sortInsts()
{
    int insts_from_fetch = fromFetch->size;
    for (int i = 0; i < insts_from_fetch; ++i) {
        insts[fromFetch->insts[i]->threadNumber].push(fromFetch->insts[i]);
    }
}

template<class Impl>
void
DefaultDecode<Impl>::readStallSignals(ThreadID tid)
{
    if (fromRename->renameBlock[tid]) {
        stalls[tid].rename = true;
    }

    if (fromRename->renameUnblock[tid]) {
        assert(stalls[tid].rename);
        stalls[tid].rename = false;
    }
}

template <class Impl>
bool
DefaultDecode<Impl>::checkSignalsAndUpdate(ThreadID tid)
{
    // Check if there's a squash signal, squash if there is.
    // Check stall signals, block if necessary.
    // If status was blocked
    //     Check if stall conditions have passed
    //         if so then go to unblocking
    // If status was Squashing
    //     check if squashing is not high.  Switch to running this cycle.

    // Update the per thread stall statuses.
    readStallSignals(tid);

    // Check squash signals from commit.
    if (fromCommit->commitInfo[tid].squash) {

        DPRINTF(Decode, "[tid:%u]: Squashing instructions due to squash "
                "from commit.\n", tid);

        squash(tid);

        return true;
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (decodeStatus[tid] == Blocked) {
        DPRINTF(Decode, "[tid:%u]: Done blocking, switching to unblocking.\n",
                tid);

        decodeStatus[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (decodeStatus[tid] == Squashing) {
        // Switch status to running if decode isn't being told to block or
        // squash this cycle.
        DPRINTF(Decode, "[tid:%u]: Done squashing, switching to running.\n",
                tid);

        decodeStatus[tid] = Running;

        return false;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause decode to change its status.  Decode remains the same as before.
    return false;
}

template<class Impl>
void
DefaultDecode<Impl>::tick()
{
    wroteToTimeBuffer = false;

    bool status_change = false;

    toRenameIndex = 0;
    std::fill(toRenameNum.begin(), toRenameNum.end(), 0);
    std::fill(squashedThisCycle.begin(), squashedThisCycle.end(), false);
    std::fill(numSquashedThisCycle.begin(), numSquashedThisCycle.end(), 0);

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    sortInsts();

    //Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(Decode,"Processing [tid:%i]\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        decode(status_change, tid);
    }

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity this cycle.\n");

        cpu->activityThisCycle();
    }

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (toRenameNum[tid]) {
            DPRINTF(InstPass, "T[%i] send %i insts to Rename\n", tid,
                    toRenameNum[tid]);
        }
    }

    passLB(HPT);

    if (this->countSlot(HPT, SlotsUse::Base) != toRenameNum[HPT]) {
        this->printSlotRow(this->slotUseRow[HPT], decodeWidth);
        panic("Slots [%i] and Insts [%i] are not coherence!\n",
                this->countSlot(HPT, SlotsUse::Base), toRenameNum[HPT]);
    }
    toRename->slotPass = this->slotUseRow[HPT];

    toRename->loadRate = ldstRate[LDST::load];
    toRename->storeRate = ldstRate[LDST::store];

    if (this->checkSlots(HPT)) {
        this->sumLocalSlots(HPT);
    }

    // DPRINTF(Pard, "Index of cur cycles: %i\n", storeIndex);
}

template<class Impl>
void
DefaultDecode<Impl>::decode(bool &status_change, ThreadID tid)
{
    // If status is Running or idle,
    //     call decodeInsts()
    // If status is Unblocking,
    //     buffer any instructions coming from fetch
    //     continue trying to empty skid buffer
    //     check if stall conditions have passed

    if (decodeStatus[tid] == Blocked) {
        ++decodeBlockedCycles;
        ++threadDecodeBlockedCycles[tid];
        //DPRINTF(Pard, "Index not move: %i\n", storeIndex);
    } else if (decodeStatus[tid] == Squashing) {
        ++decodeSquashCycles;

        if (HPT == tid) {
            noLoadStoreThisCycle();
        }
    }

    // Decode should try to decode as many instructions as its bandwidth
    // will allow, as long as it is not currently blocked.
    if (decodeStatus[tid] == Running ||
        decodeStatus[tid] == Idle) {
        DPRINTF(Decode, "[tid:%u]: Not blocked, so attempting to run "
                "stage.\n",tid);

        decodeInsts(tid);
    } else if (decodeStatus[tid] == Unblocking) {
        // Make sure that the skid buffer has something in it if the
        // status is unblocking.
        assert(!skidsEmpty());

        // If the status was unblocking, then instructions from the skid
        // buffer were used.  Remove those instructions and handle
        // the rest of unblocking.
        decodeInsts(tid);

        if (fetchInstsValid()) {
            // Add the current inputs to the skid buffer so they can be
            // reprocessed when this stage unblocks.
            skidInsert(tid);
        }

        status_change = unblock(tid) || status_change;
    }

}

template <class Impl>
void
DefaultDecode<Impl>::decodeInsts(ThreadID tid)
{
    // Instructions can come either from the skid buffer or the list of
    // instructions coming from fetch, depending on decode's status.

    int skid_size = 0;
    if (skidBuffer[tid].size() != 0) {
        skid_size = skidBuffer[tid].front().size();
        if (skid_size <= 0) {
            DPRINTF(DecodeBreakdown, "T[%i]'s skidBuffer has empty row\n", tid);
            panic("T[%i]'s skidBuffer has empty row\n", tid);
        }
    }

    int insts_available =
            decodeStatus[tid] == Unblocking ?
            skid_size : (int) insts[tid].size();

    if (insts_available == 0) {
        DPRINTF(Decode, "[tid:%u] Nothing to do, breaking out"
                " early.\n",tid);
        // Should I change the status to idle?
        ++decodeIdleCycles;

        if (HPT == tid) {
            noLoadStoreThisCycle();
        }
        return;
    } else if (decodeStatus[tid] == Unblocking) {
        DPRINTF(Decode, "[tid:%u] Unblocking, removing insts from skid "
                "buffer.\n",tid);
        ++decodeUnblockCycles;
    } else if (decodeStatus[tid] == Running) {
        ++decodeRunCycles;
    }

    if (HPT == tid) {
        for (int i = LDST::load; i < LDST::NumType; i++) {
            ldstNum[i] -= ldstSample[i][ldstIndex[i]];
            ldstSample[i][ldstIndex[i]] = 0;
        }
    }

    DynInstPtr inst;

    InstRow &insts_to_decode = decodeStatus[tid] == Unblocking ?
        skidBuffer[tid].front() : insts[tid];

    curCycleRow[tid] = decodeStatus[tid] == Unblocking ?
            skidSlotBuffer[tid].front() : fromFetch->slotPass;

    if (decodeStatus[tid] == Unblocking) {
        DPRINTF(DecodeBreakdown, "skid inst tick: %llu, skid slot tick: %llu\n",
                skidInstTick[tid].front(), skidSlotTick[tid].front());
        assert(skidInstTick[tid].front() == skidSlotTick[tid].front());
    }

    ThreadStatus old_status = decodeStatus[tid];

    DPRINTF(Decode, "[tid:%u]: Sending instruction to rename.\n",tid);

    while (insts_available > 0 && toRenameIndex < decodeWidth) {
        assert(!insts_to_decode.empty());

        inst = insts_to_decode.front();

        insts_to_decode.pop();

        DPRINTF(Decode, "[tid:%u]: Processing instruction [sn:%lli] with "
                "PC %s\n", tid, inst->seqNum, inst->pcState());

        if (inst->isSquashed()) {
            DPRINTF(Decode, "[tid:%u]: Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++decodeSquashedInsts;
            ++numSquashedThisCycle[tid];

            --insts_available;

            continue;
        }

        // Also check if instructions have no source registers.  Mark
        // them as ready to issue at any time.  Not sure if this check
        // should exist here or at a later stage; however it doesn't matter
        // too much for function correctness.
        if (inst->numSrcRegs() == 0) {
            inst->setCanIssue();
        }

        DPRINTF(DecodeBreakdown, "T[%i] sends instruction [sn:%lli] to Rename\n",
                tid, inst->seqNum);

        // This current instruction is valid, so add it into the decode
        // queue.  The next instruction may not be valid, so check to
        // see if branches were predicted correctly.
        toRename->insts[toRenameIndex] = inst;

        if (HPT == tid) {
            if (inst->isStore()) {
                ldstSample[LDST::store][ldstIndex[LDST::store]]++;
            }
            if (inst->isLoad()) {
                ldstSample[LDST::load][ldstIndex[LDST::load]]++;
            }
        }

        ++(toRename->size);
        ++toRenameIndex;
        ++toRenameNum[tid];
        ++decodeDecodedInsts;
        --insts_available;

#if TRACING_ON
        if (DTRACE(O3PipeView)) {
            inst->decodeTick = curTick() - inst->fetchTick;
        }
#endif

        // Ensure that if it was predicted as a branch, it really is a
        // branch.
        if (inst->readPredTaken() && !inst->isControl()) {
            panic("Instruction predicted as a branch!");

            ++decodeControlMispred;

            // Might want to set some sort of boolean and just do
            // a check at the end
            squash(inst, inst->threadNumber);

            break;
        }

        // Go ahead and compute any PC-relative branches.
        if (inst->isDirectCtrl() && inst->isUncondCtrl()) {
            ++decodeBranchResolved;

            if (!(inst->branchTarget() == inst->readPredTarg())) {
                ++decodeBranchMispred;

                // Might want to set some sort of boolean and just do
                // a check at the end
                squash(inst, inst->threadNumber);
                TheISA::PCState target = inst->branchTarget();

                DPRINTF(Decode, "[sn:%i]: Updating predictions: PredPC: %s\n",
                        inst->seqNum, target);
                //The micro pc after an instruction level branch should be 0
                inst->setPredTarg(target);
                break;
            }
        }
    }


    if (HPT == tid) {
        for (int i = LDST::load; i < LDST::NumType; i++) {
            ldstNum[i] += ldstSample[i][ldstIndex[i]];
            ldstIndex[i] = (ldstIndex[i] + 1) % sampleLen;
            ldstRate[i] = ((float) ldstNum[i]) / ((float) sampleLen);
        }
    }

    // If we didn't process all instructions, then we will need to block
    // and put all those instructions into the skid buffer.
    if (!squashedThisCycle[tid]) {
        if (!insts_to_decode.empty()) {
            block(tid);
        }
        if (insts_to_decode.empty() && old_status == Unblocking) {
            DPRINTF(DecodeBreakdown, "Pop empty row from skidbuffer of T[%i]\n", tid);
            skidBuffer[tid].pop();
            skidInstTick[tid].pop();
            skidSlotBuffer[tid].pop();
            skidSlotTick[tid].pop();
        }
    }

    // Record that decode has written to the time buffer for activity
    // tracking.
    if (toRenameIndex) {
        wroteToTimeBuffer = true;
    }
}

template <class Impl>
void
DefaultDecode<Impl>::passLB(ThreadID tid)
{
    toFetch->decodeInfo[tid].BLB = fromRename->renameInfo[tid].BLB;

    if (toRenameNum[tid] > 0) {
        if (!squashedThisCycle[tid]) {
            if (toRenameNum[tid] < decodeWidth) {
                this->incLocalSlots(tid, Base, toRenameNum[tid]);

                int cursor = 0, i = 0;
                while (curCycleRow[tid][cursor] == SlotsUse::Referenced) {
                    cursor++;
                }

                if (decodeStatus[tid] == Blocked) {
                    //一定是因为另一个线程占用了decodeWidth
                    for (; i < toRenameNum[tid]; i++) {
                        assert(skidSlotBuffer[tid].front()[cursor+i] == Base);
                        skidSlotBuffer[tid].front()[cursor+i] = SlotsUse::Referenced;
                    }
                    this->incLocalSlots(tid, SlotsUse::WidthWait,
                            decodeWidth - toRenameNum[tid]);
                } else {
                    for (; i < toRenameNum[tid]; i++);
                    if (cursor > 0) {
                        this->incLocalSlots(tid, SlotsUse::SplitWait, cursor);
                    }
                    for (; cursor + i < decodeWidth; i++) {
                        if (curCycleRow[tid][cursor+i] == SlotsUse::Base) {
                            assert(numSquashedThisCycle[tid]-- > 0);
                            this->incLocalSlots(tid, SlotsUse::SquashMiss, 1);
                        } else {
                            this->incLocalSlots(tid, curCycleRow[tid][cursor+i], 1);
                        }
                    }
                }
            } else {
                this->incLocalSlots(tid, SlotsUse::Base, decodeWidth);
            }

            assert(toRenameNum[tid] == this->perCycleSlots[tid][Base]);
        } else {
            this->incLocalSlots(tid, SlotsUse::Base, toRenameNum[tid]);
            this->incLocalSlots(tid, SlotsUse::SquashMiss,
                    decodeWidth - toRenameNum[tid]);
        }
    } else {
        if (decodeStatus[tid] == Blocked) {
            if (stalls[tid].rename) {
                if (fromRename->renameInfo[tid].BLB) {
                    this->incLocalSlots(tid, SlotsUse::LaterWait, decodeWidth);
                } else {
                    this->incLocalSlots(tid, SlotsUse::LaterMiss, decodeWidth);
                }
            } else {
                if (toRenameNum[this->another(tid)] > 0) {
                    this->incLocalSlots(tid, SlotsUse::WidthWait, decodeWidth);
                } else {
                    assert(0 && "Unknow condition\n");
                }
            }

        } else {
            assert(decodeStatus[tid] != Unblocking);
            // 如果是unblocking，那么skidBuffer中一定有指令
            bool intrinsic_miss = decodeStatus[tid] == Squashing;

            if (intrinsic_miss) {
                this->incLocalSlots(tid, SlotsUse::SquashMiss, decodeWidth);
            } else {
                assert(decodeStatus[tid] == Running || decodeStatus[tid] == Idle);

                for (int i = 0; i < decodeWidth; i++) {
                    if (fromFetch->slotPass[i] == SlotsUse::Base) {
                        assert(numSquashedThisCycle[tid]-- > 0);
                        this->incLocalSlots(tid, SlotsUse::SquashMiss, 1);
                    } else {
                        this->incLocalSlots(tid, fromFetch->slotPass[i], 1);
                    }
                }
            }
        }
    }
}

template <class Impl>
void
DefaultDecode<Impl>::noLoadStoreThisCycle() {
    for (int i = LDST::load; i < LDST::NumType; i++) {
        ldstNum[i] -= ldstSample[i][ldstIndex[i]];
        ldstSample[i][ldstIndex[i]] = 0;
        ldstIndex[i] = (ldstIndex[i] + 1) % sampleLen;
        ldstRate[i] = ((float) ldstNum[i]) / ((float) sampleLen);
    }
}

#endif//__CPU_O3_DECODE_IMPL_HH__
