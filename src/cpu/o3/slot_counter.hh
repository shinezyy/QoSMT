#ifndef __CPU_O3_SLOTCOUNTER_HH__
#define __CPU_O3_SLOTCOUNTER_HH__


#include <cstdint>
#include <vector>
#include <string>

#include "base/statistics.hh"
#include "config/the_isa.hh"
#include "cpu/inst_seq.hh"

struct DerivO3CPUParams;

template <class Impl>
class SlotCounter
{

    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::O3CPU O3CPU;
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;

    protected:

    std::vector<int32_t> wait, miss;

    public:

    SlotCounter(DerivO3CPUParams *params);

    void sumLocalSlots(ThreadID tid, bool isWait, int32_t num);

    void assignSlots(ThreadID tid, DynInstPtr& inst);

    virtual std::string name() const = 0;

    void reshape(DynInstPtr& inst) {
        inst->incWaitSlot(-inst->getWaitSlot());
        inst->incMissSlot(-inst->getMissSlot());
    }
};

#endif // __CPU_O3_SLOTCOUNTER_HH__
