#include "mem/cache/miss_table.hh"
#include "debug/missTry.hh"


bool MissTables::isSpecifiedMiss(Addr address, bool isDCache, MissDescriptor &md) {
    MissTable *l1_table = isDCache ? &l1DMissTable : &l1IMissTable;
    MissTable *l2_table = &l2MissTable;
    MissTable::iterator l1_it, l2_it;

    address = blockAlign(address);
    // DPRINTF(missTry, "Address to look up is 0x%x\n", address);

    l1_it = l1_table->find(address);
    if (l1_it == l1_table->end()) {
        // missTables.printMiss(*l1_table);
        md.valid = false;
        return false;
    } else {
        md.valid = true;
    }
    // found in L1 table
    l2_it = l2_table->find(address);
    if (l2_it != l2_table->end()) {
        md.missCacheLevel = 2;
        md.isCacheInterference = l2_it->second.isInterference;
    } else {
        md.missCacheLevel = 1;
        md.isCacheInterference = l1_it->second.isInterference;
    }
    // 到L2访存类型可能丢失，所以以L1为准
    md.mat = l1_it->second.mat;
    return true;
}

bool MissTables::isL1Miss(Addr address, bool &isData) {
    MissTable::iterator data_it, inst_it;

    address = blockAlign(address);
    // DPRINTF(MissTable, "Address to look up is 0x%x\n", address);

    data_it = l1DMissTable.find(address);
    inst_it = l1IMissTable.find(address);

    isData = data_it != l1DMissTable.end();
    return isData || l1IMissTable.end() != inst_it;
}

void MissTables::printMiss(MissTable &mt) {
    for (auto it = mt.begin(); it != mt.end(); ++it) {
        DPRINTF(MissTable, "L%i cache %s miss @ addr [0x%x]\n",
                it->second.cacheLevel,
                it->second.mat == MemAccessType::MemStore ? "store" : "load",
                it->first);
    }
}

void MissTables::printAllMiss() {
    DPRINTF(MissTable, "L1 I-Cache:\n");
    printMiss(l1IMissTable);
    DPRINTF(MissTable, "L1 D-Cache:\n");
    printMiss(l1DMissTable);
    DPRINTF(MissTable, "L2 Cache:\n");
    printMiss(l2MissTable);
}

bool MissTables::isMSHRFull(int cacheLevel, bool isDCache) {
    if (cacheLevel == 1) {
        if (isDCache) {
            return l1DMissTable.size() >= numL1DR_MSHR;
        } else {
            return l1IMissTable.size() >= numL1I_MSHR;
        }
    } else if (cacheLevel == 2) {
        return l2MissTable.size() >= numL2_MSHR;
    } else {
        panic("Unknown cache level %i\n", cacheLevel);
    }
}

bool MissTables::perThreadMSHRFull(int cacheLevel, bool isDCache,
                                   ThreadID tid, bool isLoad) {
    if (cacheLevel == 1) {
        if (isDCache) {
            if (isLoad) {
                return missStat.numL1LoadMiss[tid] >= numL1DR_MSHR / 2;
            } else {
                return missStat.numL1StoreMiss[tid] >= numL1DW_MSHR / 2;
            }
        } else {
            return missStat.numL1InstMiss[tid] >= numL1I_MSHR / 2;
        }
    } else if (cacheLevel == 2) {
        panic("Not implemented L%i\n", cacheLevel);
    } else {
        panic("Unknown cache level %i\n", cacheLevel);
    }
}

bool MissTables::kickedDataBlock(ThreadID tid, int &cacheLevel) {
    if (kickedBlock(l1DMissTable, tid)) {
        cacheLevel = 1;
        return true;
    }
    if (hasMiss(l1DMissTable, tid) && kickedBlock(l2MissTable, tid)) {
        cacheLevel = 2;
        return true;
    }
    cacheLevel = -1;
    return false;
}

bool MissTables::kickedInstBlock(ThreadID tid, int &cacheLevel) {
    if (kickedBlock(l1IMissTable, tid)) {
        cacheLevel = 1;
        return true;
    }
    if (hasMiss(l1IMissTable, tid) && kickedBlock(l2MissTable, tid)) {
        cacheLevel = 2;
        return true;
    }
    cacheLevel = -1;
    return false;
}

bool MissTables::kickedBlock(MissTable &mt, ThreadID tid) {
    for (auto &it : mt) {
        if (it.second.tid == tid && it.second.isInterference) {
            return true;
        }
    }
    return false;
}

bool MissTables::hasInstMiss(ThreadID tid) {
    return hasMiss(l1IMissTable, tid);
}

bool MissTables::hasDataMiss(ThreadID tid) {
    return hasMiss(l1DMissTable, tid);
}

bool MissTables::hasMiss(MissTable &mt, ThreadID tid) {
    for (auto &it : mt) {
        if (it.second.tid == tid) {
            return true;
        }
    }
    return false;
}


MissTables missTables;
