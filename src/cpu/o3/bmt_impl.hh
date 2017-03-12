#ifndef __CPU_O3_BMT_IMPL_HH__
#define __CPU_O3_BMT_IMPL_HH__


#include "cpu/o3/comm.hh"
#include "debug/BMT.hh"
#include "debug/LLM.hh"
#include "params/DerivO3CPU.hh"
#include "cpu/o3/bmt.hh"

#include <algorithm>

    template<class Impl>
    BMT<Impl>::BMT(O3CPU *cpu_ptr, DerivO3CPUParams *params)
: cpu(cpu_ptr),
    numThreads(params->numThreads),
    numROBEntries(params->numROBEntries)
{
}


/** addLL should check whether this Long-term Load is dependent on
 * an existing LL Miss. If there is, them retrun true, else false.
 */
    template<class Impl>
void BMT<Impl>::addLL(DynInstPtr &inst)
{
    uint64_t destVec = getDestRegs(inst);
    ThreadID tid = inst->threadNumber;
    table[tid].emplace_back(inst->seqNum, destVec, 0);
}

    template<class Impl>
bool BMT<Impl>::addInst(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;
    auto &&it = table[tid].begin();

    uint64_t srcVec = getSrcRegs(inst);
    uint64_t destVec = getDestRegs(inst);

    int numDep = 0;

    while (it != table[tid].end()) {
        if ((it->orbv & srcVec) == 0) {
            /** not dependent on. */
            it->orbv &= ~destVec;
            if (it->orbv == 0) {
                it = table[tid].erase(it);
                continue;
            }
        } else {
            numDep += 1;
            it->orbv &= destVec;
            it->dic++;
        }
        it++;
    }

    /** 自立门户 */
    if (numDep == 0 && inst->LLMiss() && inRange(inst->seqNum)) {
        addLL(inst);
        return false;
    }
    return true;
}

    template<class Impl>
void BMT<Impl>::merge(DynInstPtr &inst)
{
}


    template<class Impl>
uint64_t BMT<Impl>::getSrcRegs(DynInstPtr &inst)
{
    const StaticInstPtr& stInst = inst->staticInst;
    uint64_t vec = 0;
    for (int i = 0; i < stInst->numSrcRegs(); i++) {
        vec |= 1 << stInst->srcRegIdx(i);
    }
    return vec;
}

    template<class Impl>
uint64_t BMT<Impl>::getDestRegs(DynInstPtr &inst)
{
    const StaticInstPtr& stInst = inst->staticInst;
    uint64_t vec = 0;
    for (int i = 0; i < stInst->numDestRegs(); i++) {
        vec |= 1 << stInst->destRegIdx(i);
    }
    return vec;
}

    template<class Impl>
bool BMT<Impl>::isDep(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;
    auto &&it = table[tid].begin();

    uint64_t srcVec = getSrcRegs(inst);

    while (it != table[tid].end()) {
        if ((it->orbv & srcVec) != 0) {
            return true;
        }
        it++;
    }
    return false;
}


#endif  // __CPU_O3_BMT_IMPL_HH__
