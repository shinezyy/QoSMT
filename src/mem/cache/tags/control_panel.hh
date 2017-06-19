#ifndef __CONTROL_PANEL_H__
#define __CONTROL_PANEL_H__


#include <cinttypes>

#include <base/types.hh>

struct WayRationConfig {
    bool updatedByCore;
#define MaxThreads 2
    int threadWayRations[MaxThreads];
#undef MaxThreads
};

class ControlPanel {
public:
    WayRationConfig l1ICacheWayConfig;
    WayRationConfig l1DCacheWayConfig;
    WayRationConfig l2CacheWayConfig;

    ControlPanel() {
        l1ICacheWayConfig.updatedByCore = false;
        l1DCacheWayConfig.updatedByCore = false;
        l2CacheWayConfig.updatedByCore = false;
    }
};

extern ControlPanel controlPanel;

#endif // __CONTROL_PANEL_H__
