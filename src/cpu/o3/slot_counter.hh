#ifndef __CPU_O3_SLOTCOUNTER_HH__
#define __CPU_O3_SLOTCOUNTER_HH__


#include <cstdint>
#include <array>
#include <string>

#include "base/statistics.hh"
#include "config/the_isa.hh"
#include "cpu/inst_seq.hh"
#include "debug/SlotCounter.hh"

struct DerivO3CPUParams;

extern ThreadID HPT, LPT;
extern const char* slotUseStr[];

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
    L1ICacheInterference,
    L2ICacheInterference,
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
    ROBWait,
    ROBMiss,
    IQWait,
    IQMiss,
    LQWait,
    LQMiss,
    SQWait,
    SQMiss,
    SplitMiss,
    L1DCacheInterference,
    L2DCacheInterference,
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



    bool checkSlots(ThreadID tid);

    void sumLocalSlots(ThreadID tid);

    uint64_t slots[NumUse];

    Stats::Scalar slotsStat[NumUse];

    Stats::Scalar waitSlots;

    Stats::Scalar missSlots;

    Stats::Formula baseSlots;

    virtual void regStats();

    void incLocalSlots(ThreadID tid, SlotsUse su, int32_t num);

    void incLocalSlots(ThreadID tid, SlotsUse su, int32_t num, bool verbose);

    ThreadID another(ThreadID tid) {return LPT - tid;}

    std::array<std::array<SlotsUse, Impl::MaxWidth>,
            Impl::MaxThreads> slotUseRow;

    std::array<int, Impl::MaxThreads> slotIndex;

    int countSlot(ThreadID tid, SlotsUse su) {
        return perCycleSlots[tid][su];
    }

    void printSlotRow(std::array<SlotsUse, Impl::MaxWidth> row, int width);

    virtual void dumpStats();

    std::array<int, Impl::MaxThreads> curCycleBase, curCycleWait, curCycleMiss;
};

#endif // __CPU_O3_SLOTCOUNTER_HH__
