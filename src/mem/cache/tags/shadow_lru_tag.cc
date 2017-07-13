#include "mem/cache/tags/shadow_lru_tag.hh"

#include <algorithm>

ShadowLRUTag::ShadowLRUTag(unsigned _numSet, unsigned _shadowAssoc,
                           BaseSetAssoc *_baseSetAssoc)
    :numSet(_numSet), shadowAssoc(_shadowAssoc),
     baseSetAssoc(_baseSetAssoc),
     shadowTags(numSet, std::list<ShadowTag>(shadowAssoc, ShadowTag(false, 0)))
{
}

bool ShadowLRUTag::findBlock(Addr address) const {
    auto setIndex = baseSetAssoc->extractSet(address);
    auto targetTag = baseSetAssoc->extractTag(address);
    auto &shadowSet = shadowTags[setIndex];

    for (auto &tag : shadowSet) {
        if (tag.valid && tag.tag == targetTag) {
            return true;
        }
    }
    return false;
}

void ShadowLRUTag::touch(Addr address) {
    auto setIndex = baseSetAssoc->extractSet(address);
    auto targetTag = baseSetAssoc->extractTag(address);
    auto &shadowSet = shadowTags[setIndex];

    for (auto it = shadowSet.begin(); it != shadowSet.end(); ++it) {
        if (it->valid && it->tag == targetTag) {
            auto tmp = *it;
            shadowSet.erase(it);
            shadowSet.push_front(tmp);
            return;
        }
    }
    panic("Touching tag not found!\n");
}

void ShadowLRUTag::insert(Addr address) {
    auto setIndex = baseSetAssoc->extractSet(address);
    auto targetTag = baseSetAssoc->extractTag(address);
    auto &shadowSet = shadowTags[setIndex];

    shadowSet.pop_back();
    shadowSet.emplace_front(ShadowTag(true, targetTag));
}
