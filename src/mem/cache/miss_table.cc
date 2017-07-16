#include "mem/cache/miss_table.hh"
#include "debug/missTry.hh"


bool MissTables::isSpecifiedMiss(Addr address, bool isDCache, MissDescriptor &md) {
    MissTable *l1_table = isDCache ? &l1DMissTable : &l1IMissTable;
    MissTable *l2_table = &l2MissTable;
    MissTable::iterator l1_it, l2_it;

    address = blockAlign(address);
    DPRINTF(missTry, "Address to look up is 0x%x\n", address);

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
    DPRINTF(MissTable, "Address to look up is 0x%x\n", address);

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
            return l1DMissTable.size() >= numL1D_MSHR;
        } else {
            return l1IMissTable.size() >= numL1I_MSHR;
        }
    } else if (cacheLevel == 2) {
        return l2MissTable.size() >= numL2_MSHR;
    } else {
        panic("Unknown cache level %i\n", cacheLevel);
    }
}

bool MissTables::causingMSHRFull(int cacheLevel, bool isDCache, ThreadID tid) {
    if (cacheLevel == 1) {
        if (isDCache) {
            return missStat.numL1LoadMiss[tid] +
                    missStat.numL1StoreMiss[tid] >= numL1D_MSHR / 2;
        } else {
            return missStat.numL1InstMiss[tid] >= numL1I_MSHR / 2;
        }
    } else if (cacheLevel == 2) {
        return missStat.numL2InstMiss[tid] +
                missStat.numL2DataMiss[tid] >= numL2_MSHR / 2;
    } else {
        panic("Unknown cache level %i\n", cacheLevel);
    }
}

MissTables missTables;
