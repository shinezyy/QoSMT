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
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * Copyright (c) 2011 Regents of the University of California
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
 *          Rick Strong
 */

#include "arch/kernel_stats.hh"
#include "config/the_isa.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/checker/thread_context.hh"
#include "cpu/o3/cpu.hh"
#include "cpu/o3/isa_specific.hh"
#include "cpu/o3/thread_context.hh"
#include "cpu/activity.hh"
#include "cpu/quiesce_event.hh"
#include "cpu/simple_thread.hh"
#include "cpu/thread_context.hh"
#include "debug/Activity.hh"
#include "debug/Drain.hh"
#include "debug/O3CPU.hh"
#include "debug/Quiesce.hh"
#include "debug/Pard.hh"
#include "debug/QoSCtrl.hh"
#include "debug/FMT.hh"
#include "debug/ILPPred.hh"
#include "debug/Cazorla.hh"
#include "debug/ResourceAllocation.hh"
#include "enums/MemoryMode.hh"
#include "sim/core.hh"
#include "sim/full_system.hh"
#include "sim/process.hh"
#include "sim/stat_control.hh"
#include "sim/system.hh"
#include "sim/async.hh"
#include "sim/eventq.hh"
#include "mem/cache/tags/control_panel.hh"

#if THE_ISA == ALPHA_ISA
#include "arch/alpha/osfpal.hh"
#include "debug/Activity.hh"
#endif

#include <fstream>
#include <numeric>

struct BaseCPUParams;

using namespace TheISA;
using namespace std;

BaseO3CPU::BaseO3CPU(BaseCPUParams *params)
    : BaseCPU(params)
{
}

void
BaseO3CPU::regStats()
{
    BaseCPU::regStats();
}

template<class Impl>
bool
FullO3CPU<Impl>::IcachePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(O3CPU, "Fetch unit received timing\n");
    // We shouldn't ever get a cacheable block in ownership state
    assert(pkt->req->isUncacheable() ||
           !(pkt->memInhibitAsserted() && !pkt->sharedAsserted()));
    fetch->processCacheCompletion(pkt);

    return true;
}

template<class Impl>
void
FullO3CPU<Impl>::IcachePort::recvReqRetry()
{
    fetch->recvReqRetry();
}

template <class Impl>
bool
FullO3CPU<Impl>::DcachePort::recvTimingResp(PacketPtr pkt)
{
    return lsq->recvTimingResp(pkt);
}

template <class Impl>
void
FullO3CPU<Impl>::DcachePort::recvTimingSnoopReq(PacketPtr pkt)
{
    // X86 ISA: Snooping an invalidation for monitor/mwait
    if(cpu->getCpuAddrMonitor()->doMonitor(pkt)) {
        cpu->wakeup();
    }
    lsq->recvTimingSnoopReq(pkt);
}

template <class Impl>
void
FullO3CPU<Impl>::DcachePort::recvReqRetry()
{
    lsq->recvReqRetry();
}

template <class Impl>
FullO3CPU<Impl>::TickEvent::TickEvent(FullO3CPU<Impl> *c)
    : Event(CPU_Tick_Pri), cpu(c)
{
}

template <class Impl>
void
FullO3CPU<Impl>::TickEvent::process()
{
    cpu->tick();
}

template <class Impl>
const char *
FullO3CPU<Impl>::TickEvent::description() const
{
    return "FullO3CPU tick";
}

template <class Impl>
FullO3CPU<Impl>::FullO3CPU(DerivO3CPUParams *params)
    : BaseO3CPU(params),
      itb(params->itb),
      dtb(params->dtb),
      dumpCycles(0),
      policyCycles(0),
      tickEvent(this),
#ifndef NDEBUG
      instcount(0),
#endif
      removeInstsThisCycle(false),
      fetch(this, params),
      decode(this, params),
      rename(this, params),
      iew(this, params),
      commit(this, params),

      fmt(this, params),
      bmt(this, params),

      regFile(params->numPhysIntRegs,
              params->numPhysFloatRegs,
              params->numPhysCCRegs),

      freeList(name() + ".freelist", &regFile),

      rob(this, params),

      scoreboard(name() + ".scoreboard",
                 regFile.totalNumPhysRegs(), TheISA::NumMiscRegs,
                 TheISA::ZeroReg, TheISA::ZeroReg),

      isa(numThreads, NULL),

      icachePort(&fetch, this),
      dcachePort(&iew.ldstQueue, this),

      timeBuffer(params->backComSize, params->forwardComSize),
      fetchQueue(params->backComSize, params->forwardComSize),
      decodeQueue(params->backComSize, params->forwardComSize),
      renameQueue(params->backComSize, params->forwardComSize),
      iewQueue(params->backComSize, params->forwardComSize),
      activityRec(name(), NumStages,
                  params->backComSize + params->forwardComSize,
                  params->activity),

      globalSeqNum(1),
      system(params->system),
      drainManager(NULL),
      lastRunningCycle(curCycle()),
      expectedQoS((uint32_t) params->expectedQoS),
      robReserved(false),
      lqReserved(false),
      sqReserved(false),
      fetchReserved(false),
      dumpWindowSize((unsigned int) params->dumpWindowSize),
      policyWindowSize((unsigned int) params->policyWindowSize),
      numPhysIntRegs(params->numPhysIntRegs),
      numPhysFloatRegs(params->numPhysFloatRegs),
      localCycles(0),
      abnormal(false),
      numContCtrl(0),
      numResourceToReserve(params->numResourceToReserve),
      numResourceToRelease(params->numResourceToRelease),
      dynCache(params->dynCache),
      cazorlaPhase(CazorlaPhase::NotStarted),
      subTuningPhaseNumber(0),
      numPreSampleCycles(50000),
      numSampleCycles(10000),
      numSubPhaseCycles(15000),
      phaseLength(0),
      curPhaseCycles(0),
      sampledIPC(0.0),
      targetIPC(0.0),
      localIPC(0.0),
      localTargetIPC(0.0),
      compensationTerm(0),
      grainFactor(params->grainFactor),
      grain(1024 / grainFactor),
      HPTMaxQuota(params->HPTMaxQuota),
      HPTMinQuota(params->HPTMinQuota)
{
    if (!params->switched_out) {
        _status = Running;
    } else {
        _status = SwitchedOut;
    }

    if (params->checker) {
        BaseCPU *temp_checker = params->checker;
        checker = dynamic_cast<Checker<Impl> *>(temp_checker);
        checker->setIcachePort(&icachePort);
        checker->setSystem(params->system);
    } else {
        checker = NULL;
    }

    if (!FullSystem) {
        thread.resize(numThreads);
        tids.resize(numThreads);
    }

    // The stages also need their CPU pointer setup.  However this
    // must be done at the upper level CPU because they have pointers
    // to the upper level CPU, and not this FullO3CPU.

    // Set up Pointers to the activeThreads list for each stage
    fetch.setActiveThreads(&activeThreads);
    decode.setActiveThreads(&activeThreads);
    rename.setActiveThreads(&activeThreads);
    iew.setActiveThreads(&activeThreads);
    commit.setActiveThreads(&activeThreads);

    // Give each of the stages the time buffer they will use.
    fetch.setTimeBuffer(&timeBuffer);
    decode.setTimeBuffer(&timeBuffer);
    rename.setTimeBuffer(&timeBuffer);
    iew.setTimeBuffer(&timeBuffer);
    commit.setTimeBuffer(&timeBuffer);

    // Also setup each of the stages' queues.
    fetch.setFetchQueue(&fetchQueue);
    decode.setFetchQueue(&fetchQueue);
    commit.setFetchQueue(&fetchQueue);
    decode.setDecodeQueue(&decodeQueue);
    rename.setDecodeQueue(&decodeQueue);
    rename.setRenameQueue(&renameQueue);
    iew.setRenameQueue(&renameQueue);
    iew.setIEWQueue(&iewQueue);
    commit.setIEWQueue(&iewQueue);
    commit.setRenameQueue(&renameQueue);

    fetch.setIEWStage(&iew);
    commit.setIEWStage(&iew);
    rename.setIEWStage(&iew);
    rename.setCommitStage(&commit);

    fmt.setStage(&fetch, &decode, &iew);
    fetch.setFmt(&fmt);
    iew.setFmt(&fmt);
    commit.setFmt(&fmt);

    rename.setBmt(&bmt);
    iew.setBmt(&bmt);
    commit.setBmt(&bmt);

    ThreadID active_threads;
    if (FullSystem) {
        active_threads = 1;
    } else {
        active_threads = params->workload.size();

        if (active_threads > Impl::MaxThreads) {
            panic("Workload Size too large. Increase the 'MaxThreads' "
                  "constant in your O3CPU impl. file (e.g. o3/alpha/impl.hh) "
                  "or edit your workload size.");
        }
    }

    //Make Sure That this a Valid Architeture
    assert(params->numPhysIntRegs   >= numThreads * TheISA::NumIntRegs);
    assert(params->numPhysFloatRegs >= numThreads * TheISA::NumFloatRegs);
    assert(params->numPhysCCRegs >= numThreads * TheISA::NumCCRegs);

    rename.setScoreboard(&scoreboard);
    iew.setScoreboard(&scoreboard);

    // Setup the rename map for whichever stages need it.
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        isa[tid] = params->isa[tid];

        // Only Alpha has an FP zero register, so for other ISAs we
        // use an invalid FP register index to avoid special treatment
        // of any valid FP reg.
        RegIndex invalidFPReg = TheISA::NumFloatRegs + 1;
        RegIndex fpZeroReg =
            (THE_ISA == ALPHA_ISA) ? TheISA::ZeroReg : invalidFPReg;

        commitRenameMap[tid].init(&regFile, TheISA::ZeroReg, fpZeroReg,
                                  &freeList);

        renameMap[tid].init(&regFile, TheISA::ZeroReg, fpZeroReg,
                            &freeList);
    }

    // Initialize rename map to assign physical registers to the
    // architectural registers for active threads only.
    for (ThreadID tid = 0; tid < active_threads; tid++) {
        for (RegIndex ridx = 0; ridx < TheISA::NumIntRegs; ++ridx) {
            // Note that we can't use the rename() method because we don't
            // want special treatment for the zero register at this point
            PhysRegIndex phys_reg = freeList.getIntReg();
            // FIXME Ugly exposure. Due to the assignment, the free reg
            // counter per thread has to be substracted to keep the
            // consistency.
            rename.nrFreeRegs[tid]--;

            renameMap[tid].setIntEntry(ridx, phys_reg);
            commitRenameMap[tid].setIntEntry(ridx, phys_reg);
        }

        for (RegIndex ridx = 0; ridx < TheISA::NumFloatRegs; ++ridx) {
            PhysRegIndex phys_reg = freeList.getFloatReg();
            renameMap[tid].setFloatEntry(ridx, phys_reg);
            commitRenameMap[tid].setFloatEntry(ridx, phys_reg);
        }

        for (RegIndex ridx = 0; ridx < TheISA::NumCCRegs; ++ridx) {
            PhysRegIndex phys_reg = freeList.getCCReg();
            renameMap[tid].setCCEntry(ridx, phys_reg);
            commitRenameMap[tid].setCCEntry(ridx, phys_reg);
        }
    }

    rename.setRenameMap(renameMap);
    commit.setRenameMap(commitRenameMap);
    rename.setFreeList(&freeList);

    // Setup the ROB for whichever stages need it.
    commit.setROB(&rob);

    lastActivatedCycle = 0;
#if 0
    // Give renameMap & rename stage access to the freeList;
    for (ThreadID tid = 0; tid < numThreads; tid++)
        globalSeqNum[tid] = 1;
#endif

    DPRINTF(O3CPU, "Creating O3CPU object.\n");

    // Setup any thread state.
    this->thread.resize(this->numThreads);

    for (ThreadID tid = 0; tid < this->numThreads; ++tid) {
        if (FullSystem) {
            // SMT is not supported in FS mode yet.
            assert(this->numThreads == 1);
            this->thread[tid] = new Thread(this, 0, NULL);
        } else {
            if (tid < params->workload.size()) {
                DPRINTF(O3CPU, "Workload[%i] process is %#x",
                        tid, this->thread[tid]);
                this->thread[tid] = new typename FullO3CPU<Impl>::Thread(
                        (typename Impl::O3CPU *)(this),
                        tid, params->workload[tid]);

                //usedTids[tid] = true;
                //threadMap[tid] = tid;
            } else {
                //Allocate Empty thread so M5 can use later
                //when scheduling threads to CPU
                Process* dummy_proc = NULL;

                this->thread[tid] = new typename FullO3CPU<Impl>::Thread(
                        (typename Impl::O3CPU *)(this),
                        tid, dummy_proc);
                //usedTids[tid] = false;
            }
        }

        ThreadContext *tc;

        // Setup the TC that will serve as the interface to the threads/CPU.
        O3ThreadContext<Impl> *o3_tc = new O3ThreadContext<Impl>;

        tc = o3_tc;

        // If we're using a checker, then the TC should be the
        // CheckerThreadContext.
        if (params->checker) {
            tc = new CheckerThreadContext<O3ThreadContext<Impl> >(
                o3_tc, this->checker);
        }

        o3_tc->cpu = (typename Impl::O3CPU *)(this);
        assert(o3_tc->cpu);
        o3_tc->thread = this->thread[tid];

        if (FullSystem) {
            // Setup quiesce event.
            this->thread[tid]->quiesceEvent = new EndQuiesceEvent(tc);
        }
        // Give the thread the TC.
        this->thread[tid]->tc = tc;

        // Add the TC to the CPU's list of TC's.
        this->threadContexts.push_back(tc);
    }

    // FullO3CPU always requires an interrupt controller.
    if (!params->switched_out && !interrupts) {
        fatal("FullO3CPU %s has no interrupt controller.\n"
              "Ensure createInterruptController() is called.\n", name());
    }

    for (ThreadID tid = 0; tid < this->numThreads; tid++)
        this->thread[tid]->setFuncExeInst(0);

    iew.ldstQueue.init(params);
    rob.init(params);

    leastPortion = grain;
    // check control policy
    if (params->controlPolicy == "Combined") {
        controlPolicy = Combined;
    } else if (params->controlPolicy == "FrontEnd") {
        controlPolicy = FrontEnd;
    } else if (params->controlPolicy == "ILPOriented") {
        controlPolicy = ILPOriented;
        leastPortion = 256;
    } else if (params->controlPolicy == "Cazorla") {
        controlPolicy = Cazorla;
    } else if (params->controlPolicy == "None") {
        controlPolicy = None;
    } else {
        panic("Unknown Control Policy %s!\n", params->controlPolicy);
    }

    std::fill(curPhaseInsts.begin(), curPhaseInsts.end(), 0);
}

template <class Impl>
FullO3CPU<Impl>::~FullO3CPU()
{
}

template <class Impl>
void
FullO3CPU<Impl>::regProbePoints()
{
    BaseCPU::regProbePoints();

    ppInstAccessComplete = new ProbePointArg<PacketPtr>(getProbeManager(), "InstAccessComplete");
    ppDataAccessComplete = new ProbePointArg<std::pair<DynInstPtr, PacketPtr> >(getProbeManager(), "DataAccessComplete");

    fetch.regProbePoints();
    iew.regProbePoints();
    commit.regProbePoints();
}

template <class Impl>
void
FullO3CPU<Impl>::regStats()
{
    BaseO3CPU::regStats();

    // Register any of the O3CPU's stats here.
    timesIdled
        .name(name() + ".timesIdled")
        .desc("Number of times that the entire CPU went into an idle state and"
              " unscheduled itself")
        .prereq(timesIdled);

    idleCycles
        .name(name() + ".idleCycles")
        .desc("Total number of cycles that the CPU has spent unscheduled due "
              "to idling")
        .prereq(idleCycles);

    quiesceCycles
        .name(name() + ".quiesceCycles")
        .desc("Total number of cycles that CPU has spent quiesced or waiting "
              "for an interrupt")
        .prereq(quiesceCycles);

    // Number of Instructions simulated
    // --------------------------------
    // Should probably be in Base CPU but need templated
    // MaxThreads so put in here instead
    committedInsts
        .init(numThreads)
        .name(name() + ".committedInsts")
        .desc("Number of Instructions Simulated")
        .flags(Stats::total | Stats::display);

    committedOps
        .init(numThreads)
        .name(name() + ".committedOps")
        .desc("Number of Ops (including micro ops) Simulated")
        .flags(Stats::total);

    cpi
        .name(name() + ".cpi")
        .desc("CPI: Cycles Per Instruction")
        .precision(6);
    cpi = numCycles / committedInsts;

    totalCpi
        .name(name() + ".cpi_total")
        .desc("CPI: Total CPI of All Threads")
        .precision(6);
    totalCpi = numCycles / sum(committedInsts);

    ipc
        .name(name() + ".ipc")
        .desc("IPC: Instructions Per Cycle")
        .precision(6)
        .flags(Stats::display);
    ipc =  committedInsts / numCycles;

    totalIpc
        .name(name() + ".ipc_total")
        .desc("IPC: Total IPC of All Threads")
        .precision(6)
        .flags(Stats::display);
    totalIpc =  sum(committedInsts) / numCycles;

    this->fetch.regStats();
    this->decode.regStats();
    this->rename.regStats();
    this->iew.regStats();
    this->commit.regStats();
    this->rob.regStats();
    this->fmt.regStats();

    intRegfileReads
        .name(name() + ".int_regfile_reads")
        .desc("number of integer regfile reads")
        .prereq(intRegfileReads);

    intRegfileWrites
        .name(name() + ".int_regfile_writes")
        .desc("number of integer regfile writes")
        .prereq(intRegfileWrites);

    fpRegfileReads
        .name(name() + ".fp_regfile_reads")
        .desc("number of floating regfile reads")
        .prereq(fpRegfileReads);

    fpRegfileWrites
        .name(name() + ".fp_regfile_writes")
        .desc("number of floating regfile writes")
        .prereq(fpRegfileWrites);

    ccRegfileReads
        .name(name() + ".cc_regfile_reads")
        .desc("number of cc regfile reads")
        .prereq(ccRegfileReads);

    ccRegfileWrites
        .name(name() + ".cc_regfile_writes")
        .desc("number of cc regfile writes")
        .prereq(ccRegfileWrites);

    miscRegfileReads
        .name(name() + ".misc_regfile_reads")
        .desc("number of misc regfile reads")
        .prereq(miscRegfileReads);

    miscRegfileWrites
        .name(name() + ".misc_regfile_writes")
        .desc("number of misc regfile writes")
        .prereq(miscRegfileWrites);

    numInstsPerThread
        .name(name() + ".num_insts")
        .desc("number of insts executed by each thread")
        .flags(Stats::display)
        .init(numThreads);

    HPTQoS
        .name(name() + ".HPTQoS")
        .desc("Predicted QoS of HPT")
        .precision(6)
        .flags(Stats::display)
        ;

    HPTpredIPC
        .name(name() + ".HPTpredIPC")
        .desc("Predicted IPC of HPT when running alone")
        .prereq(HPTQoS)
        .precision(6)
        .flags(Stats::display)
        ;

    // HPTpredIPC[1] is meaningless!!
    HPTpredIPC = ipc / HPTQoS;

}


template <class Impl>
void
FullO3CPU<Impl>::tick()
{
    DPRINTF(O3CPU, "\n\nFullO3CPU: Ticking main, FullO3CPU.\n");
    assert(!switchedOut());
    assert(getDrainState() != Drainable::Drained);

    DPRINTFR(FMT,"Tick-----------------------------------\n");
    ++numCycles;
    ++localCycles;
    ++dumpCycles;
    ++policyCycles;
    ++curPhaseCycles;

    if (dumpCycles >= dumpWindowSize) {

        commit.rob->dumpUsedEntries();
        rename.dumpFreeEntries();
        iew.instQueue.dumpUsedEntries();
        iew.ldstQueue.dumpUsedEntries();
        fmt.dumpStats();
        dumpStats();

        async_event = true;
        async_statdump = true;
        dumpCycles = 0;
        getEventQueue(0)->wakeup();
    }

    if (controlPolicy == ControlPolicy::Combined ||
            controlPolicy == ControlPolicy::FrontEnd ||
            controlPolicy == ControlPolicy::ILPOriented) {
        if (policyCycles >= policyWindowSize) {
            resourceAdjust();

            rename.dumpStats();
            rename.clearFull();
            iew.dumpStats();
            iew.clearFull();

            policyCycles = 0;
        }

    } else if (controlPolicy == ControlPolicy::Cazorla) {
        if (curPhaseCycles >= phaseLength) {
            doCazorlaControl();
        }
    }

    ppCycles->notify(1);

//    activity = false;

    //Tick each of the stages
    fetch.tick();

    decode.tick();

    rename.tick();

    iew.tick();

    commit.tick();

    // Now advance the time buffers
    timeBuffer.advance();

    fetchQueue.advance();
    decodeQueue.advance();
    renameQueue.advance();
    iewQueue.advance();

    activityRec.advance();

    if (removeInstsThisCycle) {
        cleanUpRemovedInsts();
    }

    if (!tickEvent.scheduled()) {
        if (_status == SwitchedOut) {
            DPRINTF(O3CPU, "Switched out!\n");
            // increment stat
            lastRunningCycle = curCycle();
        } else if (!activityRec.active() || _status == Idle) {
            DPRINTF(O3CPU, "Idle!\n");
            lastRunningCycle = curCycle();
            timesIdled++;
        } else {
            schedule(tickEvent, clockEdge(Cycles(1)));
            DPRINTF(O3CPU, "Scheduling next tick!\n");
        }
    }

    if (!FullSystem)
        updateThreadPriority();

    tryDrain();

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        numInstsPerThread[tid] = thread[tid]->numInst;
    }
}

template <class Impl>
void
FullO3CPU<Impl>::init()
{
    BaseCPU::init();

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        // Set noSquashFromTC so that the CPU doesn't squash when initially
        // setting up registers.
        thread[tid]->noSquashFromTC = true;
        // Initialise the ThreadContext's memory proxies
        thread[tid]->initMemProxies(thread[tid]->getTC());
    }

    if (FullSystem && !params()->switched_out) {
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            ThreadContext *src_tc = threadContexts[tid];
            TheISA::initCPU(src_tc, src_tc->contextId());
        }
    }

    // Clear noSquashFromTC.
    for (int tid = 0; tid < numThreads; ++tid)
        thread[tid]->noSquashFromTC = false;

    commit.setThreads(thread);
}

template <class Impl>
void
FullO3CPU<Impl>::startup()
{
    BaseCPU::startup();
    for (int tid = 0; tid < numThreads; ++tid)
        isa[tid]->startup(threadContexts[tid]);


    fetch.startupStage();
    decode.startupStage();
    iew.startupStage();
    rename.startupStage();
    commit.startupStage();
}

template <class Impl>
void
FullO3CPU<Impl>::activateThread(ThreadID tid)
{
    list<ThreadID>::iterator isActive =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(O3CPU, "[tid:%i]: Calling activate thread.\n", tid);
    assert(!switchedOut());

    if (isActive == activeThreads.end()) {
        DPRINTF(O3CPU, "[tid:%i]: Adding to active threads list\n",
                tid);

        activeThreads.push_back(tid);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::deactivateThread(ThreadID tid)
{
    //Remove From Active List, if Active
    list<ThreadID>::iterator thread_it =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(O3CPU, "[tid:%i]: Calling deactivate thread.\n", tid);
    assert(!switchedOut());

    if (thread_it != activeThreads.end()) {
        DPRINTF(O3CPU,"[tid:%i]: Removing from active threads list\n",
                tid);
        activeThreads.erase(thread_it);
    }

    fetch.deactivateThread(tid);
    commit.deactivateThread(tid);
}

template <class Impl>
Counter
FullO3CPU<Impl>::totalInsts() const
{
    Counter total(0);

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++)
        total += thread[i]->numInst;

    return total;
}

template <class Impl>
Counter
FullO3CPU<Impl>::totalOps() const
{
    Counter total(0);

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++)
        total += thread[i]->numOp;

    return total;
}

template <class Impl>
void
FullO3CPU<Impl>::activateContext(ThreadID tid)
{
    assert(!switchedOut());

    // Needs to set each stage to running as well.
    activateThread(tid);

    // We don't want to wake the CPU if it is drained. In that case,
    // we just want to flag the thread as active and schedule the tick
    // event from drainResume() instead.
    if (getDrainState() == Drainable::Drained)
        return;

    // If we are time 0 or if the last activation time is in the past,
    // schedule the next tick and wake up the fetch unit
    if (lastActivatedCycle == 0 || lastActivatedCycle < curTick()) {
        scheduleTickEvent(Cycles(0));

        // Be sure to signal that there's some activity so the CPU doesn't
        // deschedule itself.
        activityRec.activity();
        fetch.wakeFromQuiesce();

        Cycles cycles(curCycle() - lastRunningCycle);
        // @todo: This is an oddity that is only here to match the stats
        if (cycles != 0)
            --cycles;
        quiesceCycles += cycles;

        lastActivatedCycle = curTick();

        _status = Running;
    }
}

template <class Impl>
void
FullO3CPU<Impl>::suspendContext(ThreadID tid)
{
    DPRINTF(O3CPU,"[tid: %i]: Suspending Thread Context.\n", tid);
    assert(!switchedOut());

    deactivateThread(tid);

    // If this was the last thread then unschedule the tick event.
    if (activeThreads.size() == 0) {
        unscheduleTickEvent();
        lastRunningCycle = curCycle();
        _status = Idle;
    }

    DPRINTF(Quiesce, "Suspending Context\n");
}

template <class Impl>
void
FullO3CPU<Impl>::haltContext(ThreadID tid)
{
    //For now, this is the same as deallocate
    DPRINTF(O3CPU,"[tid:%i]: Halt Context called. Deallocating", tid);
    assert(!switchedOut());

    deactivateThread(tid);
    removeThread(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::insertThread(ThreadID tid)
{
    DPRINTF(O3CPU,"[tid:%i] Initializing thread into CPU");
    // Will change now that the PC and thread state is internal to the CPU
    // and not in the ThreadContext.
    ThreadContext *src_tc;
    if (FullSystem)
        src_tc = system->threadContexts[tid];
    else
        src_tc = tcBase(tid);

    //Bind Int Regs to Rename Map
    for (int ireg = 0; ireg < TheISA::NumIntRegs; ireg++) {
        PhysRegIndex phys_reg = freeList.getIntReg();

        renameMap[tid].setEntry(ireg,phys_reg);
        scoreboard.setReg(phys_reg);
    }

    //Bind Float Regs to Rename Map
    int max_reg = TheISA::NumIntRegs + TheISA::NumFloatRegs;
    for (int freg = TheISA::NumIntRegs; freg < max_reg; freg++) {
        PhysRegIndex phys_reg = freeList.getFloatReg();

        renameMap[tid].setEntry(freg,phys_reg);
        scoreboard.setReg(phys_reg);
    }

    //Bind condition-code Regs to Rename Map
    max_reg = TheISA::NumIntRegs + TheISA::NumFloatRegs + TheISA::NumCCRegs;
    for (int creg = TheISA::NumIntRegs + TheISA::NumFloatRegs;
         creg < max_reg; creg++) {
        PhysRegIndex phys_reg = freeList.getCCReg();

        renameMap[tid].setEntry(creg,phys_reg);
        scoreboard.setReg(phys_reg);
    }

    //Copy Thread Data Into RegFile
    //this->copyFromTC(tid);

    //Set PC/NPC/NNPC
    pcState(src_tc->pcState(), tid);

    src_tc->setStatus(ThreadContext::Active);

    activateContext(tid);

    //Reset ROB/IQ/LSQ Entries
    commit.rob->resetEntries();
    iew.resetEntries();
}

template <class Impl>
void
FullO3CPU<Impl>::removeThread(ThreadID tid)
{
    DPRINTF(O3CPU,"[tid:%i] Removing thread context from CPU.\n", tid);

    // Copy Thread Data From RegFile
    // If thread is suspended, it might be re-allocated
    // this->copyToTC(tid);


    // @todo: 2-27-2008: Fix how we free up rename mappings
    // here to alleviate the case for double-freeing registers
    // in SMT workloads.

    // Unbind Int Regs from Rename Map
    for (int ireg = 0; ireg < TheISA::NumIntRegs; ireg++) {
        PhysRegIndex phys_reg = renameMap[tid].lookup(ireg);
        scoreboard.unsetReg(phys_reg);
        freeList.addReg(phys_reg);
    }

    // Unbind Float Regs from Rename Map
    int max_reg = TheISA::FP_Reg_Base + TheISA::NumFloatRegs;
    for (int freg = TheISA::FP_Reg_Base; freg < max_reg; freg++) {
        PhysRegIndex phys_reg = renameMap[tid].lookup(freg);
        scoreboard.unsetReg(phys_reg);
        freeList.addReg(phys_reg);
    }

    // Unbind condition-code Regs from Rename Map
    max_reg = TheISA::CC_Reg_Base + TheISA::NumCCRegs;
    for (int creg = TheISA::CC_Reg_Base; creg < max_reg; creg++) {
        PhysRegIndex phys_reg = renameMap[tid].lookup(creg);
        scoreboard.unsetReg(phys_reg);
        freeList.addReg(phys_reg);
    }

    // Squash Throughout Pipeline
    DynInstPtr inst = commit.rob->readHeadInst(tid);
    InstSeqNum squash_seq_num = inst->seqNum;
    fetch.squash(0, squash_seq_num, inst, tid);
    decode.squash(tid);
    rename.squash(squash_seq_num, tid);
    iew.squash(tid);
    iew.ldstQueue.squash(squash_seq_num, tid);
    commit.rob->squash(squash_seq_num, tid);


    assert(iew.instQueue.getCount(tid) == 0);
    assert(iew.ldstQueue.getCount(tid) == 0);

    // Reset ROB/IQ/LSQ Entries

    // Commented out for now.  This should be possible to do by
    // telling all the pipeline stages to drain first, and then
    // checking until the drain completes.  Once the pipeline is
    // drained, call resetEntries(). - 10-09-06 ktlim
/*
    if (activeThreads.size() >= 1) {
        commit.rob->resetEntries();
        iew.resetEntries();
    }
*/
}

template <class Impl>
Fault
FullO3CPU<Impl>::hwrei(ThreadID tid)
{
#if THE_ISA == ALPHA_ISA
    // Need to clear the lock flag upon returning from an interrupt.
    this->setMiscRegNoEffect(AlphaISA::MISCREG_LOCKFLAG, false, tid);

    this->thread[tid]->kernelStats->hwrei();

    // FIXME: XXX check for interrupts? XXX
#endif
    return NoFault;
}

template <class Impl>
bool
FullO3CPU<Impl>::simPalCheck(int palFunc, ThreadID tid)
{
#if THE_ISA == ALPHA_ISA
    if (this->thread[tid]->kernelStats)
        this->thread[tid]->kernelStats->callpal(palFunc,
                                                this->threadContexts[tid]);

    switch (palFunc) {
      case PAL::halt:
        halt();
        if (--System::numSystemsRunning == 0)
            exitSimLoop("all cpus halted");
        break;

      case PAL::bpt:
      case PAL::bugchk:
        if (this->system->breakpoint())
            return false;
        break;
    }
#endif
    return true;
}

template <class Impl>
Fault
FullO3CPU<Impl>::getInterrupts()
{
    // Check if there are any outstanding interrupts
    return this->interrupts->getInterrupt(this->threadContexts[0]);
}

template <class Impl>
void
FullO3CPU<Impl>::processInterrupts(const Fault &interrupt)
{
    // Check for interrupts here.  For now can copy the code that
    // exists within isa_fullsys_traits.hh.  Also assume that thread 0
    // is the one that handles the interrupts.
    // @todo: Possibly consolidate the interrupt checking code.
    // @todo: Allow other threads to handle interrupts.

    assert(interrupt != NoFault);
    this->interrupts->updateIntrInfo(this->threadContexts[0]);

    DPRINTF(O3CPU, "Interrupt %s being handled\n", interrupt->name());
    this->trap(interrupt, 0, nullptr);
}

template <class Impl>
void
FullO3CPU<Impl>::trap(const Fault &fault, ThreadID tid,
                      const StaticInstPtr &inst)
{
    // Pass the thread's TC into the invoke method.
    fault->invoke(this->threadContexts[tid], inst);
}

template <class Impl>
void
FullO3CPU<Impl>::syscall(int64_t callnum, ThreadID tid)
{
    DPRINTF(O3CPU, "[tid:%i] Executing syscall().\n\n", tid);

    DPRINTF(Activity,"Activity: syscall() called.\n");

    // Temporarily increase this by one to account for the syscall
    // instruction.
    ++(this->thread[tid]->funcExeInst);

    // Execute the actual syscall.
    this->thread[tid]->syscall(callnum);

    // Decrease funcExeInst by one as the normal commit will handle
    // incrementing it.
    --(this->thread[tid]->funcExeInst);
}

template <class Impl>
void
FullO3CPU<Impl>::serializeThread(std::ostream &os, ThreadID tid)
{
    thread[tid]->serialize(os);
}

template <class Impl>
void
FullO3CPU<Impl>::unserializeThread(Checkpoint *cp, const std::string &section,
                                   ThreadID tid)
{
    thread[tid]->unserialize(cp, section);
}

template <class Impl>
unsigned int
FullO3CPU<Impl>::drain(DrainManager *drain_manager)
{
    // If the CPU isn't doing anything, then return immediately.
    if (switchedOut()) {
        setDrainState(Drainable::Drained);
        return 0;
    }

    DPRINTF(Drain, "Draining...\n");
    setDrainState(Drainable::Draining);

    // We only need to signal a drain to the commit stage as this
    // initiates squashing controls the draining. Once the commit
    // stage commits an instruction where it is safe to stop, it'll
    // squash the rest of the instructions in the pipeline and force
    // the fetch stage to stall. The pipeline will be drained once all
    // in-flight instructions have retired.
    commit.drain();

    // Wake the CPU and record activity so everything can drain out if
    // the CPU was not able to immediately drain.
    if (!isDrained())  {
        drainManager = drain_manager;

        wakeCPU();
        activityRec.activity();

        DPRINTF(Drain, "CPU not drained\n");

        return 1;
    } else {
        setDrainState(Drainable::Drained);
        DPRINTF(Drain, "CPU is already drained\n");
        if (tickEvent.scheduled())
            deschedule(tickEvent);

        // Flush out any old data from the time buffers.  In
        // particular, there might be some data in flight from the
        // fetch stage that isn't visible in any of the CPU buffers we
        // test in isDrained().
        for (int i = 0; i < timeBuffer.getSize(); ++i) {
            timeBuffer.advance();
            fetchQueue.advance();
            decodeQueue.advance();
            renameQueue.advance();
            iewQueue.advance();
        }

        drainSanityCheck();
        return 0;
    }
}

template <class Impl>
bool
FullO3CPU<Impl>::tryDrain()
{
    if (!drainManager || !isDrained())
        return false;

    if (tickEvent.scheduled())
        deschedule(tickEvent);

    DPRINTF(Drain, "CPU done draining, processing drain event\n");
    drainManager->signalDrainDone();
    drainManager = NULL;

    return true;
}

template <class Impl>
void
FullO3CPU<Impl>::drainSanityCheck() const
{
    assert(isDrained());
    fetch.drainSanityCheck();
    decode.drainSanityCheck();
    rename.drainSanityCheck();
    iew.drainSanityCheck();
    commit.drainSanityCheck();
}

template <class Impl>
bool
FullO3CPU<Impl>::isDrained() const
{
    bool drained(true);

    if (!instList.empty() || !removeList.empty()) {
        DPRINTF(Drain, "Main CPU structures not drained.\n");
        drained = false;
    }

    if (!fetch.isDrained()) {
        DPRINTF(Drain, "Fetch not drained.\n");
        drained = false;
    }

    if (!decode.isDrained()) {
        DPRINTF(Drain, "Decode not drained.\n");
        drained = false;
    }

    if (!rename.isDrained()) {
        DPRINTF(Drain, "Rename not drained.\n");
        drained = false;
    }

    if (!iew.isDrained()) {
        DPRINTF(Drain, "IEW not drained.\n");
        drained = false;
    }

    if (!commit.isDrained()) {
        DPRINTF(Drain, "Commit not drained.\n");
        drained = false;
    }

    return drained;
}

template <class Impl>
void
FullO3CPU<Impl>::commitDrained(ThreadID tid)
{
    fetch.drainStall(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::drainResume()
{
    setDrainState(Drainable::Running);
    if (switchedOut())
        return;

    DPRINTF(Drain, "Resuming...\n");
    verifyMemoryMode();

    fetch.drainResume();
    commit.drainResume();

    _status = Idle;
    for (ThreadID i = 0; i < thread.size(); i++) {
        if (thread[i]->status() == ThreadContext::Active) {
            DPRINTF(Drain, "Activating thread: %i\n", i);
            activateThread(i);
            _status = Running;
        }
    }

    assert(!tickEvent.scheduled());
    if (_status == Running)
        schedule(tickEvent, nextCycle());
}

template <class Impl>
void
FullO3CPU<Impl>::switchOut()
{
    DPRINTF(O3CPU, "Switching out\n");
    BaseCPU::switchOut();

    activityRec.reset();

    _status = SwitchedOut;

    if (checker)
        checker->switchOut();
}

template <class Impl>
void
FullO3CPU<Impl>::takeOverFrom(BaseCPU *oldCPU)
{
    BaseCPU::takeOverFrom(oldCPU);

    fetch.takeOverFrom();
    decode.takeOverFrom();
    rename.takeOverFrom();
    iew.takeOverFrom();
    commit.takeOverFrom();

    assert(!tickEvent.scheduled());

    FullO3CPU<Impl> *oldO3CPU = dynamic_cast<FullO3CPU<Impl>*>(oldCPU);
    if (oldO3CPU)
        globalSeqNum = oldO3CPU->globalSeqNum;

    lastRunningCycle = curCycle();
    _status = Idle;
}

template <class Impl>
void
FullO3CPU<Impl>::verifyMemoryMode() const
{
    if (!system->isTimingMode()) {
        fatal("The O3 CPU requires the memory system to be in "
              "'timing' mode.\n");
    }
}

template <class Impl>
TheISA::MiscReg
FullO3CPU<Impl>::readMiscRegNoEffect(int misc_reg, ThreadID tid) const
{
    return this->isa[tid]->readMiscRegNoEffect(misc_reg);
}

template <class Impl>
TheISA::MiscReg
FullO3CPU<Impl>::readMiscReg(int misc_reg, ThreadID tid)
{
    miscRegfileReads++;
    return this->isa[tid]->readMiscReg(misc_reg, tcBase(tid));
}

template <class Impl>
void
FullO3CPU<Impl>::setMiscRegNoEffect(int misc_reg,
        const TheISA::MiscReg &val, ThreadID tid)
{
    this->isa[tid]->setMiscRegNoEffect(misc_reg, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setMiscReg(int misc_reg,
        const TheISA::MiscReg &val, ThreadID tid)
{
    miscRegfileWrites++;
    this->isa[tid]->setMiscReg(misc_reg, val, tcBase(tid));
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readIntReg(int reg_idx)
{
    intRegfileReads++;
    return regFile.readIntReg(reg_idx);
}

template <class Impl>
FloatReg
FullO3CPU<Impl>::readFloatReg(int reg_idx)
{
    fpRegfileReads++;
    return regFile.readFloatReg(reg_idx);
}

template <class Impl>
FloatRegBits
FullO3CPU<Impl>::readFloatRegBits(int reg_idx)
{
    fpRegfileReads++;
    return regFile.readFloatRegBits(reg_idx);
}

template <class Impl>
CCReg
FullO3CPU<Impl>::readCCReg(int reg_idx)
{
    ccRegfileReads++;
    return regFile.readCCReg(reg_idx);
}

template <class Impl>
void
FullO3CPU<Impl>::setIntReg(int reg_idx, uint64_t val)
{
    intRegfileWrites++;
    regFile.setIntReg(reg_idx, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setFloatReg(int reg_idx, FloatReg val)
{
    fpRegfileWrites++;
    regFile.setFloatReg(reg_idx, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setFloatRegBits(int reg_idx, FloatRegBits val)
{
    fpRegfileWrites++;
    regFile.setFloatRegBits(reg_idx, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setCCReg(int reg_idx, CCReg val)
{
    ccRegfileWrites++;
    regFile.setCCReg(reg_idx, val);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readArchIntReg(int reg_idx, ThreadID tid)
{
    intRegfileReads++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupInt(reg_idx);

    return regFile.readIntReg(phys_reg);
}

template <class Impl>
float
FullO3CPU<Impl>::readArchFloatReg(int reg_idx, ThreadID tid)
{
    fpRegfileReads++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupFloat(reg_idx);

    return regFile.readFloatReg(phys_reg);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readArchFloatRegInt(int reg_idx, ThreadID tid)
{
    fpRegfileReads++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupFloat(reg_idx);

    return regFile.readFloatRegBits(phys_reg);
}

template <class Impl>
CCReg
FullO3CPU<Impl>::readArchCCReg(int reg_idx, ThreadID tid)
{
    ccRegfileReads++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupCC(reg_idx);

    return regFile.readCCReg(phys_reg);
}

template <class Impl>
void
FullO3CPU<Impl>::setArchIntReg(int reg_idx, uint64_t val, ThreadID tid)
{
    intRegfileWrites++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupInt(reg_idx);

    regFile.setIntReg(phys_reg, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setArchFloatReg(int reg_idx, float val, ThreadID tid)
{
    fpRegfileWrites++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupFloat(reg_idx);

    regFile.setFloatReg(phys_reg, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setArchFloatRegInt(int reg_idx, uint64_t val, ThreadID tid)
{
    fpRegfileWrites++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupFloat(reg_idx);

    regFile.setFloatRegBits(phys_reg, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setArchCCReg(int reg_idx, CCReg val, ThreadID tid)
{
    ccRegfileWrites++;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookupCC(reg_idx);

    regFile.setCCReg(phys_reg, val);
}

template <class Impl>
TheISA::PCState
FullO3CPU<Impl>::pcState(ThreadID tid)
{
    return commit.pcState(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::pcState(const TheISA::PCState &val, ThreadID tid)
{
    commit.pcState(val, tid);
}

template <class Impl>
Addr
FullO3CPU<Impl>::instAddr(ThreadID tid)
{
    return commit.instAddr(tid);
}

template <class Impl>
Addr
FullO3CPU<Impl>::nextInstAddr(ThreadID tid)
{
    return commit.nextInstAddr(tid);
}

template <class Impl>
MicroPC
FullO3CPU<Impl>::microPC(ThreadID tid)
{
    return commit.microPC(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::squashFromTC(ThreadID tid)
{
    this->thread[tid]->noSquashFromTC = true;
    this->commit.generateTCEvent(tid);
}

template <class Impl>
typename FullO3CPU<Impl>::ListIt
FullO3CPU<Impl>::addInst(DynInstPtr &inst)
{
    instList.push_back(inst);

    return --(instList.end());
}

template <class Impl>
void
FullO3CPU<Impl>::instDone(ThreadID tid, DynInstPtr &inst)
{
    // Keep an instruction count.
    if (!inst->isMicroop() || inst->isLastMicroop()) {
        thread[tid]->numInst++;
        thread[tid]->numInsts++;
        committedInsts[tid]++;
        curPhaseInsts[tid]++;
        system->totalNumInsts++;

        // Check for instruction-count-based events.
        comInstEventQueue[tid]->serviceEvents(thread[tid]->numInst);
        system->instEventQueue.serviceEvents(system->totalNumInsts);
    }
    thread[tid]->numOp++;
    thread[tid]->numOps++;
    committedOps[tid]++;

    probeInstCommit(inst->staticInst);
}

template <class Impl>
void
FullO3CPU<Impl>::removeFrontInst(DynInstPtr &inst)
{
    DPRINTF(O3CPU, "Removing committed instruction [tid:%i] PC %s "
            "[sn:%lli]\n",
            inst->threadNumber, inst->pcState(), inst->seqNum);

    removeInstsThisCycle = true;

    // Remove the front instruction.
    removeList.push(inst->getInstListIt());
}

template <class Impl>
void
FullO3CPU<Impl>::removeInstsNotInROB(ThreadID tid)
{
    DPRINTF(O3CPU, "Thread %i: Deleting instructions from instruction"
            " list.\n", tid);

    ListIt end_it;

    bool rob_empty = false;

    if (instList.empty()) {
        return;
    } else if (rob.isEmpty(tid)) {
        DPRINTF(O3CPU, "ROB is empty, squashing all insts.\n");
        end_it = instList.begin();
        rob_empty = true;
    } else {
        end_it = (rob.readTailInst(tid))->getInstListIt();
        DPRINTF(O3CPU, "ROB is not empty, squashing insts not in ROB.\n");
    }

    removeInstsThisCycle = true;

    ListIt inst_it = instList.end();

    inst_it--;

    // Walk through the instruction list, removing any instructions
    // that were inserted after the given instruction iterator, end_it.
    while (inst_it != end_it) {
        assert(!instList.empty());

        squashInstIt(inst_it, tid);

        inst_it--;
    }

    // If the ROB was empty, then we actually need to remove the first
    // instruction as well.
    if (rob_empty) {
        squashInstIt(inst_it, tid);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::removeInstsUntil(const InstSeqNum &seq_num, ThreadID tid)
{
    assert(!instList.empty());

    removeInstsThisCycle = true;

    ListIt inst_iter = instList.end();

    inst_iter--;

    DPRINTF(O3CPU, "Deleting instructions from instruction "
            "list that are from [tid:%i] and above [sn:%lli] (end=%lli).\n",
            tid, seq_num, (*inst_iter)->seqNum);

    while ((*inst_iter)->seqNum > seq_num) {

        bool break_loop = (inst_iter == instList.begin());

        squashInstIt(inst_iter, tid);

        inst_iter--;

        if (break_loop)
            break;
    }
}

template <class Impl>
inline void
FullO3CPU<Impl>::squashInstIt(const ListIt &instIt, ThreadID tid)
{
    if ((*instIt)->threadNumber == tid) {
        DPRINTF(O3CPU, "Squashing instruction, "
                "[tid:%i] [sn:%lli] PC %s\n",
                (*instIt)->threadNumber,
                (*instIt)->seqNum,
                (*instIt)->pcState());

        // Mark it as squashed.
        (*instIt)->setSquashed();

        // @todo: Formulate a consistent method for deleting
        // instructions from the instruction list
        // Remove the instruction from the list.
        removeList.push(instIt);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::cleanUpRemovedInsts()
{
    while (!removeList.empty()) {
        DPRINTF(O3CPU, "Removing instruction, "
                "[tid:%i] [sn:%lli] PC %s\n",
                (*removeList.front())->threadNumber,
                (*removeList.front())->seqNum,
                (*removeList.front())->pcState());

        instList.erase(removeList.front());

        removeList.pop();
    }

    removeInstsThisCycle = false;
}
/*
template <class Impl>
void
FullO3CPU<Impl>::removeAllInsts()
{
    instList.clear();
}
*/
template <class Impl>
void
FullO3CPU<Impl>::dumpInsts()
{
    int num = 0;

    ListIt inst_list_it = instList.begin();

    cprintf("Dumping Instruction List\n");

    while (inst_list_it != instList.end()) {
        cprintf("Instruction:%i\nPC:%#x\n[tid:%i]\n[sn:%lli]\nIssued:%i\n"
                "Squashed:%i\n\n",
                num, (*inst_list_it)->instAddr(), (*inst_list_it)->threadNumber,
                (*inst_list_it)->seqNum, (*inst_list_it)->isIssued(),
                (*inst_list_it)->isSquashed());
        inst_list_it++;
        ++num;
    }
}
/*
template <class Impl>
void
FullO3CPU<Impl>::wakeDependents(DynInstPtr &inst)
{
    iew.wakeDependents(inst);
}
*/
template <class Impl>
void
FullO3CPU<Impl>::wakeCPU()
{
    if (activityRec.active() || tickEvent.scheduled()) {
        DPRINTF(Activity, "CPU already running.\n");
        return;
    }

    DPRINTF(Activity, "Waking up CPU\n");

    Cycles cycles(curCycle() - lastRunningCycle);
    // @todo: This is an oddity that is only here to match the stats
    if (cycles > 1) {
        --cycles;
        idleCycles += cycles;
        numCycles += cycles;
        localCycles += cycles;
        dumpCycles += cycles;
        policyCycles += cycles;
        curPhaseCycles += cycles;
        ppCycles->notify(cycles);
    }

    schedule(tickEvent, clockEdge());
}

template <class Impl>
void
FullO3CPU<Impl>::wakeup()
{
    if (this->thread[0]->status() != ThreadContext::Suspended)
        return;

    this->wakeCPU();

    DPRINTF(Quiesce, "Suspended Processor woken\n");
    this->threadContexts[0]->activate();
}

template <class Impl>
ThreadID
FullO3CPU<Impl>::getFreeTid()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!tids[tid]) {
            tids[tid] = true;
            return tid;
        }
    }

    return InvalidThreadID;
}

template <class Impl>
void
FullO3CPU<Impl>::updateThreadPriority()
{
    if (activeThreads.size() > 1) {
        //DEFAULT TO ROUND ROBIN SCHEME
        //e.g. Move highest priority to end of thread list
        list<ThreadID>::iterator list_begin = activeThreads.begin();

        unsigned high_thread = *list_begin;

        activeThreads.erase(list_begin);

        activeThreads.push_back(high_thread);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::setUpSrcManagerConfigs(const std::string filename)
{
    std::ifstream configs(filename);
    std::string key;
    int value;

    assert(configs && "Missing config file");

    while (configs >> key >> value) {
        DPRINTF(O3CPU, "%s -> %d\n", key.c_str(), value);
        srcManagerConfig[key] = value;
    }
}

template <class Impl>
void
FullO3CPU<Impl>::dumpStats()
{
    ThreadID hpt = 0;

    uint64_t predicted = fmt.globalBase[hpt] + fmt.globalMiss[hpt] +
        fmt.getHptNonWait();

    uint64_t real = predicted + fmt.globalWait[hpt] + fmt.getHptWait();

    HPTQoS = double(predicted)/double(real);
}

template <class Impl>
bool
FullO3CPU<Impl>::satisfiedQoS()
{

    uint64_t predicted_st_slots = fmt.globalBase[HPT] + fmt.globalMiss[HPT] +
        fmt.getHptNonWait();
    uint64_t smt_slots= predicted_st_slots + fmt.globalWait[HPT] + fmt.getHptWait();

    bool satisfied = predicted_st_slots*1024 > smt_slots*expectedQoS;

    DPRINTF(QoSCtrl, "predicted_st_slots: %i, SMT slots: %i, expectedQoS: %i"
            ", satisfied: %i\n", predicted_st_slots,
            smt_slots, expectedQoS, satisfied);

    return satisfied;
}

template <class Impl>
void
FullO3CPU<Impl>::allocFetch(bool incHPT)
{
    int vec[2];
    int delta = incHPT ? grain : -grain;

    vec[HPT] = fetch.getHPTPortion() + delta;
    vec[HPT] = std::min(vec[HPT], HPTMaxQuota);
    vec[HPT] = std::max(vec[HPT], HPTMinQuota);
    vec[LPT] = 1024 - vec[HPT];

    if (vec[HPT] != fetch.getHPTPortion()) {
        DPRINTF(QoSCtrl, "%s [Fetch], vec[0]: %d, vec[1]: %d\n",
                incHPT ? "Reserving":"Releasing", vec[HPT], vec[LPT]);
        fetch.reassignFetchSlice(vec, 1024);
    }
}

#define portionRangeCheck \
do { \
    vec[HPT] = std::min(vec[HPT], HPTMaxQuota); \
    vec[HPT] = std::max(vec[HPT], HPTMinQuota); \
    vec[LPT] = 1024 - vec[HPT]; \
} while(0)

template <class Impl>
void
FullO3CPU<Impl>::allocROB(bool incHPT)
{
    int vec[2];
    int delta = incHPT ? grain : -grain;
    vec[HPT] = commit.rob->getHPTPortion() + delta;
    portionRangeCheck;

    if (vec[HPT] != commit.rob->getHPTPortion()) {
        DPRINTF(QoSCtrl, "%s [ROB], vec[0]: %d, vec[1]: %d\n",
                incHPT ? "Reserving":"Releasing", vec[0], vec[1]);
        commit.rob->reassignPortion(vec, 2, 1024);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::allocIQ(bool incHPT)
{
    int vec[2];
    int delta = incHPT ? grain : -grain;
    vec[HPT] = iew.instQueue.getHPTPortion() + delta;
    portionRangeCheck;

    if (vec[HPT] != iew.instQueue.getHPTPortion()) {
        DPRINTF(QoSCtrl, "%s [IQ], vec[0]: %d, vec[1]: %d\n",
                incHPT ? "Reserving":"Releasing", vec[0], vec[1]);
        iew.instQueue.reassignPortion(vec, 2, 1024,
                controlPolicy == ControlPolicy::Cazorla);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::allocLQ(bool incHPT)
{
    int vec[2];
    int delta = incHPT ? grain : -grain;
    vec[0] = iew.ldstQueue.getHPTLQPortion() + delta;
    portionRangeCheck;

    if (vec[0] != iew.ldstQueue.getHPTLQPortion()) {
        DPRINTF(QoSCtrl, "%s [LQ], vec[0]: %d, vec[1]: %d\n",
                incHPT ? "Reserving":"Releasing", vec[0], vec[1]);
        iew.ldstQueue.reassignLQPortion(vec, 2, 1024);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::allocSQ(bool incHPT)
{
    int vec[2];
    int delta = incHPT ? grain : -grain;
    vec[0] = iew.ldstQueue.getHPTSQPortion() + delta;
    portionRangeCheck;

    if (vec[0] != iew.ldstQueue.getHPTSQPortion()) {
        DPRINTF(QoSCtrl, "%s [SQ], vec[0]: %d, vec[1]: %d\n",
                incHPT ? "Reserving":"Releasing", vec[0], vec[1]);
        iew.ldstQueue.reassignSQPortion(vec, 2, 1024);
    }
}

#undef portionRangeCheck

template <class Impl>
void
FullO3CPU<Impl>::allocCache(int cacheLevel, bool DCache, bool incHPT)
{
    WayRationConfig *wayRationConfig = nullptr;
    if (cacheLevel == 2) {
        wayRationConfig = &controlPanel.l2CacheWayConfig;
    } else if (cacheLevel == 1) {
        if (DCache) {
            wayRationConfig = &controlPanel.l1DCacheWayConfig;
        } else {
            wayRationConfig = &controlPanel.l1ICacheWayConfig;
        }
    } else {
        panic("Unknown cache level: %i\n", cacheLevel);
        wayRationConfig = &controlPanel.l2CacheWayConfig;
    }

    using namespace std;

    int delta = incHPT ? 1 : -1;
    int HPTAssoc = wayRationConfig->threadWayRations[HPT];
    int newHPTAssoc = min(max(HPTAssoc + delta, 1), wayRationConfig->assoc - 1);
    if (newHPTAssoc != HPTAssoc) {
        wayRationConfig->updatedByCore = true;
        wayRationConfig->threadWayRations[HPT] = newHPTAssoc;
        wayRationConfig->threadWayRations[LPT] = wayRationConfig->assoc - newHPTAssoc;
    }
}

template <class Impl>
void
FullO3CPU<Impl>::sortContention() {
    std::array<uint64_t, ContentionNum> contNums;
    std::array<size_t , ContentionNum> contIndices;
    std::iota(contIndices.begin(), contIndices.end(), 0);

    //<editor-fold desc="Contention aggregation">
    contNums[Contention::FetchCont] = iew.recentSlots[SlotsUse::FetchSliceWait]
                                      + iew.recentSlots[SlotsUse::SplitWait]
                                      + iew.recentSlots[SlotsUse::WidthWait];

    contNums[Contention::ROBCont] = iew.recentSlots[SlotsUse::ROBWait];
    contNums[Contention::IQCont] = iew.recentSlots[SlotsUse::IQWait];
    contNums[Contention::LQCont] = iew.recentSlots[SlotsUse::LQWait];
    contNums[Contention::SQCont] = iew.recentSlots[SlotsUse::SQWait];

    contNums[Contention::L1DCacheCont] = iew.recentSlots[SlotsUse::L1DCacheInterference];
    contNums[Contention::L1ICacheCont] = iew.recentSlots[SlotsUse::L1ICacheInterference];
    contNums[Contention::L2CacheCont] = iew.recentSlots[SlotsUse::L2DCacheInterference]
                                        + iew.recentSlots[SlotsUse::L2ICacheInterference];
    //</editor-fold>

    std::sort(contIndices.begin(), contIndices.end(),
              [&contNums](size_t x, size_t y) {return contNums[x] < contNums[y];});

    for (size_t i = 0; i < ContentionNum; ++i) {
        rankedContentions[i] = static_cast<Contention>((int)contIndices[i]);
    }

    iew.clearRecent();
}

template <class Impl>
void
FullO3CPU<Impl>::resourceAdjust() {
    if (controlPolicy == ControlPolicy::Combined ||
        controlPolicy == ControlPolicy::FrontEnd) {
        sortContention();
        int numAdjusted = 0;

        if (!satisfiedQoS()) {
            for (auto it = rankedContentions.rbegin();
                 it != rankedContentions.rend() && numAdjusted < numResourceToReserve;
                 ++it) {
                numAdjusted += adjustRoute(*it, true);
            }
        } else {
            for (auto it = rankedContentions.begin();
                 it != rankedContentions.end() && numAdjusted < numResourceToRelease;
                 ++it) {
                numAdjusted += adjustRoute(*it, false);
            }
        }
        return;
    }

    if (controlPolicy == ControlPolicy::ILPOriented) {
        double ILP0 = ILP_predictor[HPT].getILP();
        double ILP1 = ILP_predictor[LPT].getILP();
        ILP_predictor[HPT].clear();
        ILP_predictor[LPT].clear();
        DPRINTF(ILPPred, "T[0] ILP: %f; T[1] ILP: %f\n", ILP0, ILP1);

        allocROB(ILP0 >= ILP1);
        allocIQ(ILP0 >= ILP1);
        allocLQ(ILP0 >= ILP1);
        allocSQ(ILP0 >= ILP1);

        return;
    }

    panic("Unkown Control Policy\n");
}

template <class Impl>
int
FullO3CPU<Impl>::adjustRoute(Contention contention, bool incHPT)
{
    switch (contention) {
        case Contention::L1DCacheCont:
            if (!dynCache) return 0;
            allocCache(1, true, incHPT);
            break;
        case Contention::L1ICacheCont:
            if (!dynCache) return 0;
            allocCache(1, false, incHPT);
            break;
        case Contention::L2CacheCont:
            if (!dynCache) return 0;
            allocCache(2, true, incHPT);
            break;
        case Contention::FetchCont:
            allocFetch(incHPT);
            break;
        case Contention::ROBCont:
            if (controlPolicy == ControlPolicy::FrontEnd) return 0;
            allocROB(incHPT);
            break;
        case Contention::IQCont:
            if (controlPolicy == ControlPolicy::FrontEnd) return 0;
            allocIQ(incHPT);
            break;
        case Contention::LQCont:
            if (controlPolicy == ControlPolicy::FrontEnd) return 0;
            allocLQ(incHPT);
            break;
        case Contention::SQCont:
            if (controlPolicy == ControlPolicy::FrontEnd) return 0;
            allocSQ(incHPT);
            break;
        default:
            panic("Unexpected type of contention!\n");

    }
    return 1;
}

template <class Impl>
void
FullO3CPU<Impl>::doCazorlaControl()
{
    if (cazorlaPhase == CazorlaPhase::Tuning) {
        if (subTuningPhaseNumber != 80) {
            // Compute local IPC and compensation term
            localIPC = div(curPhaseInsts[HPT], curPhaseCycles);
            if (localIPC < targetIPC) {
                compensationTerm += 5;
            } else if (localIPC > targetIPC && compensationTerm >= 5) {
                compensationTerm -= 5;
            }

            // Compute local target IPC
            double expectedQoS_d = (double) expectedQoS;
            double compensatedQoS = expectedQoS_d * 100 / 1024 + compensationTerm;
            localTargetIPC = compensatedQoS * sampledIPC / 100;

            bool incHPT = localIPC < localTargetIPC;
            DPRINTF(Cazorla, "curPhaseInsts[HPT] = %i\n", curPhaseInsts[HPT]);
            DPRINTF(Cazorla, "local target IPC is %f, local IPC is %f\n"
                    "is to %s HPT quota\n",
                    localTargetIPC, localIPC, incHPT ? "inc" : "dec");

            // Resource allocation
            allocAllResource(incHPT);

            continueTuning();

        } else {
            DPRINTF(Cazorla, "==== End Tuning\n");
            assignAllResource2HPT();
            switch2Presample();
        }

    } else if (cazorlaPhase == CazorlaPhase::NotStarted) {
        assignAllResource2HPT();
        switch2Presample();

    } else if (cazorlaPhase == CazorlaPhase::Presample) {
        DPRINTF(Cazorla, "==== End PreSample\n");
        switch2Sampling();

    } else if (cazorlaPhase == CazorlaPhase::Sampling) {
        DPRINTF(Cazorla, "==== End Sample\n");
        // compute target IPC
        sampledIPC = div(curPhaseInsts[HPT], curPhaseCycles);
        targetIPC = sampledIPC * expectedQoS / 1024;

        localTargetIPC = targetIPC;
        DPRINTF(Cazorla, "phaseLength = %i\n", phaseLength);
        DPRINTF(Cazorla, "curPhaseCycles = %i\n", curPhaseCycles);
        DPRINTF(Cazorla, "curPhaseInsts[HPT] = %i\n", curPhaseInsts[HPT]);
        DPRINTF(Cazorla, "curPhaseInsts[LPT] = %i\n", curPhaseInsts[LPT]);
        DPRINTF(Cazorla, "sampledIPC = %f\n", sampledIPC);

        // switch to SMT, allocate half of resources to HPT
        assignHalfResource2HPT();

        switch2Tuning();
    }
}

template <class Impl>
void
FullO3CPU<Impl>::continueTuning()
{
    phaseLength = numSubPhaseCycles;
    curPhaseCycles = 0;
    std::fill(curPhaseInsts.begin(), curPhaseInsts.end(), 0);
    subTuningPhaseNumber += 1;
    DPRINTF(Cazorla, "==== Cazorla: %s\n", __func__);
}

template <class Impl>
void
FullO3CPU<Impl>::switch2Presample()
{
    cazorlaPhase = CazorlaPhase::Presample;
    phaseLength = numPreSampleCycles;
    curPhaseCycles = 0;
    std::fill(curPhaseInsts.begin(), curPhaseInsts.end(), 0);
    DPRINTF(Cazorla, "==== Cazorla: %s\n", __func__);
}

template <class Impl>
void
FullO3CPU<Impl>::switch2Sampling()
{
    cazorlaPhase = CazorlaPhase::Sampling;
    phaseLength = numSampleCycles;
    curPhaseCycles = 0;
    std::fill(curPhaseInsts.begin(), curPhaseInsts.end(), 0);
    DPRINTF(Cazorla, "==== Cazorla: %s\n", __func__);
}

template <class Impl>
void
FullO3CPU<Impl>::switch2Tuning()
{
    cazorlaPhase = CazorlaPhase::Tuning;
    phaseLength = numSubPhaseCycles;
    subTuningPhaseNumber = 0;
    curPhaseCycles = 0;
    std::fill(curPhaseInsts.begin(), curPhaseInsts.end(), 0);
    DPRINTF(Cazorla, "==== Cazorla: %s\n", __func__);
}

template <class Impl>
void
FullO3CPU<Impl>::assignFetch(int quota)
{
    DPRINTF(Cazorla, "Allocate %d fetch opportunities to HPT\n", quota);
    cazorlaVec[0] = quota;
    cazorlaVec[1] = 1024 - quota;
    fetch.reassignFetchSlice(cazorlaVec, 1024);
}

template <class Impl>
void
FullO3CPU<Impl>::assignROB(int quota)
{
    DPRINTF(Cazorla, "Allocate %d to HPT\n", quota);
    cazorlaVec[0] = quota;
    cazorlaVec[1] = 1024 - quota;
    commit.rob->reassignPortion(cazorlaVec, 2, 1024);
}

template <class Impl>
void
FullO3CPU<Impl>::assignIQ(int quota)
{
    DPRINTF(Cazorla, "Allocate %d IQ to HPT\n", quota);
    cazorlaVec[0] = quota;
    cazorlaVec[1] = 1024 - quota;
    iew.instQueue.reassignPortion(cazorlaVec, 2, 1024,
            controlPolicy == ControlPolicy::Cazorla);
}

template <class Impl>
void
FullO3CPU<Impl>::assignLQ(int quota)
{
    DPRINTF(Cazorla, "Allocate %d LQ to HPT\n", quota);
    cazorlaVec[0] = quota;
    cazorlaVec[1] = 1024 - quota;
    iew.ldstQueue.reassignLQPortion(cazorlaVec, 2, 1024);
}

template <class Impl>
void
FullO3CPU<Impl>::assignSQ(int quota)
{
    DPRINTF(Cazorla, "Allocate %d SQ to HPT\n", quota);
    cazorlaVec[0] = quota;
    cazorlaVec[1] = 1024 - quota;
    iew.ldstQueue.reassignSQPortion(cazorlaVec, 2, 1024);
}

template <class Impl>
void
FullO3CPU<Impl>::assignL2Cache(int quota)
{
    DPRINTF(Cazorla, "Allocate %d cache to HPT\n", quota);
    int cacheAssoc;

    // Note that Cazorla does not requires L1 configuration
    // So TODO: use Normal cache for L1
    //    cacheAssoc = controlPanel.l1ICacheWayConfig.assoc;
    //    reConfigOneCache(controlPanel.l1ICacheWayConfig, cacheAssoc);
    //
    //    cacheAssoc = controlPanel.l1DCacheWayConfig.assoc;
    //    reConfigOneCache(controlPanel.l1DCacheWayConfig, cacheAssoc);

    // NOTE that LPT assoc should be at least one
    quota = std::min(1024-128, quota);
    DPRINTF(ResourceAllocation, "Thread [%i] L2 cache portion: %i\n",
            0, quota);
    cacheAssoc = controlPanel.l2CacheWayConfig.assoc * quota / 1024;
    reConfigOneCache(controlPanel.l2CacheWayConfig, cacheAssoc);
}

template <class Impl>
void
FullO3CPU<Impl>::reConfigOneCache(
        WayRationConfig &wayRationConfig, int HPTAssoc)
{
    assert(HPTAssoc <= wayRationConfig.assoc);
    wayRationConfig.threadWayRations[HPT] = HPTAssoc;
    wayRationConfig.threadWayRations[LPT] =
            wayRationConfig.assoc - HPTAssoc;

    wayRationConfig.updatedByCore = true;
}

template <class Impl>
void
FullO3CPU<Impl>::assignAllResource2HPT()
{
    assignFetch(1024);
    assignROB(1024);
    assignIQ(1024);
    assignLQ(1024);
    assignSQ(1024);
    assignL2Cache(1024);
}

template <class Impl>
double
FullO3CPU<Impl>::div(unsigned x, unsigned y)
{
    assert(y != 0);
    return ((double) x) / ((double) y);
}

template <class Impl>
void
FullO3CPU<Impl>::allocAllResource(bool incHPT)
{
    allocFetch(incHPT);
    allocROB(incHPT);
    allocIQ(incHPT); // Including issue width
    allocLQ(incHPT);
    allocSQ(incHPT);
    allocCache(2, true, incHPT);
}

template <class Impl>
void
FullO3CPU<Impl>::assignHalfResource2HPT()
{
    assignFetch(512);
    assignROB(512);
    assignIQ(512);
    assignLQ(512);
    assignSQ(512);
    assignL2Cache(512);
}

// Forward declaration of FullO3CPU.
template class FullO3CPU<O3CPUImpl>;
