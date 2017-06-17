#ifndef __MISS_DESCRIPTOR_H__
#define __MISS_DESCRIPTOR_H__


#include <cinttypes>

enum MemAccessType {
    UnKnown,
    MemLoad,
    MemStore,
    NotCare,
};

struct MissDescriptor {
    bool valid;
    int16_t missCacheLevel;
    bool isCacheInterference;
    MemAccessType mat;
};

#endif // __MISS_DESCRIPTOR_H__

