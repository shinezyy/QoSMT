#ifndef __CPU_O3_FMT_HH__
#define __CPU_O3_FMT_HH__


#include <cstdint>
#include <list>
#include <vector>

#include "base/statistics.hh"
#include "config/the_isa.hh"
#include "cpu/inst_seq.hh"

struct DerivO3CPUParams;

struct BranchEntry {
    InstSeqNum seqNum;

    uint64_t baseSlots;

    uint64_t waitSlots;

    uint64_t missSlots;

    uint64_t initTimeStamp;
};


template <class Impl>
class FMT {

    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::O3CPU O3CPU;
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename CPUPol::Fetch Fetch;
    typedef typename CPUPol::Decode Decode;
    typedef typename CPUPol::IEW IEW;


    public:

    typedef typename std::list<BranchEntry>
        ::reverse_iterator rBranchEntryIterator;

    typedef typename std::list<BranchEntry>
        ::iterator BranchEntryIterator;

    private:

    O3CPU *cpu;

    Fetch *fetch;

    Decode *decode;

    IEW *iew;

    std::vector<std::list<BranchEntry> > table;

    public:

    uint64_t globalBase[Impl::MaxThreads];

    uint64_t globalMiss[Impl::MaxThreads];

    uint64_t globalWait[Impl::MaxThreads];

    private:

    ThreadID numThreads;

    Stats::Vector numBaseSlots;

    Stats::Vector numWaitSlots;

    Stats::Vector numMissSlots;

    Stats::Vector numOverlappedMisses;

    Stats::Vector fmtSize;

    /** slots converted to miss because of branch miss prediction. */
    Stats::Vector waitToMiss;

    Stats::Vector baseToMiss;

    public:

    std::string name() const
    {
        return cpu->name() + ".fmt";
    }

    void regStats();

    FMT(O3CPU *cpu_ptr, DerivO3CPUParams *params);

    // Add the first instruction of each thread into the table
    // void init(std::vector<DynInstPtr> &v_bran, uint64_t timeStamp);


    void setStage(Fetch *_fetch, Decode *_decode, IEW *_iew);

    void addBranch(DynInstPtr &bran, ThreadID tid, uint64_t timeStamp);

    /* If prediction is right:
     * add timestamp difference counts to global dispatching count;
     * else:
     * add timestamp difference to global branch misprediction count,
     * and count slots after the branch instruction as miss event slots.
     */
    void resolveBranch(bool right, DynInstPtr &bran, ThreadID tid);


    void incBaseSlot(DynInstPtr &inst, ThreadID tid, int n);

    //Because miss is always deterministic, so it is not needed
    //void incMissSlot(DynInstPtr &bran, ThreadID tid, int n, bool Overlapped);

    void incWaitSlot(DynInstPtr &inst, ThreadID tid, int n);


    /* The following functions are used to add slots to global counter
     * directly, which should be deterministic
     */
    void incMissDirect(ThreadID tid, int n, bool Overlapped);

    /* incWaitDirect是不准确的，它的存在是为了解决：
     * 当T处于wait时，不知道它的下一条指令应该属于哪一个branch
     * 姑且认为它属于最新的branch（一般是这样，但是可能引入误差）
     */
    void incWaitDirect(ThreadID tid, int n);

    void dumpStats();

    uint64_t getHptWait() { return table[0].begin()->waitSlots; }

    uint64_t getHptNonWait() { return table[0].begin()->baseSlots +
        table[0].begin()->missSlots; }
};

#endif // __CPU_O3_FMT_HH__
