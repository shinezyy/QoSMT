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

    int width;

    std::vector<int32_t> wait, miss;

    std::vector<std::vector<int32_t> > perCycleSlots;

    public:

    SlotCounter(DerivO3CPUParams *params, uint32_t _width);

    void assignSlots(ThreadID tid, DynInstPtr& inst);

    virtual std::string name() const = 0;

    void reshape(DynInstPtr& inst) {
        inst->incWaitSlot(-inst->getWaitSlot());
        inst->incMissSlot(-inst->getMissSlot());
    }

    enum SlotsUse {
        /** Doesn't have enough insts because of
         * front end miss (iTLB miss, icache miss, miss prediction)
         */
        InstMiss,

        /** Doesn't have enough insts because of instrutions of HPT
         * were blocked in earlier stage by LPT
         */
        EarlierWait,

        /**
         * Have insts to process , but other thread occupied some dispatchWidth
         */
        WidthWait,

        /**
         * Have insts to process , but other thread occupied some entries
         */
        EntryWait,

        /**
         * Have insts to process , but there's no enough entries because of itself (HPT)
         */
        EntryMiss,

        /**
         * Have insts to process , but there's no enough entries
         * partly because of itself (HPT), while also because of LPT
         * ComputeEntryMiss is because of HPT, ComputeEntryWait is because of LPT
         */
        ComputeEntryMiss,

        ComputeEntryWait,

        /** process insts*/
        Base,

        /**
         * Have insts to process, but later stage blocked this stage
         * LaterMiss: because of HPT itself; LaterWait: because of LPT
         */
        LaterMiss,

        LaterWait,

        NumUse
    };

    static const char* getSlotUseStr(int index) {
        static const char* slotUseStr[] = {
            "InstMiss",
            "EarlierWait",
            "WidthWait",
            "EntryWait",
            "EntryMiss",
            "ComputeEntryMiss",
            "ComputeEntryWait",
            "Base",
            "LaterMiss",
            "LaterWait",
        };
        return slotUseStr[index];
    }

    bool checkSlots(ThreadID tid);

    void sumLocalSlots(ThreadID tid);

    Stats::Scalar Slots[NumUse];

    Stats::Formula waitSlots;

    Stats::Formula missSlots;

    virtual void regStats();

    void incLocalSlots(ThreadID tid, SlotsUse su, int32_t num);
};

#endif // __CPU_O3_SLOTCOUNTER_HH__
