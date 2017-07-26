from m5.objects import *

def common_config(cpu, little_core):
    cpu.dumpWindowSize = 4*(10**6)
    cpu.policyWindowSize = (10**3)*20
    cpu.max_insts_hpt_thread = 200*(10**6)
    cpu.PTAVQ = False

    if little_core:
        cpu.fetchWidth = 6
        cpu.decodeWidth = 6
        cpu.renameWidth = 6
        cpu.dispatchWidth = 6
        cpu.issueWidth = 6
        cpu.wbWidth = 6
        cpu.commitWidth = 6
        cpu.squashWidth = 8

        cpu.numROBEntries = 160
        cpu.numIQEntries = 64
        cpu.LQEntries = 32
        cpu.SQEntries = 28
        cpu.numPhysIntRegs = 150
        cpu.numPhysFloatRegs = 144

    else:
        cpu.fetchWidth = 8
        cpu.decodeWidth = 8
        cpu.renameWidth = 8
        cpu.dispatchWidth = 8
        cpu.issueWidth = 8
        cpu.wbWidth = 8
        cpu.commitWidth = 8
        cpu.squashWidth = 8

        cpu.numROBEntries = 224
        cpu.numIQEntries = 96
        cpu.LQEntries = 72
        cpu.SQEntries = 56
        cpu.numPhysIntRegs = 216
        cpu.numPhysFloatRegs = 200

    # when threshold policy is used, they are meaningful
    cpu.smtROBThreshold = int(cpu.numROBEntries * 2 / 3)
    cpu.smtIQThreshold = int(cpu.numIQEntries * 2 / 3)
    cpu.smtLQThreshold = int(cpu.LQEntries * 2 / 3)
    cpu.smtSQThreshold = int(cpu.SQEntries * 2 / 3)


def cache_config_1(options):
    options.caches = True
    options.cacheline_size = 64
    options.l2cache = True

    dup = 1
    if options.dup_cache:
        dup = 2

    options.l1i_size = '{}kB'.format(32 * dup)
    options.l1i_assoc = 4 * dup
    options.l1d_size = '{}kB'.format(32 * dup)
    options.l1d_assoc = 4 * dup
    options.l2_size = '{}MB'.format(2 * dup)
    options.l2_assoc = 8 * dup

def cache_config_2(system, options):
    dup = 1
    if options.dup_cache:
        dup = 2

    if options.dyn_cache:
        assert not options.dup_cache
        assert not options.comp_cache
        for cpu in system.cpu:
            cpu.icache.tags = LRUDynPartition()
            cpu.icache.tags.thread_0_assoc = 2
            cpu.icache.shadow_tag_assoc = 4
            cpu.dcache.tags = LRUDynPartition()
            cpu.dcache.tags.thread_0_assoc = 2
            cpu.dcache.shadow_tag_assoc = 4
            cpu.dynCache = True

        system.l2.tags = LRUDynPartition()
        system.l2.tags.thread_0_assoc = 4
        system.l2.shadow_tag_assoc = 8

    elif options.comp_cache:
        assert not options.dup_cache
        assert not options.dyn_cache

        for cpu in system.cpu:
            cpu.icache.tags = LRU()
            cpu.dcache.tags = LRU()
        system.l2.tags = LRU()

    else: # static partition
        for cpu in system.cpu:
            cpu.icache.tags = LRUPartition()
            cpu.icache.tags.thread_0_assoc = 2 * dup
            cpu.icache.shadow_tag_assoc = 4
            cpu.dcache.tags = LRUPartition()
            cpu.dcache.tags.thread_0_assoc = 2 * dup
            cpu.dcache.shadow_tag_assoc = 4
            cpu.dynCache = True

        system.l2.tags = LRUPartition()
        system.l2.tags.thread_0_assoc = 4 * dup
        system.l2.shadow_tag_assoc = 8

