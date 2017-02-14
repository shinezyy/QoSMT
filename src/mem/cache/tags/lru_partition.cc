/*
 * Copyright (c) 2012-2013 ARM Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2003-2005,2014 The Regents of The University of Michigan
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
 * Authors: Erik Hallnor
 */

/**
 * @file
 * Definitions of a LRUPartition tag store.
 */

#include "debug/CacheRepl.hh"
#include "mem/cache/tags/lru_partition.hh"
#include "mem/cache/base.hh"


LRUPartition::LRUPartition(const Params *p)
    : BaseSetAssoc(p)
{
    assert(numSets <= MAX_NUM_SETS);
    int rationFirst = p->thread_0_assoc;
    for (int i = 0; i < numSets; i++) {
        threadWayRation[i][0] = rationFirst;
        threadWayRation[i][1] = assoc - rationFirst;
    }

    for (int i = 0; i < numSets; i++) {
        for (int j = 0; j < assoc; j++) {
            sets[i].blks[j]->threadID = -1;
        }
    }
}

CacheBlk*
LRUPartition::accessBlock(Addr addr, bool is_secure, Cycles &lat, int master_id)
{
    CacheBlk *blk = BaseSetAssoc::accessBlock(addr, is_secure, lat, master_id);

    if (blk != NULL) {
        // move this block to head of the MRU list
        sets[blk->set].moveToHead(blk);
        // Check the coherence of threadID assigned when inserted.
        // ??? The different thread can hit on the same line!?
        // assert(curThreadID == blk->threadID);
        assert(blk->threadID >= 0);  // invalid ones with -1
        DPRINTF(CacheRepl, "set %x: moving blk %x (%s) to MRU\n",
                blk->set, regenerateBlkAddr(blk->tag, blk->set),
                is_secure ? "s" : "ns");
#ifdef DEBUG_CACHE_PARTITION
        printf("set %03d ", blk->set);
        for (int i = 0; i < assoc; i++) {
            printf("%d ", sets[blk->set].blks[i]->threadID);
        }
        puts("");
#endif
    }

    return blk;
}

CacheBlk*
LRUPartition::findVictim(Addr addr)
{
    assert(curThreadID >= 0);
    int set = extractSet(addr);
    // grab a replacement candidate
    BlkType *blk = NULL;
    if (threadWayRation[set][curThreadID] > 0) {  // find invlaid ones
        int i;
        for (i = assoc - 1; i >= 0; i--) {
            blk = sets[set].blks[i];
            if (blk->threadID == -1) break;
        }
        // Consume ration in invalid cases.
        // This has pityfalls when performing cache coherence.
        // But we don't care that now.
        assert(i >= 0);
        threadWayRation[set][curThreadID]--;
        blk->threadID = curThreadID;
    }
    else {  // find last used line inside its own ways.
        for (int i = assoc - 1; i >= 0; i--) {
            blk = sets[set].blks[i];
            if (blk->threadID == curThreadID) break;
        }
        assert(blk->threadID == curThreadID);
        DPRINTF(CacheRepl, "set %x: selecting blk %x for replacement\n",
                set, regenerateBlkAddr(blk->tag, set));
    }

    assert(threadWayRation[set][curThreadID] >= 0);

    return blk;
}

void
LRUPartition::insertBlock(PacketPtr pkt, BlkType *blk)
{
    // insertBlock is called in the specific thread context.
    // so we can determine that this block belongs to thread
    // <curThreadID>
    BaseSetAssoc::insertBlock(pkt, blk);

    assert(blk->threadID >= 0);
    int set = extractSet(pkt->getAddr());
    sets[set].moveToHead(blk);
}

void
LRUPartition::invalidate(CacheBlk *blk)
{
    BaseSetAssoc::invalidate(blk);

    // should be evicted before valid blocks
    int set = blk->set;
    sets[set].moveToTail(blk);

    // Reset block belonging status
    assert(blk->threadID >= 0);
    threadWayRation[set][blk->threadID]++;
    blk->threadID = -1;
}

LRUPartition*
LRUPartitionParams::create()
{
    return new LRUPartition(this);
}
