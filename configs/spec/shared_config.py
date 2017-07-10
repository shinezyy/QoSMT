def common_config(cpu):
    cpu.dumpWindowSize = 4*(10**6)
    cpu.policyWindowSize = (10**3)*20
    cpu.max_insts_hpt_thread = 200*(10**6)
    cpu.PTAVQ = False

    high = False

    if high:
        cpu.numROBEntries = 224
        cpu.numIQEntries = 100
        cpu.LQEntries = 72
        cpu.SQEntries = 56
    else:
        cpu.numROBEntries = 144
        cpu.numIQEntries = 64
        cpu.LQEntries = 32
        cpu.SQEntries = 24
