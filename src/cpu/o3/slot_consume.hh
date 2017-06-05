#ifndef __CPU_O3_SLOT_CONSUME_HH__
#define __CPU_O3_SLOT_CONSUME_HH__


#include <array>
#include <string>

#include "config/the_isa.hh"
#include "base/types.hh"
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
    NoState,
    Normal, // L1 hitting
    L1DCacheMiss,
    L2DCacheMiss
};

enum VQState {
    NoVQ,
    VQNotFull,
    VQFull
};


enum WayOfConsumeSlots {
    NotConsumed,
    EffectUsed, // effectively used: renamed instructions
    NoInstrSup, // no instruction supply
    NoROB, // no reorder buffer entries
    NoPR, // no physical register
    NoIQ, // no instruction queue entries
    NoLQ, // no load queue entries
    NoSQ, // no store queue entries
    IEWBlock,
    WaitingSI, // waiting serializing instructions
    DoingSquash,
    SquashedInst,
    OtherThreadsUsed
};


template<class Impl>
class SlotConsumer
{
  public:
    static const char* getConsumeString(int index) {
        static const char *consumeStrings[] = {
                "NotConsumed",
                "EffectUsed", // effectively used: renamed instructions
                "NoInstrSup", // no instruction supply
                "NoROB", // no reorder buffer entries
                "NoPR", // no physical register
                "NoIQ", // no instruction queue entries
                "NoLQ", // no load queue entries
                "NoSQ", // no store queue entries
                "IEWBlock",
                "WaitingSI", // waiting serializing instructions
                "DoingSquash",
                "SquashedInst",
                "OtherThreadsUsed",
        };
        return consumeStrings[index];
    }

    std::array<std::array<HeadInstrState, 4>, Impl::MaxThreads> queueHeadState;

    std::array<std::array<VQState, 4>, Impl::MaxThreads> vqState;

    // way of slots consumed by each thread
    std::array<std::array<WayOfConsumeSlots, Impl::MaxWidth>,
            Impl::MaxThreads> slotConsumption;

    std::array<int, Impl::MaxThreads> localSlotIndex;

    ThreadID another(ThreadID tid) {return LPT - tid;}

    SlotConsumer(DerivO3CPUParams *params, unsigned width,
                 std::string father_name);

    void cleanSlots();

    void consumeSlots(int numSlots, ThreadID who, WayOfConsumeSlots wocs);

    void addUpSlots();

    const unsigned stageWidth;

    const ThreadID numThreads;

    std::string consumerName;

    std::string name() const {
        return consumerName;
    };
};


#endif
