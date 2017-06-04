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
    NotInitiated,
    NotUsed,
    /** Doesn't have enough insts because of
     * front end miss (iTLB miss, icache miss, miss prediction)
     */
    InstSupMiss,
    /** Doesn't have enough insts because of instrutions of HPT
     * were blocked in earlier stage by LPT
     */
    InstSupWait,
    ICacheInterference,
    FetchSliceWait,
    // Have insts to process , but other thread occupied some dispatchWidth
    WidthWait,
    // Have insts to process , but other thread occupied some entries
    EntryWait,
    // Have insts to process , but there's no enough entries because of itself (HPT)
    EntryMiss,
    // process insts
    Base,
    /**Have insts to process, but later stage blocked this stage
     * LaterMiss: because of HPT itself; LaterWait: because of LPT
     */
    LaterMiss,
    LaterWait,
    /**In Unblocking status, if HPT, last cycle, was blocked by LPT,
     * then insts were brokend down into two chunks, which leads to
     * miss slots in the 2nd chunk, and should be rectified.
     */
    LBLCWait,
    SerializeMiss,
    SquashMiss,
    NotFullInstSupMiss,
    Referenced,
    SplitWait,
    NumUse
};

template <class Impl>
class SlotCounter
{
    static const char* getSlotUseStr(int index) {
        static const char* slotUseStr[] = {
                "NotInitiated",
                "NotUsed",
                "InstSupMiss",
                "InstSupWait",
                "ICacheInterference",
                "FetchSliceWait",
                "WidthWait",
                "EntryWait",
                "EntryMiss",
                "Base",
                "LaterMiss",
                "LaterWait",
                "LBLCWait",
                "SerializeMiss",
                "SquashMiss",
                "NotFullInstSupMiss",
                "Referenced",
                "SplitWait",
        };
        return slotUseStr[index];
    }

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



    bool checkSlots(ThreadID tid);

    void sumLocalSlots(ThreadID tid);

    Stats::Scalar slots[NumUse];

    Stats::Formula waitSlots;

    Stats::Formula missSlots;

    Stats::Formula baseSlots;

    virtual void regStats();

    void incLocalSlots(ThreadID tid, SlotsUse su, int32_t num);

    void incLocalSlots(ThreadID tid, SlotsUse su, int32_t num, bool verbose);

    ThreadID another(ThreadID tid) {return LPT - tid;}

    std::array<std::array<SlotsUse, Impl::MaxWidth>,
            Impl::MaxThreads> slotUseRow;

    std::array<int, Impl::MaxThreads> slotIndex;
};

#endif // __CPU_O3_SLOTCOUNTER_HH__
