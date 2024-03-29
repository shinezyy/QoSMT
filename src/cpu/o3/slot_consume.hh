#ifndef __CPU_O3_SLOT_CONSUME_HH__
#define __CPU_O3_SLOT_CONSUME_HH__


#include <array>
#include <string>

#include "config/the_isa.hh"
#include "base/types.hh"
#include "base/statistics.hh"
#include "cpu/o3/slot_counter.hh"

struct DerivO3CPUParams;

enum NoInstrReason {
    NoReason,
    IntrinsicICacheMiss,
    ExtrinsicICacheMiss,
    BranchMissPrediction,
    Fetching,
    AnotherThreadFetch
};

enum HeadInstrState {
    NoState = 0,
    Normal, // L1 hitting
    L1DCacheMiss,
    L2DCacheMiss,
    L1DCacheWait,
    L2DCacheWait,
    WaitingAddress,
};

enum VQState {
    NoVQ = 0,
    VQNotFull,
    VQFull
};


template<class Impl>
class SlotConsumer
{
  public:
    enum FullSource {
        ROB = 0,
        IQ,
        LQ,
        SQ,
        IEWStage,
        Register,
        CommitStage,
        SI,
        NONE,
    };

    std::array<std::array<HeadInstrState, 4>, Impl::MaxThreads> queueHeadState;

    std::array<std::array<VQState, 4>, Impl::MaxThreads> vqState;

    // way of slots consumed by each thread
    std::array<std::array<SlotsUse, Impl::MaxWidth>,
            Impl::MaxThreads> slotConsumption;

    std::array<int, Impl::MaxThreads> localSlotIndex;

    ThreadID another(ThreadID tid) {return LPT - tid;}

    SlotConsumer(DerivO3CPUParams *params, unsigned width,
                 std::string father_name);

    void cleanSlots();

    void consumeSlots(int numSlots, ThreadID who, SlotsUse su);

    void addUpSlots();

    const unsigned stageWidth;

    const ThreadID numThreads;

    std::string consumerName;

    std::string name() const {
        return consumerName;
    };

    void cycleStart() {
        cleanSlots();
    }

    void cycleEnd(
            ThreadID tid,
            std::array<unsigned, Impl::MaxThreads> &toNextStageNum,
            FullSource fullSource,
            std::array<SlotsUse, Impl::MaxWidth> &curCycleRow,
            std::queue<std::array<SlotsUse, Impl::MaxWidth> > &skidSlotBuffer,
            SlotCounter<Impl> *slotCounter,
            bool isRename, bool BLB_in, bool SI, bool finishSS,
            bool siTail, bool siTailNext, bool &BLB_out
    );

    std::array<bool, Impl::MaxThreads> ROBHeadMissCache;

    Stats::Vector ROBWait_HeadNotMiss;
    Stats::Vector ROBWait_VQNotFull;

    void regStats();

  private:

    std::array<int, Impl::MaxThreads> blockCounter;

    const bool considerHeadStatus;
};


#endif
