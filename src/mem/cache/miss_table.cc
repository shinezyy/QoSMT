#include <mem/cache/miss_table.hh>

MissTable l1MissTable;
MissTable l2MissTable;

MissStat missStat;

bool inMissTable(MissTable &mt, uint64_t sn) {
    return mt.find(sn) != mt.end();
}

bool isMiss(uint64_t sn) {
    return inMissTable(l1MissTable, sn) || inMissTable(l2MissTable, sn);
}

bool isSpecifiedMiss(uint64_t sn, int16_t cacheLevel, MemAccessType mat) {
    MissEntry *me = nullptr;
    auto l1_result = l1MissTable.find(sn);
    auto l2_result = l2MissTable.find(sn);
    if (cacheLevel == 1 && l1_result != l1MissTable.end()) {
        me = &(l1_result->second);
    } else if (cacheLevel == 2 && l2_result != l2MissTable.end()) {
        me = &(l2_result->second);
    } else {
        if (l2_result != l2MissTable.end()) {
            me = &(l2_result->second);
        } else if (l1_result != l1MissTable.end()) {
            me = &(l1_result->second);
        }
    }
    if (me == nullptr) {
        return false;
    } else {
        if (mat == MemAccessType::NotCare) {
            return true;
        } else {
            return me->isLoad == (mat == MemAccessType::MemLoad);
        }
    }
}
