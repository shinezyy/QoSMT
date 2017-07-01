def common_config(cpu):
    cpu.dumpWindowSize = 4*(10**6)
    cpu.policyWindowSize = (10**3)*20
    cpu.numROBEntries = 224
    cpu.numIQEntries = 100
    cpu.LQEntries = 72
    cpu.SQEntries = 56
    cpu.max_insts_hpt_thread = 200*(10**6)
    cpu.PTAVQ = False

