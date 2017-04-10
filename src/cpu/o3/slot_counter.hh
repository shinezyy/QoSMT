#ifndef __CPU_O3_SLOTCOUNTER_HH__
#define __CPU_O3_SLOTCOUNTER_HH__


#include <cstdint>
#include <array>
#include <string>

#include "base/statistics.hh"
#include "config/the_isa.hh"
#include "cpu/inst_seq.hh"

struct DerivO3CPUParams;

extern ThreadID HPT, LPT;


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

    /**
     * In Unblocking status, if HPT, last cycle, was blocked by LPT,
     * then insts were brokend down into two chunks, which leads to
     * miss slots in the 2nd chunk, and should be rectified.
     */
    LBLCWait,

    SerializeMiss,

    NumUse
};

template <class Impl>
class SlotCounter
{

    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::O3CPU O3CPU;
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;

    protected:

    int width;

    ThreadID numThreads;

    std::array<int32_t, Impl::MaxThreads> wait, miss;

    std::array<std::array<int32_t, NumUse>, Impl::MaxThreads> perCycleSlots;

    public:

    SlotCounter(DerivO3CPUParams *params, uint32_t _width);

    void assignSlots(ThreadID tid, DynInstPtr& inst);

    virtual std::string name() const = 0;

    void reshape(DynInstPtr& inst) {
        inst->incWaitSlot(-inst->getWaitSlot());
        inst->incMissSlot(-inst->getMissSlot());
    }


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
            "LBLCWait",
            "SerializeMiss",
        };
        return slotUseStr[index];
    }

    bool checkSlots(ThreadID tid);

    void sumLocalSlots(ThreadID tid);

    Stats::Scalar slots[NumUse];

    Stats::Formula waitSlots;

    Stats::Formula missSlots;

    Stats::Formula baseSlots;

    virtual void regStats();

    void incLocalSlots(ThreadID tid, SlotsUse su, int32_t num);

    void incLocalSlots(ThreadID tid, SlotsUse su, int32_t num, bool verbose);
};

#endif // __CPU_O3_SLOTCOUNTER_HH__
