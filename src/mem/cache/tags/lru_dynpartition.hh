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
 * Declaration of a LRUDynPartition tag store.
 * The LRUDynPartition tags guarantee that the true least-recently-used way in
 * a set will always be evicted.
 */

#ifndef __MEM_CACHE_TAGS_LRUDynPartition_HH__
#define __MEM_CACHE_TAGS_LRUDynPartition_HH__

#include "mem/cache/tags/base_set_assoc.hh"
#include "mem/cache/tags/shadow_lru_tag.hh"
#include "mem/cache/tags/control_panel.hh"
#include "params/LRUDynPartition.hh"

class LRUDynPartition : public BaseSetAssoc
{
  public:
    /** Convenience typedef. */
    typedef LRUDynPartitionParams Params;

    /**
     * Construct and initialize this tag store.
     */
    explicit LRUDynPartition(const Params *p);

    /**
     * Destructor
     */
    ~LRUDynPartition() override = default;

    CacheBlk* accessBlock(Addr addr, bool is_secure, Cycles &lat,
                         int context_src) override;

    bool accessShadowTag(Addr addr) override;

    CacheBlk* findVictim(Addr addr) override;

    void insertBlock(PacketPtr pkt, BlkType *blk) override;

    void invalidate(CacheBlk *blk) override;

    void wayRealloc(ThreadID tid, int wayNum);

    void setThread(ThreadID tid) override {
        curThreadID = tid;
    }

    void clearThread() override {
        curThreadID = -1;
    }

  private:
    /**
     * Determine the upper bound ways for each thread
     * the max numSets is 128 (to simplify implementation).
     */
#define MAX_NUM_SETS 4096
#define MaxThreads 2
    int threadWayRation[MAX_NUM_SETS][MaxThreads];
    int wayCount[MAX_NUM_SETS][MaxThreads];

    ThreadID curThreadID;
#undef MaxThreads

    const int cacheLevel;
    const bool isDCache;

    WayRationConfig *wayRationConfig;

    ShadowLRUTag shadowLRUTag;

    void checkWayRationUpdate();

    void get3PossibleVictim(BlkType* &invalidVictim, BlkType* &selfVictim,
                       BlkType* &otherVictim, ThreadID tid, int setIndex);
};

#endif // __MEM_CACHE_TAGS_LRUDynPartition_HH__
