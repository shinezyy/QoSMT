#ifndef __CPU_O3_BMT_HH__
#define __CPU_O3_BMT_HH__


#include <list>
#include <cinttypes>
#include <cpu/inst_seq.hh>

#include "config/the_isa.hh"

struct DerivO3CPUParams;

template <class Impl>
class BMT {

    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::O3CPU O3CPU;
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;
    //typedef typename Impl::StaticInstPtr StaticInstPtr;
    typedef typename CPUPol::Fetch Fetch;
    typedef typename CPUPol::Decode Decode;
    typedef typename CPUPol::IEW IEW;
    typedef typename TheISA::RegIndex RegIndex;


    public:

    struct BME { // Backend Miss Entry

        // Long-latency Load ID
        InstSeqNum llid;

        // Output Register Bit Vector
        uint64_t orbv;

        // dependent instruction counter
        int dic;

        // Pointer to the Long-latency Load
        // DynInstPtr &dlp;

        BME(InstSeqNum _llid, uint64_t _orbv, int _dic)
            : llid(std::move(_llid)), orbv(std::move(_orbv)), dic(std::move(_dic))
        {}

    };


    private:

    O3CPU *cpu;

    std::list<BME> table[Impl::MaxThreads];

    ThreadID numThreads;

    int numROBEntries;


    public:

    std::string name() const
    {
        return cpu->name() + ".bmt";
    }

    BMT(O3CPU *cpu_ptr, DerivO3CPUParams *params);

    // allocate a BME for the loading inst, and initiate the entry
    void addLL(DynInstPtr &inst);

    // if inst is depencent on an existing tree, return true, else return false
    bool addInst(DynInstPtr &inst);

    void merge(DynInstPtr &inst);

    int count1(uint64_t x)
    {
        return __builtin_popcount(x);
    }

    uint64_t getSrcRegs(DynInstPtr &inst);

    uint64_t getDestRegs(DynInstPtr &inst);

    void clear(ThreadID tid)
    {
        table[tid].clear();
    }

    bool isDep(DynInstPtr &inst);

    private:

    InstSeqNum start, end;

    public:

    void setRange(InstSeqNum s, InstSeqNum e)
    {
        start = s;
        end = e;
    }

    void inRange(InstSeqNum seq)
    {
        return seq >= start && seq <= end;
    }
};

#endif // __CPU_O3_BMT_HH__
