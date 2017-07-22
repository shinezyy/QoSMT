#include "base/types.hh"
#include "mem/cache/tags/base_set_assoc.hh"

#include <list>
#include <vector>

struct ShadowTag {
    bool valid;
    Addr tag;
    ShadowTag (bool v, Addr t)
            : valid(v), tag(t) {}
};

class ShadowLRUTag
{
    typedef std::list<ShadowTag> ShadowSet;

    const unsigned int numSet;

    const unsigned int shadowAssoc;

    const BaseSetAssoc *baseSetAssoc;

    std::vector<ShadowSet> shadowTags;

public:

    ShadowLRUTag(unsigned _numSet, unsigned _shadowAssoc,
                 BaseSetAssoc *_baseSetAssoc);

    bool findBlock(Addr address) const;

    void touch(Addr address);

    void insert(Addr address);
};
