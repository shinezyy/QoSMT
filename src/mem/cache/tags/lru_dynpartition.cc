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
 * Definitions of a LRUDynPartition tag store.
 */

#include "debug/CacheRepl.hh"
#include "debug/DynCache.hh"
#include "mem/cache/tags/lru_dynpartition.hh"
#include "mem/cache/base.hh"


LRUDynPartition::LRUDynPartition(const Params *p)
        : BaseSetAssoc(p), cacheLevel(p->cache_level), isDCache(p->is_dcache)
{
    assert(numSets <= MAX_NUM_SETS);
    int rationFirst = p->thread_0_assoc;
    for (int i = 0; i < numSets; i++) {
        threadWayRation[i][0] = rationFirst;
        threadWayRation[i][1] = assoc - rationFirst;
        wayCount[i][0] = 0;
        wayCount[i][1] = 0;
        noInvalid[i][0] = false;
        noInvalid[i][1] = false;
    }

    for (int i = 0; i < numSets; i++) {
        for (int j = 0; j < assoc; j++) {
            sets[i].blks[j]->threadID = -1;
        }
    }
    if (cacheLevel == 2) {
        wayRationConfig = &controlPanel.l2CacheWayConfig;
    } else if (cacheLevel == 1) {
        if (isDCache) {
            wayRationConfig = &controlPanel.l1DCacheWayConfig;
        } else {
            wayRationConfig = &controlPanel.l1ICacheWayConfig;
        }
    }
    wayRationConfig->assoc = assoc;
}

CacheBlk*
LRUDynPartition::accessBlock(Addr addr, bool is_secure, Cycles &lat, int master_id)
{
    CacheBlk *blk = BaseSetAssoc::accessBlock(addr, is_secure, lat, master_id);
    //int set = extractSet(addr);
    //Addr tag = extractTag(addr);

   // for (int nWay = 0; nWay < assoc; nWay++) {
   //     auto blk = sets[set].blks[nWay];
   //     DPRINTF(CacheRepl, "The   tag list is  %x\n", blk->tag);
  //  }
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
LRUDynPartition::accessShadowTag(Addr addr)
{
    int set = extractSet(addr);
    Addr tag = extractTag(addr);
    DPRINTF(CacheRepl, "The  shadow tag should be  %x\n", tag);
    for (int nWay = 0; nWay < assoc; nWay++) {
        auto blk = sets[set].blks[nWay];
    DPRINTF(CacheRepl, "The  shadow tag list is  %x\n", blk->shadowtag);
    if (blk && blk->shadowtag == tag) {
       return blk;
       }
    }
    return NULL;
}

CacheBlk*
LRUDynPartition::findVictim(Addr addr)
{
    if (wayRationConfig->updatedByCore) {
        if (wayRationConfig->threadWayRations[0] +
               wayRationConfig->threadWayRations[1] != assoc) {
            DPRINTF(DynCache, "way ration[0]: %i, way ration[1]: %i\n",
                    wayRationConfig->threadWayRations[0],
                    wayRationConfig->threadWayRations[1]);
            panic("Associativity exceeds\n");
        }
        for (int i = 0; i < numSets; i++) {
            threadWayRation[i][0] = wayRationConfig->threadWayRations[0];
            threadWayRation[i][1] = wayRationConfig->threadWayRations[1];
        }
        wayRationConfig->updatedByCore = false;
    }

    assert(curThreadID >= 0);
    int set = extractSet(addr);
    //Addr tag = extractTag(addr); // prepare to store the shadow tag
    // grab a replacement candidate
    BlkType *blk = NULL;

    if ((threadWayRation[set][curThreadID] > wayCount[set][curThreadID])
        && (!noInvalid[set][curThreadID])) {  // find invlaid ones
        for (int i = assoc - 1; i >= 0; i--) {
            blk = sets[set].blks[i];
            if (blk->threadID == -1) {
                break;
            }
        }

        // Consume ration in invalid cases.
        // This has pityfalls when performing cache coherence.
        // But we don't care that now.
        wayCount[set][curThreadID]++;
        assert(blk);
        blk->threadID = (ThreadID) curThreadID;
        if (threadWayRation[set][curThreadID] == wayCount[set][curThreadID]) {
            noInvalid[set][curThreadID] = true;
        }
       // if (curThreadID == 0) {
       //     blk->shadowtag = tag;
       // }
    } else if ((threadWayRation[set][curThreadID] > wayCount[set][curThreadID])
             && (noInvalid[set][curThreadID])) { // find victim after new allocation
        for (int i = assoc - 1; i >= 0; i--) {
            blk = sets[set].blks[i];
            if (blk->threadID != curThreadID) break;
        }
        assert(blk);
        blk->threadID = (ThreadID) curThreadID;
        wayCount[set][curThreadID]++;
        wayCount[set][1-curThreadID]--;
        assert(wayCount[set][curThreadID] <= assoc);
        assert(wayCount[set][curThreadID] >= 0);
        assert(wayCount[set][1 - curThreadID] <= assoc);
        assert(wayCount[set][1 - curThreadID] >= 0);
        //if (curThreadID == 0) {  // update shadowtag when HP thread evict one way
       //     blk->shadowtag = tag;
       // }
    } else {  // find last used line inside its own ways.
        for (int i = assoc - 1; i >= 0; i--) {
            blk = sets[set].blks[i];
            if (blk->threadID == curThreadID) {
                break;
            } else {
                DPRINTF(DynCache, "block %i belongs to T[%i]\n", i, blk->threadID);
            }
        }
        if (blk->threadID != curThreadID) {
            DPRINTF(DynCache, "No block found for T[%i]\n, T[0] assoc: %i, "
                    "T[1] assoc: %i]\n", curThreadID,
                    wayRationConfig->threadWayRations[0],
                    wayRationConfig->threadWayRations[1]
            );
        }
  //      DPRINTF(CacheRepl, "set %x: selecting blk %x for replacement\n",
 //               set, regenerateBlkAddr(blk->tag, set));
//        if (curThreadID == 0) {  // update shadowtag when HP thread evict one way
//            blk->shadowtag = tag;
//        }
    }
    assert(threadWayRation[set][curThreadID] >= 0);
    return blk;
}

void
LRUDynPartition::insertBlock(PacketPtr pkt, BlkType *blk)
{
    // insertBlock is called in the specific thread context.
    // so we can determine that this block belongs to thread
    // <curThreadID>
    BaseSetAssoc::insertBlock(pkt, blk);
    assert(pkt->getAddr());
    uint64_t tag = extractTag(pkt->getAddr());
    if (curThreadID == 0) {  // update shadowtag when HP thread evict one way
	    blk->shadowtag = tag ;
    }
    assert(blk->threadID >= 0);
    int set = extractSet(pkt->getAddr());
    sets[set].moveToHead(blk);
}

void
LRUDynPartition::invalidate(CacheBlk *blk)
{
    BaseSetAssoc::invalidate(blk);

    // should be evicted before valid blocks
    int set = blk->set;
    sets[set].moveToTail(blk);
    noInvalid[set][curThreadID] = false;
    // Reset block belonging status
    assert(blk->threadID >= 0);
    wayCount[set][blk->threadID]--;
    blk->threadID = -1;
}
void
LRUDynPartition::wayRealloc(ThreadID tid, int wayNum)
{
    for (int i = 0; i < numSets; i++) {
        threadWayRation[i][tid] = wayNum ;
        threadWayRation[i][1-tid] = assoc - wayNum ;
    }
}
LRUDynPartition*
LRUDynPartitionParams::create()
{
    return new LRUDynPartition(this);
}
