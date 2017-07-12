/*
 * Copyright (c) 2011-2012, 2014 ARM Limited
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
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
 * Copyright (c) 2005-2006 The Regents of The University of Michigan
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
 * Authors: Korey Sewell
 */

#ifndef __CPU_O3_LSQ_IMPL_HH__
#define __CPU_O3_LSQ_IMPL_HH__

#include <algorithm>
#include <list>
#include <string>
#include <numeric>

#include "cpu/o3/lsq.hh"
#include "debug/Drain.hh"
#include "debug/Fetch.hh"
#include "debug/LSQ.hh"
#include "debug/Writeback.hh"
#include "debug/Pard.hh"
#include "debug/FmtCtrl.hh"
#include "params/DerivO3CPU.hh"

using namespace std;

template <typename Container, typename BinaryOp, typename Ret>
Ret Accumulate(Container &c, Ret init, BinaryOp op)
{
    return std::accumulate(c.begin(), c.end(), init, op);
}

template <class Impl>
LSQ<Impl>::LSQ(O3CPU *cpu_ptr, IEW *iew_ptr, DerivO3CPUParams *params)
    :denominator(1024),
      cpu(cpu_ptr), iewStage(iew_ptr),
      LQEntries(params->LQEntries),
      SQEntries(params->SQEntries),
      numThreads(params->numThreads),
      numUsedLQEntries(0),
      numUsedSQEntries(0),
      lqUtil(0),
      sqUtil(0),
      lqUptodate(false),
      sqUptodate(false),
      sampleCycle(0),
      sampleTime(0),
      sampleRate(params->dumpWindowSize),
      hptInitLQPriv(unsigned(float(LQEntries) *params->hptLQPrivProp)),
      hptInitSQPriv(unsigned(float(SQEntries) *params->hptSQPrivProp))
{
    LQPortion[HPT] = hptInitLQPriv * denominator / LQEntries;
    SQPortion[HPT] = hptInitSQPriv * denominator / SQEntries;

    unsigned lq_portion_other_thread = (denominator - LQPortion[HPT]) / (numThreads - 1);
    unsigned sq_portion_other_thread = (denominator - SQPortion[HPT]) / (numThreads - 1);

    for (ThreadID tid = 1; tid < numThreads; tid++) {
        LQPortion[tid] = lq_portion_other_thread;
        SQPortion[tid] = sq_portion_other_thread;
    }
}

template<class Impl>
void
LSQ<Impl>::init(DerivO3CPUParams *params)
{
    assert(numThreads > 0 && numThreads <= Impl::MaxThreads);

    //**********************************************/
    //************ Handle SMT Parameters ***********/
    //**********************************************/
    std::string policy = params->smtLSQPolicy;

    //Convert string to lowercase
    std::transform(policy.begin(), policy.end(), policy.begin(),
                   (int(*)(int)) tolower);

    //Figure out fetch policy
    if (policy == "dynamic") {
        lsqPolicy = Dynamic;

        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            maxLQEntries[tid] = LQEntries;
            maxSQEntries[tid] = SQEntries;
        }

        DPRINTF(LSQ, "LSQ sharing policy set to Dynamic\n");
    } else if (policy == "partitioned") {
        lsqPolicy = Partitioned;

        //@todo:make work if part_amt doesnt divide evenly.
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            maxLQEntries[tid] = LQEntries / numThreads;
            maxSQEntries[tid] = SQEntries / numThreads;
        }

        DPRINTF(Fetch, "LSQ sharing policy set to Partitioned: "
                "%i entries per LQ | %i entries per SQ\n",
                maxLQEntries[0], maxSQEntries[0]);
    } else if (policy == "threshold") {
        lsqPolicy = Threshold;

        // The following lien may be wrong ?
        // SB Assertion
        // assert(params->smtLSQThreshold > LQEntries);
        // assert(params->smtLSQThreshold > SQEntries);

        //Divide up by threshold amount
        //@todo: Should threads check the max and the total
        //amount of the LSQ
        //NO
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            maxLQEntries[tid] = params->smtLSQThreshold;
            maxSQEntries[tid] = params->smtLSQThreshold;
        }

        DPRINTF(LSQ, "LSQ sharing policy set to Threshold: "
                "%i entries per LQ | %i entries per SQ\n",
                maxLQEntries[0] ,maxSQEntries[0]);
    } else if (policy == "programmable"){
        /** get max LQ and SQ entries from params. */

        lsqPolicy = Programmable;

        DPRINTF(LSQ, "LSQ sharing policy set to Programmable\n");
        DPRINTF(Pard, "LSQ sharing policy set to Programmable\n");

        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            maxLQEntries[tid] = LQEntries*LQPortion[tid]/denominator;
            maxSQEntries[tid] = SQEntries*SQPortion[tid]/denominator;
            DPRINTF(Pard, "LQEntries[%d]: %d\n", tid, LQEntries);
            DPRINTF(Pard, "LQPortion[%d]: %d\n", tid, LQPortion[tid]);
        }
    } else {
        assert(0 && "Invalid LSQ Sharing Policy.Options Are:{Dynamic,"
                    "Partitioned, Threshold}");
    }

    //Initialize LSQs
    thread = new LSQUnit[numThreads];
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        /** Because LSQUnit is not dynamic,
          * in order to implement dynamic partition,
          * all of threads own LQEntries LQ and SQEntries SQ.
          */
        if (lsqPolicy == Threshold) {
            thread[tid].init(cpu, iewStage, params, this,
                    maxLQEntries[tid], maxSQEntries[tid], tid, false);
        } else if (lsqPolicy == Dynamic || lsqPolicy == Programmable){
            thread[tid].init(cpu, iewStage , params, this,
                    LQEntries, SQEntries, tid, true);
        } else {
            thread[tid].init(cpu, iewStage, params, this,
                    maxLQEntries[tid], maxSQEntries[tid], tid, false);
        }
        thread[tid].setDcachePort(&cpu->getDataPort());
    }
    numThreadUsedLQEntries[0] = 0;
    numThreadUsedLQEntries[1] = 0;
    numThreadUsedSQEntries[0] = 0;
    numThreadUsedSQEntries[1] = 0;
    lqThreadUtil[0] = 0;
    lqThreadUtil[1] = 0;
    sqThreadUtil[0] = 0;
    sqThreadUtil[1] = 0;
}

template<class Impl>
std::string
LSQ<Impl>::name() const
{
    return iewStage->name() + ".lsq";
}

template<class Impl>
void
LSQ<Impl>::regStats()
{
    //Initialize LSQs
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        thread[tid].regStats();
    }

    lqUtilization
        .init(numThreads)
        .name(name() + ".lq_utilization")
        .desc("Accumulation of load queue used every cycle")
        .flags(Stats::display | Stats::total);

    sqUtilization
        .init(numThreads)
        .name(name() + ".sq_utilization")
        .desc("Accumulation of store queue used every cycle")
        .flags(Stats::display | Stats::total);
}

template<class Impl>
void
LSQ<Impl>::setActiveThreads(list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
    assert(activeThreads != 0);
}

template <class Impl>
void
LSQ<Impl>::drainSanityCheck() const
{
    assert(isDrained());

    for (ThreadID tid = 0; tid < numThreads; tid++)
        thread[tid].drainSanityCheck();
}

template <class Impl>
bool
LSQ<Impl>::isDrained() const
{
    bool drained(true);

    if (!lqEmpty()) {
        DPRINTF(Drain, "Not drained, LQ not empty.\n");
        drained = false;
    }

    if (!sqEmpty()) {
        DPRINTF(Drain, "Not drained, SQ not empty.\n");
        drained = false;
    }

    return drained;
}

template <class Impl>
void
LSQ<Impl>::takeOverFrom()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        thread[tid].takeOverFrom();
    }
}

template <class Impl>
int
LSQ<Impl>::entryAmount(ThreadID num_threads)
{
    if (lsqPolicy == Partitioned) {
        return LQEntries / num_threads;
    } else if (lsqPolicy == Programmable) {
        return maxLQEntries[num_threads];
    } else {
        return 0;
    }
}

template <class Impl>
void
LSQ<Impl>::resetEntries()
{
    DPRINTF(Pard, "resizing LS Unit\n");
    if (lsqPolicy != Dynamic || numThreads > 1) {
        int active_threads = activeThreads->size();

        int maxEntries;

        if (lsqPolicy == Partitioned) {
            maxEntries = LQEntries / active_threads;
        } else if (lsqPolicy == Threshold && active_threads == 1) {
            maxEntries = maxLQEntries[0];
            // for testing
        } else {
            maxEntries = LQEntries;
        }

        list<ThreadID>::iterator threads  = activeThreads->begin();
        list<ThreadID>::iterator end = activeThreads->end();

        while (threads != end) {
            ThreadID tid = *threads++;

            resizeEntries(maxEntries, tid);
        }
    }
}

template<class Impl>
void
LSQ<Impl>::removeEntries(ThreadID tid)
{
    thread[tid].clearLQ();
    thread[tid].clearSQ();
}

template<class Impl>
void
LSQ<Impl>::resizeEntries(unsigned size, ThreadID tid)
{
    thread[tid].resizeLQ(size);
    thread[tid].resizeSQ(size);
}

template<class Impl>
void
LSQ<Impl>::tick()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        thread[tid].tick();
    }
}

template<class Impl>
void
LSQ<Impl>::insertLoad(DynInstPtr &load_inst)
{
    ThreadID tid = load_inst->threadNumber;

    thread[tid].insertLoad(load_inst);
}

template<class Impl>
void
LSQ<Impl>::insertStore(DynInstPtr &store_inst)
{
    ThreadID tid = store_inst->threadNumber;

    thread[tid].insertStore(store_inst);
}

template<class Impl>
Fault
LSQ<Impl>::executeLoad(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;

    return thread[tid].executeLoad(inst);
}

template<class Impl>
Fault
LSQ<Impl>::executeStore(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;

    return thread[tid].executeStore(inst);
}

template<class Impl>
void
LSQ<Impl>::writebackStores()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (numStoresToWB(tid) > 0) {
            DPRINTF(Writeback,"[tid:%i] Writing back stores. %i stores "
                "available for Writeback.\n", tid, numStoresToWB(tid));
        }

        thread[tid].writebackStores();
    }
}

template<class Impl>
bool
LSQ<Impl>::violation()
{
    /* Answers: Does Anybody Have a Violation?*/
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (thread[tid].violation())
            return true;
    }

    return false;
}

template <class Impl>
void
LSQ<Impl>::recvReqRetry()
{
    iewStage->cacheUnblocked();

    for (ThreadID tid : *activeThreads) {
        thread[tid].recvRetry();
    }
}

template <class Impl>
bool
LSQ<Impl>::recvTimingResp(PacketPtr pkt)
{
    if (pkt->isError())
        DPRINTF(LSQ, "Got error packet back for address: %#X\n",
                pkt->getAddr());

    thread[pkt->req->threadId()].completeDataAccess(pkt);

    if (pkt->isInvalidate()) {
        // This response also contains an invalidate; e.g. this can be the case
        // if cmd is ReadRespWithInvalidate.
        //
        // The calling order between completeDataAccess and checkSnoop matters.
        // By calling checkSnoop after completeDataAccess, we ensure that the
        // fault set by checkSnoop is not lost. Calling writeback (more
        // specifically inst->completeAcc) in completeDataAccess overwrites
        // fault, and in case this instruction requires squashing (as
        // determined by checkSnoop), the ReExec fault set by checkSnoop would
        // be lost otherwise.

        DPRINTF(LSQ, "received invalidation with response for addr:%#x\n",
                pkt->getAddr());

        for (ThreadID tid = 0; tid < numThreads; tid++) {
            thread[tid].checkSnoop(pkt);
        }
    }

    delete pkt->req;
    delete pkt;
    return true;
}

template <class Impl>
void
LSQ<Impl>::recvTimingSnoopReq(PacketPtr pkt)
{
    DPRINTF(LSQ, "received pkt for addr:%#x %s\n", pkt->getAddr(),
            pkt->cmdString());

    // must be a snoop
    if (pkt->isInvalidate()) {
        DPRINTF(LSQ, "received invalidation for addr:%#x\n",
                pkt->getAddr());
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            thread[tid].checkSnoop(pkt);
        }
    }
}

template<class Impl>
int
LSQ<Impl>::getCount()
{
    return Accumulate(*activeThreads, 0,
            [this](int sum, ThreadID tid){ return sum + getCount(tid); });
}

template<class Impl>
int
LSQ<Impl>::numLoads()
{
    return Accumulate(*activeThreads, 0,
            [this](int sum, ThreadID tid){ return sum + numLoads(tid); });
}

template<class Impl>
int
LSQ<Impl>::numStores()
{
    return Accumulate(*activeThreads, 0,
            [this](int sum, ThreadID tid){ return sum + numStores(tid); });
}

template<class Impl>
unsigned
LSQ<Impl>::numFreeLoadEntries()
{
    return LQEntries - numLoads();
}

template<class Impl>
unsigned
LSQ<Impl>::numFreeStoreEntries()
{
    return SQEntries - numStores();
}

template<class Impl>
unsigned
LSQ<Impl>::numFreeLoadEntries(ThreadID tid)
{
    if (lsqPolicy == Dynamic) {
        return numFreeLoadEntries();
    } else {
        return thread[tid].numFreeLoadEntries();
    }
}

template<class Impl>
unsigned
LSQ<Impl>::numFreeStoreEntries(ThreadID tid)
{
    if (lsqPolicy == Dynamic) {
        return numFreeStoreEntries();
    } else {
        return thread[tid].numFreeStoreEntries();
    }
}

template<class Impl>
bool
LSQ<Impl>::isFull()
{
    return lqFull() || sqFull();
}

template<class Impl>
bool
LSQ<Impl>::isFull(ThreadID tid)
{
    if (lsqPolicy == Dynamic) {
        return isFull();
    } else {
        return thread[tid].lqFull() || thread[tid].sqFull();
    }
}

template<class Impl>
bool
LSQ<Impl>::isEmpty() const
{
    return lqEmpty() && sqEmpty();
}

template<class Impl>
bool
LSQ<Impl>::lqEmpty() const
{
    list<ThreadID>::const_iterator threads = activeThreads->begin();
    list<ThreadID>::const_iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!thread[tid].lqEmpty())
            return false;
    }

    return true;
}

template<class Impl>
bool
LSQ<Impl>::sqEmpty() const
{
    list<ThreadID>::const_iterator threads = activeThreads->begin();
    list<ThreadID>::const_iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!thread[tid].sqEmpty())
            return false;
    }

    return true;
}

template<class Impl>
bool
LSQ<Impl>::lqFull()
{
    return numLoads() == LQEntries;
}

template<class Impl>
bool
LSQ<Impl>::lqFull(ThreadID tid)
{
    if (lsqPolicy == Dynamic) {
        return lqFull();
    } else {
        return thread[tid].lqFull() || lqFull();
    }
}

template<class Impl>
bool
LSQ<Impl>::sqFull()
{
    return numStores() == SQEntries;
}

template<class Impl>
bool
LSQ<Impl>::sqFull(ThreadID tid)
{
    if (lsqPolicy == Dynamic) {
        return sqFull();
    } else {
        return thread[tid].sqFull() || sqFull();
    }
}

template<class Impl>
bool
LSQ<Impl>::isStalled()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!thread[tid].isStalled())
            return false;
    }

    return true;
}

template<class Impl>
bool
LSQ<Impl>::isStalled(ThreadID tid)
{
    if (lsqPolicy == Dynamic)
        return isStalled();
    else
        return thread[tid].isStalled();
}

template<class Impl>
bool
LSQ<Impl>::hasStoresToWB()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (hasStoresToWB(tid))
            return true;
    }

    return false;
}

template<class Impl>
bool
LSQ<Impl>::willWB()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (willWB(tid))
            return true;
    }

    return false;
}

template<class Impl>
void
LSQ<Impl>::dumpInsts() const
{
    list<ThreadID>::const_iterator threads = activeThreads->begin();
    list<ThreadID>::const_iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        thread[tid].dumpInsts();
    }
}

template<class Impl>
void
LSQ<Impl>::updateMaxEntries()
{
    if (numThreads < 2 || lsqPolicy != Programmable ||
            (lqUptodate && sqUptodate)) {
        return;
    }

    DPRINTF(FmtCtrl, "LQ [0]: %d, [1]: %d\n", maxLQEntries[0], maxLQEntries[1]);
    DPRINTF(FmtCtrl, "SQ [0]: %d, [1]: %d\n", maxSQEntries[0], maxSQEntries[1]);

    DPRINTF(Pard, "Updating LSQ maxEntries\n");

    bool increaseThread0LQ = false;
    bool increaseThread0SQ = false;

    unsigned LQLimit[] = {0, 0};
    unsigned SQLimit[] = {0, 0};

    int LQFree[] = {0, 0};
    int SQFree[] = {0, 0};

    LQLimit[0] = LQEntries * LQPortion[0] / denominator;
    SQLimit[0] = SQEntries * SQPortion[0] / denominator;

    LQLimit[1] = LQEntries - LQLimit[0];
    SQLimit[1] = SQEntries - SQLimit[0];

    DPRINTF(LSQ, "LQPortion[0]: %i, LQPortion[1]: %i\n",
            LQPortion[0], LQPortion[1]);
    DPRINTF(LSQ, "LQLimit[0]: %i, LQlimit[1]: %i\n",
            LQLimit[0], LQLimit[1]);

    if (LQLimit[0] > maxLQEntries[0])
        increaseThread0LQ = true;
    if (SQLimit[0] > maxSQEntries[0])
        increaseThread0SQ = true;

    if (!lqUptodate) {
        if (increaseThread0LQ) {
            LQFree[1] = thread[1].setLQLimit(LQLimit[1]);
            // LQFree > 0 means allocation can be done in one time
            if (LQFree[1] >= 0) {
                thread[0].setLQLimit(LQLimit[0]);
                maxLQEntries[0] = LQLimit[0];
                maxLQEntries[1] = LQLimit[1];
                lqUptodate = true;
            }
            else {
                thread[0].setLQLimit(LQLimit[0] + LQFree[1]);
                maxLQEntries[0] = LQLimit[0] + LQFree[1];
                maxLQEntries[1] = LQLimit[1];
                lqUptodate = false;
            }
        }
        else {
            LQFree[0] = thread[0].setLQLimit(LQLimit[0]);
            // LQFree > 0 means allocation can be done in one time
            if (LQFree[0] >= 0) {
                thread[1].setLQLimit(LQLimit[1]);
                maxLQEntries[0] = LQLimit[0];
                maxLQEntries[1] = LQLimit[1];
                lqUptodate = true;
            }
            else {
                thread[1].setLQLimit(LQLimit[1] + LQFree[0]);
                maxLQEntries[0] = LQLimit[0];
                maxLQEntries[1] = LQLimit[1] + LQFree[0];
                lqUptodate = false;
            }
        }
    }

    if (!sqUptodate) {
        if (increaseThread0SQ) {
            SQFree[1] = thread[1].setSQLimit(SQLimit[1]);
            // SQFree > 0 means allocation can be done in one time
            if (SQFree[1] >= 0) {
                thread[0].setSQLimit(SQLimit[0]);
                maxSQEntries[0] = SQLimit[0];
                maxSQEntries[1] = SQLimit[1];
                sqUptodate = true;
            }
            else {
                thread[0].setSQLimit(SQLimit[0] + SQFree[1]);
                maxSQEntries[0] = SQLimit[0] + SQFree[1];
                maxSQEntries[1] = SQLimit[1];
                sqUptodate = false;
            }
        }
        else {
            SQFree[0] = thread[0].setSQLimit(SQLimit[0]);
            // SQFree > 0 means allocation can be done in one time
            if (SQFree[0] >= 0) {
                thread[1].setSQLimit(SQLimit[1]);
                maxSQEntries[0] = SQLimit[0];
                maxSQEntries[1] = SQLimit[1];
                sqUptodate = true;
            }
            else {
                thread[1].setSQLimit(SQLimit[1] + SQFree[0]);
                maxSQEntries[0] = SQLimit[0];
                maxSQEntries[1] = SQLimit[1] + SQFree[0];
                sqUptodate = false;
            }
        }
    }


    for (ThreadID tid = 0; tid < numThreads - 1; ++tid) {
        DPRINTF(Pard, "Thread %d LQ Entries: %d, SQ Entries: %d\n",
                tid, maxLQEntries[tid], maxSQEntries[tid]);
    }
}

template<class Impl>
void
LSQ<Impl>::reassignLQPortion(int newPortionVec[],
        int lenNewPortionVec, int newPortionDenominator)
{
    //assert(lenNewPortionVec == numThreads);
    if (lsqPolicy != Programmable) {
        return;
    }

    lqUptodate = false;

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        LQPortion[tid] = newPortionVec[tid];
    }

    denominator = newPortionDenominator;
}

template<class Impl>
void
LSQ<Impl>::reassignSQPortion(int newPortionVec[],
        int lenNewPortionVec, int newPortionDenominator)
{
    //assert(lenNewPortionVec == numThreads);
    if (lsqPolicy != Programmable) {
        return;
    }

    sqUptodate = false;

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        SQPortion[tid] = newPortionVec[tid];
    }

    denominator = newPortionDenominator;
}

template<class Impl>
void
LSQ<Impl>::increaseUsedEntries()
{
    sampleCycle++;
    if (sampleTime*(cpu->dumpWindowSize/sampleRate) <= sampleCycle) {
        sampleTime++;
        numUsedLQEntries += numLoads();
        numThreadUsedLQEntries[0] += numLoads(0);
        numThreadUsedLQEntries[1] += numLoads(1);
        numUsedSQEntries += numStores();
        numThreadUsedSQEntries[0] += numStores(0);
        numThreadUsedSQEntries[1] += numStores(1);
    }
}

template<class Impl>
void
LSQ<Impl>::resetUsedEntries()
{
    numUsedLQEntries = 0;
    numUsedSQEntries = 0;
    numThreadUsedLQEntries[0] = 0;
    numThreadUsedLQEntries[1] = 0;
    numThreadUsedSQEntries[0] = 0;
    numThreadUsedSQEntries[1] = 0;
    sampleCycle = 0;
    sampleTime = 0;
}

template<class Impl>
void
LSQ<Impl>::dumpUsedEntries()
{
    lqUtil = double(numUsedLQEntries) /
        double(LQEntries * sampleRate);
    sqUtil = double(numUsedSQEntries) /
        double(SQEntries * sampleRate);

    lqThreadUtil[0] = double(numThreadUsedLQEntries[0]) /
        double(LQEntries * sampleRate);
    sqThreadUtil[0] = double(numThreadUsedSQEntries[0]) /
        double(SQEntries * sampleRate);

    lqThreadUtil[1] = double(numThreadUsedLQEntries[1]) /
        double(LQEntries * sampleRate);
    sqThreadUtil[1] = double(numThreadUsedSQEntries[1]) /
        double(SQEntries * sampleRate);

    lqUtilization[0] = lqThreadUtil[0];
    lqUtilization[1] = lqThreadUtil[1];

    sqUtilization[0] = sqThreadUtil[0];
    sqUtilization[1] = sqThreadUtil[1];

    resetUsedEntries();
}

#endif//__CPU_O3_LSQ_IMPL_HH__
