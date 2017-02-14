/*
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Nathan Binkert
 *          Steve Reinhardt
 *          Andrew Schultz
 */

#include <memory>
#include <string>
#include <vector>

#include "arch/alpha/faults.hh"
#include "arch/alpha/pagetable.hh"
#include "arch/alpha/tlb.hh"
#include "arch/generic/debugfaults.hh"
#include "base/inifile.hh"
#include "base/str.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/TLB.hh"
#include "sim/full_system.hh"

using namespace std;

namespace AlphaISA {

///////////////////////////////////////////////////////////////////////
//
//  Alpha TLB
//

#ifdef DEBUG
bool uncacheBit39 = false;
bool uncacheBit40 = false;
#endif

#define MODE2MASK(X) (1 << (X))

TLB::TLB(const Params *p)
    : BaseTLB(p), size(p->size)
{
    for (unsigned tid = 0; tid < MaxNumThreads;tid ++) {
        nlu[tid] = 0;

        table[tid] = new TlbEntry[size];
        memset(table[tid], 0, sizeof(TlbEntry) * size);
        flushCache();
    }
}

TLB::~TLB()
{
    for (unsigned tid = 0; tid < MaxNumThreads;tid ++) {
        if (table[tid])
            delete [] table[tid];
    }
}

void
TLB::regStats()
{
    fetch_hits
        .init(MaxNumThreads)
        .name(name() + ".fetch_hits")
        .desc("ITB hits");
    fetch_misses
        .init(MaxNumThreads)
        .name(name() + ".fetch_misses")
        .desc("ITB misses");
    fetch_acv
        .init(MaxNumThreads)
        .name(name() + ".fetch_acv")
        .desc("ITB acv");
    fetch_accesses
        .name(name() + ".fetch_accesses")
        .desc("ITB accesses");

    fetch_accesses = fetch_hits + fetch_misses;

    read_hits
        .init(MaxNumThreads)
        .name(name() + ".read_hits")
        .desc("DTB read hits")
        ;

    read_misses
        .init(MaxNumThreads)
        .name(name() + ".read_misses")
        .desc("DTB read misses")
        ;

    read_acv
        .init(MaxNumThreads)
        .name(name() + ".read_acv")
        .desc("DTB read access violations")
        ;

    read_accesses
        .init(MaxNumThreads)
        .name(name() + ".read_accesses")
        .desc("DTB read accesses")
        ;

    write_hits
        .init(MaxNumThreads)
        .name(name() + ".write_hits")
        .desc("DTB write hits")
        ;

    write_misses
        .init(MaxNumThreads)
        .name(name() + ".write_misses")
        .desc("DTB write misses")
        ;

    write_acv
        .init(MaxNumThreads)
        .name(name() + ".write_acv")
        .desc("DTB write access violations")
        ;

    write_accesses
        .init(MaxNumThreads)
        .name(name() + ".write_accesses")
        .desc("DTB write accesses")
        ;

    data_hits
        .name(name() + ".data_hits")
        .desc("DTB hits")
        ;

    data_misses
        .name(name() + ".data_misses")
        .desc("DTB misses")
        ;

    data_acv
        .name(name() + ".data_acv")
        .desc("DTB access violations")
        ;

    data_accesses
        .name(name() + ".data_accesses")
        .desc("DTB accesses")
        ;

    data_hits = read_hits + write_hits;
    data_misses = read_misses + write_misses;
    data_acv = read_acv + write_acv;
    data_accesses = read_accesses + write_accesses;
}

// look up an entry in the TLB
TlbEntry *
TLB::lookup(Addr vpn, uint8_t asn, unsigned tid)
{
    // assume not found...
    TlbEntry *retval = NULL;

    if (EntryCache[tid][0]) {
        if (vpn == EntryCache[tid][0]->tag &&
            (EntryCache[tid][0]->asma || EntryCache[tid][0]->asn == asn))
            retval = EntryCache[tid][0];
        else if (EntryCache[tid][1]) {
            if (vpn == EntryCache[tid][1]->tag &&
                (EntryCache[tid][1]->asma || EntryCache[tid][1]->asn == asn))
                retval = EntryCache[tid][1];
            else if (EntryCache[tid][2] && vpn == EntryCache[tid][2]->tag &&
                     (EntryCache[tid][2]->asma || EntryCache[tid][2]->asn == asn))
                retval = EntryCache[tid][2];
        }
    }

    if (retval == NULL) {
        PageTable::const_iterator i = lookupTable[tid].find(vpn);
        if (i != lookupTable[tid].end()) {
            while (i->first == vpn) {
                int index = i->second;
                TlbEntry *entry = &table[tid][index];
                assert(entry->valid);
                if (vpn == entry->tag && (entry->asma || entry->asn == asn)) {
                    retval = updateCache(entry, tid);
                    break;
                }

                ++i;
            }
        }
    }

    DPRINTF(TLB, "lookup %#x, asn %#x -> %s ppn %#x\n", vpn, (int)asn,
            retval ? "hit" : "miss", retval ? retval->ppn : 0);
    return retval;
}

Fault
TLB::checkCacheability(RequestPtr &req, bool itb)
{
    // in Alpha, cacheability is controlled by upper-level bits of the
    // physical address

    /*
     * We support having the uncacheable bit in either bit 39 or bit
     * 40.  The Turbolaser platform (and EV5) support having the bit
     * in 39, but Tsunami (which Linux assumes uses an EV6) generates
     * accesses with the bit in 40.  So we must check for both, but we
     * have debug flags to catch a weird case where both are used,
     * which shouldn't happen.
     */


    if (req->getPaddr() & PAddrUncachedBit43) {
        // IPR memory space not implemented
        if (PAddrIprSpace(req->getPaddr())) {
            return std::make_shared<UnimpFault>(
                "IPR memory space not implemented!");
        } else {
            // mark request as uncacheable
            req->setFlags(Request::UNCACHEABLE | Request::STRICT_ORDER);

            // Clear bits 42:35 of the physical address (10-2 in
            // Tsunami manual)
            req->setPaddr(req->getPaddr() & PAddrUncachedMask);
        }
        // We shouldn't be able to read from an uncachable address in Alpha as
        // we don't have a ROM and we don't want to try to fetch from a device 
        // register as we destroy any data that is clear-on-read. 
        if (req->isUncacheable() && itb)
            return std::make_shared<UnimpFault>(
                "CPU trying to fetch from uncached I/O");

    }
    return NoFault;
}


// insert a new TLB entry
void
TLB::insert(Addr addr, TlbEntry &entry, unsigned tid)
{
    flushCache();
    VAddr vaddr = addr;
    if (table[tid][nlu[tid]].valid) {
        Addr oldvpn = table[tid][nlu[tid]].tag;
        PageTable::iterator i = lookupTable[tid].find(oldvpn);

        if (i == lookupTable[tid].end())
            panic("TLB entry not found in lookupTable");

        int index;
        while ((index = i->second) != nlu[tid]) {
            if (table[tid][index].tag != oldvpn)
                panic("TLB entry not found in lookupTable");

            ++i;
        }

        DPRINTF(TLB, "remove @%d: %#x -> %#x\n", nlu[tid], oldvpn,
                table[tid][nlu[tid]].ppn);

        lookupTable[tid].erase(i);
    }

    DPRINTF(TLB, "insert @%d: %#x -> %#x\n", nlu[tid], vaddr.vpn(), entry.ppn);

    table[tid][nlu[tid]] = entry;
    table[tid][nlu[tid]].tag = vaddr.vpn();
    table[tid][nlu[tid]].valid = true;

    lookupTable[tid].insert(make_pair(vaddr.vpn(), nlu[tid]));
    nextnlu(tid);
}

void
TLB::flushAll()
{
    DPRINTF(TLB, "flushAll\n");
    for (unsigned tid = 0; tid < MaxNumThreads; tid++) {
        memset(table[tid], 0, sizeof(TlbEntry) * size);
        flushCache();
        lookupTable[tid].clear();
        nlu[tid] = 0;
    }
}

void
TLB::flushProcesses(unsigned tid)
{
    flushCache();
    PageTable::iterator i = lookupTable[tid].begin();
    PageTable::iterator end = lookupTable[tid].end();
    while (i != end) {
        int index = i->second;
        TlbEntry *entry = &table[tid][index];
        assert(entry->valid);

        // we can't increment i after we erase it, so save a copy and
        // increment it to get the next entry now
        PageTable::iterator cur = i;
        ++i;

        if (!entry->asma) {
            DPRINTF(TLB, "flush @%d: %#x -> %#x\n", index,
                    entry->tag, entry->ppn);
            entry->valid = false;
            lookupTable[tid].erase(cur);
        }
    }
}

void
TLB::flushAddr(Addr addr, uint8_t asn, unsigned tid)
{
    flushCache();
    VAddr vaddr = addr;

    PageTable::iterator i = lookupTable[tid].find(vaddr.vpn());
    if (i == lookupTable[tid].end())
        return;

    while (i != lookupTable[tid].end() && i->first == vaddr.vpn()) {
        int index = i->second;
        TlbEntry *entry = &table[tid][index];
        assert(entry->valid);

        if (vaddr.vpn() == entry->tag && (entry->asma || entry->asn == asn)) {
            DPRINTF(TLB, "flushaddr @%d: %#x -> %#x\n", index, vaddr.vpn(),
                    entry->ppn);

            // invalidate this entry
            entry->valid = false;

            lookupTable[tid].erase(i++);
        } else {
            ++i;
        }
    }
}


void
TLB::serialize(ostream &os)
{
    SERIALIZE_SCALAR(size);
    for (unsigned tid = 0; tid < MaxNumThreads; tid++) {
        SERIALIZE_SCALAR(nlu[tid]);

        for (int i = 0; i < size; i++) {
            nameOut(os, csprintf("%s.Entry%d", name(), i));
            table[tid][i].serialize(os);
        }
    }
}

void
TLB::unserialize(Checkpoint *cp, const string &section)
{
    UNSERIALIZE_SCALAR(size);
    for (unsigned tid = 0; tid < MaxNumThreads; tid++) {
        UNSERIALIZE_SCALAR(nlu[tid]);

        for (int i = 0; i < size; i++) {
            table[tid][i].unserialize(cp, csprintf("%s.Entry%d", section, i));
            if (table[tid][i].valid) {
                lookupTable[tid].insert(make_pair(table[tid][i].tag, i));
            }
        }
    }
}

Fault
TLB::translateInst(RequestPtr req, ThreadContext *tc)
{
    unsigned tid = req->threadId();
    //If this is a pal pc, then set PHYSICAL
    if (FullSystem && PcPAL(req->getPC()))
        req->setFlags(Request::PHYSICAL);

    if (PcPAL(req->getPC())) {
        // strip off PAL PC marker (lsb is 1)
        req->setPaddr((req->getVaddr() & ~3) & PAddrImplMask);
        fetch_hits[tid]++;
        return NoFault;
    }

    if (req->getFlags() & Request::PHYSICAL) {
        req->setPaddr(req->getVaddr());
    } else {
        // verify that this is a good virtual address
        if (!validVirtualAddress(req->getVaddr())) {
            fetch_acv[tid]++;
            return std::make_shared<ItbAcvFault>(req->getVaddr());
        }


        // VA<42:41> == 2, VA<39:13> maps directly to PA<39:13> for EV5
        // VA<47:41> == 0x7e, VA<40:13> maps directly to PA<40:13> for EV6
        if (VAddrSpaceEV6(req->getVaddr()) == 0x7e) {
            // only valid in kernel mode
            if (ICM_CM(tc->readMiscRegNoEffect(IPR_ICM)) !=
                mode_kernel) {
                fetch_acv[tid]++;
                return std::make_shared<ItbAcvFault>(req->getVaddr());
            }

            req->setPaddr(req->getVaddr() & PAddrImplMask);

            // sign extend the physical address properly
            if (req->getPaddr() & PAddrUncachedBit40)
                req->setPaddr(req->getPaddr() | ULL(0xf0000000000));
            else
                req->setPaddr(req->getPaddr() & ULL(0xffffffffff));
        } else {
            // not a physical address: need to look up pte
            int asn = DTB_ASN_ASN(tc->readMiscRegNoEffect(IPR_DTB_ASN));
            TlbEntry *entry = lookup(VAddr(req->getVaddr()).vpn(),
                              asn, tid);

            if (!entry) {
                fetch_misses[tid]++;
                return std::make_shared<ItbPageFault>(req->getVaddr());
            }

            req->setPaddr((entry->ppn << PageShift) +
                          (VAddr(req->getVaddr()).offset()
                           & ~3));

            // check permissions for this access
            if (!(entry->xre &
                  (1 << ICM_CM(tc->readMiscRegNoEffect(IPR_ICM))))) {
                // instruction access fault
                fetch_acv[tid]++;
                return std::make_shared<ItbAcvFault>(req->getVaddr());
            }

            fetch_hits[tid]++;
        }
    }

    // check that the physical address is ok (catch bad physical addresses)
    if (req->getPaddr() & ~PAddrImplMask) {
        return std::make_shared<MachineCheckFault>();
    }

    return checkCacheability(req, true);

}

Fault
TLB::translateData(RequestPtr req, ThreadContext *tc, bool write)
{
    unsigned tid = req->threadId();
    mode_type mode =
        (mode_type)DTB_CM_CM(tc->readMiscRegNoEffect(IPR_DTB_CM));

    /**
     * Check for alignment faults
     */
    if (req->getVaddr() & (req->getSize() - 1)) {
        DPRINTF(TLB, "Alignment Fault on %#x, size = %d\n", req->getVaddr(),
                req->getSize());
        uint64_t flags = write ? MM_STAT_WR_MASK : 0;
        return std::make_shared<DtbAlignmentFault>(req->getVaddr(),
                                                   req->getFlags(),
                                                   flags);
    }

    if (PcPAL(req->getPC())) {
        mode = (req->getFlags() & AlphaRequestFlags::ALTMODE) ?
            (mode_type)ALT_MODE_AM(
                tc->readMiscRegNoEffect(IPR_ALT_MODE))
            : mode_kernel;
    }

    if (req->getFlags() & Request::PHYSICAL) {
        req->setPaddr(req->getVaddr());
    } else {
        // verify that this is a good virtual address
        if (!validVirtualAddress(req->getVaddr())) {
            if (write) { write_acv[tid]++; } else { read_acv[tid]++; }
            uint64_t flags = (write ? MM_STAT_WR_MASK : 0) |
                MM_STAT_BAD_VA_MASK |
                MM_STAT_ACV_MASK;
            return std::make_shared<DtbPageFault>(req->getVaddr(),
                                                  req->getFlags(),
                                                  flags);
        }

        // Check for "superpage" mapping
        if (VAddrSpaceEV6(req->getVaddr()) == 0x7e) {
            // only valid in kernel mode
            if (DTB_CM_CM(tc->readMiscRegNoEffect(IPR_DTB_CM)) !=
                mode_kernel) {
                if (write) { write_acv[tid]++; } else { read_acv[tid]++; }
                uint64_t flags = ((write ? MM_STAT_WR_MASK : 0) |
                                  MM_STAT_ACV_MASK);

                return std::make_shared<DtbAcvFault>(req->getVaddr(),
                                                     req->getFlags(),
                                                     flags);
            }

            req->setPaddr(req->getVaddr() & PAddrImplMask);

            // sign extend the physical address properly
            if (req->getPaddr() & PAddrUncachedBit40)
                req->setPaddr(req->getPaddr() | ULL(0xf0000000000));
            else
                req->setPaddr(req->getPaddr() & ULL(0xffffffffff));
        } else {
            if (write)
                write_accesses[tid]++;
            else
                read_accesses[tid]++;

            int asn = DTB_ASN_ASN(tc->readMiscRegNoEffect(IPR_DTB_ASN));

            // not a physical address: need to look up pte
            TlbEntry *entry = lookup(VAddr(req->getVaddr()).vpn(), asn, tid);

            if (!entry) {
                // page fault
                if (write) { write_misses[tid]++; } else { read_misses[tid]++; }
                uint64_t flags = (write ? MM_STAT_WR_MASK : 0) |
                    MM_STAT_DTB_MISS_MASK;
                return (req->getFlags() & AlphaRequestFlags::VPTE) ?
                    (Fault)(std::make_shared<PDtbMissFault>(req->getVaddr(),
                                                            req->getFlags(),
                                                            flags)) :
                    (Fault)(std::make_shared<NDtbMissFault>(req->getVaddr(),
                                                            req->getFlags(),
                                                            flags));
            }

            req->setPaddr((entry->ppn << PageShift) +
                          VAddr(req->getVaddr()).offset());

            if (write) {
                if (!(entry->xwe & MODE2MASK(mode))) {
                    // declare the instruction access fault
                    write_acv[tid]++;
                    uint64_t flags = MM_STAT_WR_MASK |
                        MM_STAT_ACV_MASK |
                        (entry->fonw ? MM_STAT_FONW_MASK : 0);
                    return std::make_shared<DtbPageFault>(req->getVaddr(),
                                                          req->getFlags(),
                                                          flags);
                }
                if (entry->fonw) {
                    write_acv[tid]++;
                    uint64_t flags = MM_STAT_WR_MASK | MM_STAT_FONW_MASK;
                    return std::make_shared<DtbPageFault>(req->getVaddr(),
                                                          req->getFlags(),
                                                          flags);
                }
            } else {
                if (!(entry->xre & MODE2MASK(mode))) {
                    read_acv[tid]++;
                    uint64_t flags = MM_STAT_ACV_MASK |
                        (entry->fonr ? MM_STAT_FONR_MASK : 0);
                    return std::make_shared<DtbAcvFault>(req->getVaddr(),
                                                         req->getFlags(),
                                                         flags);
                }
                if (entry->fonr) {
                    read_acv[tid]++;
                    uint64_t flags = MM_STAT_FONR_MASK;
                    return std::make_shared<DtbPageFault>(req->getVaddr(),
                                                          req->getFlags(),
                                                          flags);
                }
            }
        }

        if (write)
            write_hits[tid]++;
        else
            read_hits[tid]++;
    }

    // check that the physical address is ok (catch bad physical addresses)
    if (req->getPaddr() & ~PAddrImplMask) {
        return std::make_shared<MachineCheckFault>();
    }

    return checkCacheability(req);
}

TlbEntry &
TLB::index(unsigned tid, bool advance)
{
    TlbEntry *entry = &table[tid][nlu[tid]];

    if (advance)
        nextnlu(tid);

    return *entry;
}

Fault
TLB::translateAtomic(RequestPtr req, ThreadContext *tc, Mode mode)
{
    if (mode == Execute)
        return translateInst(req, tc);
    else
        return translateData(req, tc, mode == Write);
}

void
TLB::translateTiming(RequestPtr req, ThreadContext *tc,
        Translation *translation, Mode mode)
{
    assert(translation);
    translation->finish(translateAtomic(req, tc, mode), req, tc, mode);
}

Fault
TLB::translateFunctional(RequestPtr req, ThreadContext *tc, Mode mode)
{
    panic("Not implemented\n");
    return NoFault;
}

Fault
TLB::finalizePhysical(RequestPtr req, ThreadContext *tc, Mode mode) const
{
    return NoFault;
}

} // namespace AlphaISA

AlphaISA::TLB *
AlphaTLBParams::create()
{
    return new AlphaISA::TLB(this);
}
