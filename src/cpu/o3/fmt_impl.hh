#ifndef __CPU_O3_FMT_IMPL_HH__
#define __CPU_O3_FMT_IMPL_HH__

#define __STDC_FORMAT_MACROS // for PRIu64 macro
#include <cinttypes>

#include "cpu/o3/comm.hh"
#include "debug/FMT.hh"
#include "debug/FmtSlot.hh"
#include "params/DerivO3CPU.hh"
#include "cpu/o3/fmt.hh"

using namespace std;


    template<class Impl>
FMT<Impl>::FMT(O3CPU *cpu_ptr, DerivO3CPUParams *params)
    : cpu(cpu_ptr),
    numThreads(params->numThreads)
{
    std::list<BranchEntry> entry;
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        table.push_back(entry);
    }

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        BranchEntry dummy;
        bzero((void *)&dummy, sizeof(BranchEntry));
        table[tid].push_back(dummy);

        DPRINTF(FMT, "Initiate branch entries for thread %d\n", tid);
        DPRINTF(FMT, "Size of table[%d] is %d\n", tid, table[tid].size());

        globalBase[tid] = 0;
        globalMiss[tid] = 0;
        globalWait[tid] = 0;
    }
}


    template<class Impl>
void FMT<Impl>::regStats()
{
    using namespace Stats;

    numBaseSlots
        .init(cpu->numThreads)
        .name(name() + ".numBaseSlots")
        .desc("Number of slots dispatching instruction")
        .flags(Stats::display)
        ;

    numMissSlots
        .init(cpu->numThreads)
        .name(name() + ".numMissSlots")
        .desc("Number of slots wasted because of stall")
        .flags(Stats::display)
        ;

    numWaitSlots
        .init(cpu->numThreads)
        .name(name() + ".numWaitSlots")
        .desc("Number of slots wasted because of waiting another thread")
        .flags(Stats::display)
        ;

    numOverlappedMisses
        .init(cpu->numThreads)
        .name(name() + ".numOverlappedMisses")
        .desc("Number of slots in stall but overlapped by another thread")
        .flags(Stats::display)
        ;

    fmtSize
        .init(cpu->numThreads)
        .name(name() + ".fmtSize")
        .desc("Number of branches in FMT")
        ;
}

    template<class Impl>
void FMT<Impl>::dumpStats()
{
    using namespace Stats;
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        numBaseSlots[tid] = globalBase[tid] + table[tid].begin()->baseSlots;
        numMissSlots[tid] = globalMiss[tid] + table[tid].begin()->missSlots;
        numWaitSlots[tid] = globalWait[tid] + table[tid].begin()->waitSlots;
        fmtSize[tid] = table[tid].size();
    }
}

    template<class Impl>
void FMT<Impl>::setStage(Fetch *_fetch, Decode *_decode, IEW *_iew)
{
    fetch = _fetch;
    decode = _decode;
    iew = _iew;
}

    template<class Impl>
void FMT<Impl>::addBranch(DynInstPtr &bran, ThreadID tid, uint64_t timeStamp)
{
    DPRINTF(FMT, "Adding %i\n", bran->seqNum);
    rBranchEntryIterator it = table[tid].rbegin();

    if (it->seqNum < bran->seqNum) {
        table[tid].emplace_back(BranchEntry{bran->seqNum, 0, 0, 0, timeStamp});
        return;
    }

    for (; it->seqNum > bran->seqNum; it++);
    assert(it->seqNum != bran->seqNum);

    //--: Get entry that bigger than the bran exactly
    it--;
    BranchEntryIterator it2 = it.base();

    table[tid].emplace(it2, BranchEntry{bran->seqNum, 0, 0, 0, timeStamp});

    DPRINTF(FMT, "Branches now in table[%d] is: ", tid);
    for (it2 = table[tid].begin(); it2 != table[tid].end(); it2++) {
        DPRINTFR(FMT, "%d, ", it2->seqNum);
    }
    DPRINTFR(FMT, "\n");
}

    template<class Impl>
void FMT<Impl>::incBaseSlot(DynInstPtr &inst, ThreadID tid)
{
    rBranchEntryIterator it = table[tid].rbegin();
    for (; it->seqNum > inst->seqNum; it++);
    it->baseSlots++;
}

    template<class Impl>
void FMT<Impl>::incWaitSlot(DynInstPtr &inst, ThreadID tid)
{
    rBranchEntryIterator it = table[tid].rbegin();
    for (; it->seqNum > inst->seqNum; it++);
    it->waitSlots++;
}

    template<class Impl>
void FMT<Impl>::incMissSlot(DynInstPtr &inst, ThreadID tid, bool Overlapped)
{
    rBranchEntryIterator it = table[tid].rbegin();
    for (; it->seqNum > inst->seqNum; it++);
    it->missSlots++;

    if (Overlapped) {
        numOverlappedMisses[tid]++;
    }
}

    template<class Impl>
void FMT<Impl>::resolveBranch(bool right, DynInstPtr &bran, ThreadID tid)
{
    DPRINTF(FMT, "Resolving %i\n", bran->seqNum);

    if (right) {
        BranchEntryIterator it = table[tid].begin();
        it++; // Do not delete the first one

        while (it != table[tid].end()) {
            globalBase[tid] += it->baseSlots;
            globalMiss[tid] += it->missSlots;
            globalWait[tid] += it->waitSlots;

            DPRINTF(FMT, "Commiting Inst: %i\n", it->seqNum);

            if (it->seqNum < bran->seqNum) {
                DPRINTF(FMT, "Erase branch: %i from FMT\n", it->seqNum);
                it = table[tid].erase(it);

            } else if (it->seqNum == bran->seqNum) {
                table[tid].erase(it);
                DPRINTF(FMT, "Erase latest branch: %i from FMT\n", it->seqNum);
                break;

            } else {
                // This instruction is Control but not added to FMT
                DPRINTF(FMT, "Committing control instruction which is not branch\n");
                break;
            }
        }
    } else {
        rBranchEntryIterator rit = table[tid].rbegin();
        for (; rit->seqNum > bran->seqNum; rit++);

        BranchEntryIterator it = std::next(rit).base();
        if (it->seqNum < bran->seqNum) {
            it++;
        }
        while (it != table[tid].end()) {
            globalMiss[tid] += it->baseSlots;
            globalMiss[tid] += it->missSlots;
            globalMiss[tid] += it->waitSlots;

            DPRINTF(FMT, "Squashing Inst: %i\n", it->seqNum);

            it = table[tid].erase(it);
        }
    }

    DPRINTF(FMT, "Branches now in table[%d] is: ", tid);
    for (BranchEntryIterator it2 = table[tid].begin(); it2 != table[tid].end(); it2++) {
        DPRINTFR(FMT, "%d, ", it2->seqNum);
    }
    DPRINTFR(FMT, "\n");
}

    template<class Impl>
void FMT<Impl>::incBaseSlot(ThreadID tid, int n)
{
    rBranchEntryIterator it = table[tid].rbegin();
    it->baseSlots += n;
}

    template<class Impl>
void FMT<Impl>::incMissSlot(ThreadID tid, int n, bool Overlapped)
{
    rBranchEntryIterator it = table[tid].rbegin();
    it->missSlots += n;
    if (Overlapped) {
        numOverlappedMisses[tid] += n;
    }
}

    template<class Impl>
void FMT<Impl>::incWaitSlot(ThreadID tid, int n)
{
    rBranchEntryIterator it = table[tid].rbegin();
    it->waitSlots += n;
}


#endif  // __CPU_O3_FMT_IMPL_HH__
