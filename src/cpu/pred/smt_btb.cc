/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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
 * Authors: Kevin Lim
 */

#include "base/intmath.hh"
#include "base/trace.hh"
#include "cpu/pred/smt_btb.hh"
#include "debug/Fetch.hh"

SMTBTB::SMTBTB(unsigned _numEntries,
                       unsigned _tagBits,
                       unsigned _instShiftAmt,
                       ThreadID _numThreads)
    : numThreads(_numThreads),
      btb(_numThreads),
      numEntries(_numEntries),
      tagBits(_tagBits),
      instShiftAmt(_instShiftAmt)
{
    DPRINTF(Fetch, "BTB: Creating BTB object.\n");

    if (!isPowerOf2(numEntries)) {
        fatal("BTB entries is not a power of 2!");
    }

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        btb[tid].resize(numEntries);

        for (unsigned i = 0; i < numEntries; ++i) {
            btb[tid][i].valid = false;
        }
    }

    idxMask = numEntries - 1;

    tagMask = (1 << tagBits) - 1;

    tagShiftAmt = instShiftAmt + floorLog2(numEntries);
}

void
SMTBTB::reset()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        for (unsigned i = 0; i < numEntries; ++i) {
            btb[tid][i].valid = false;
        }
    }
}

inline
unsigned
SMTBTB::getIndex(Addr instPC, ThreadID tid)
{
    // Need to shift PC over by the word offset.
    return ((instPC >> instShiftAmt)) & idxMask;
}

inline
Addr
SMTBTB::getTag(Addr instPC, ThreadID tid)
{
    return ((instPC >> tagShiftAmt)) & tagMask;
}

bool
SMTBTB::valid(Addr instPC, ThreadID tid)
{
    unsigned btb_idx = getIndex(instPC, tid);

    Addr inst_tag = getTag(instPC, tid);

    assert(btb_idx < numEntries);

    if (btb[tid][btb_idx].valid
        && inst_tag == btb[tid][btb_idx].tag) {
        return true;
    } else {
        return false;
    }
}

// @todo Create some sort of return struct that has both whether or not the
// address is valid, and also the address.  For now will just use addr = 0 to
// represent invalid entry.
TheISA::PCState
SMTBTB::lookup(Addr instPC, ThreadID tid)
{
    unsigned btb_idx = getIndex(instPC, tid);

    Addr inst_tag = getTag(instPC, tid);

    assert(btb_idx < numEntries);

    if (btb[tid][btb_idx].valid
        && inst_tag == btb[tid][btb_idx].tag) {
        return btb[tid][btb_idx].target;
    } else {
        return 0;
    }
}

void
SMTBTB::update(Addr instPC, const TheISA::PCState &target, ThreadID tid)
{
    unsigned btb_idx = getIndex(instPC, tid);

    assert(btb_idx < numEntries);

    btb[tid][btb_idx].valid = true;
    btb[tid][btb_idx].target = target;
    btb[tid][btb_idx].tag = getTag(instPC, tid);
}
