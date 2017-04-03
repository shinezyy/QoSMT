# Copyright (c) 2012-2013 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Steve Reinhardt

# Simple test script
#
# "m5 test.py"

import spec06_benchmarks
import optparse
import sys
import os

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal

addToPath('../common')
addToPath('../ruby')

import Options
import Ruby
import Simulation
import CacheConfig
import MemConfig

# Check if KVM support has been enabled, we might need to do VM
# configuration if that's the case.
have_kvm_support = 'BaseKvmCPU' in globals()
def is_kvm_cpu(cpu_class):
    return have_kvm_support and cpu_class != None and \
        issubclass(cpu_class, BaseKvmCPU)

parser = optparse.OptionParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)

parser.add_option("-b", "--benchmark",\
                  type="string", default="",\
                  help="The SPEC benchmark to be loaded."
                 )
parser.add_option("--benchmark_stdout",\
        type="string", default="",\
        help=\
        "Absolute path for stdout redirection for the benchmark."
        )
parser.add_option("--benchmark_stderr",
        type="string", default="",\
        help=\
        "Absolute path for stderr redirection for the benchmark."
        )

if '--ruby' in sys.argv:
    Ruby.define_options(parser)

(options, args) = parser.parse_args()

if args:
    print "Error: script doesn't take any positional arguments"
    sys.exit(1)

numThreads = 1

if options.smt:
    numThreads = 2


def get_benchmark_process(benchmark_name):
    spec06_benchmarks.init_env()
    if benchmark_name == 'perlbench':
        print '--> perlbench'
        process = spec06_benchmarks.get_perlbench()
    elif benchmark_name == 'bzip2':
        print '--> bzip2'
        process = spec06_benchmarks.get_bzip2()
    elif benchmark_name == 'gcc':
        print '--> gcc'
        process = spec06_benchmarks.get_gcc()
    elif benchmark_name == 'bwaves':
        print '--> bwaves'
        process = spec06_benchmarks.get_bwaves()
    elif benchmark_name == 'gamess':
        print '--> gamess'
        process = spec06_benchmarks.get_gamess()
    elif benchmark_name == 'mcf':
        print '--> mcf'
        process = spec06_benchmarks.get_mcf()
    elif benchmark_name == 'milc':
        print '--> milc'
        process = spec06_benchmarks.get_milc()
    elif benchmark_name == 'zeusmp':
        print '--> zeusmp'
        process = spec06_benchmarks.get_zeusmp()
    elif benchmark_name == 'gromacs':
        print '--> gromacs'
        process = spec06_benchmarks.get_gromacs()
    elif benchmark_name == 'cactusADM':
        print '--> cactusADM'
        process = spec06_benchmarks.get_cactusADM()
    elif benchmark_name == 'leslie3d':
        print '--> leslie3d'
        process = spec06_benchmarks.get_leslie3d()
    elif benchmark_name == 'namd':
        print '--> namd'
        process = spec06_benchmarks.get_namd()
    elif benchmark_name == 'gobmk':
        print '--> gobmk'
        process = spec06_benchmarks.get_gobmk()
    elif benchmark_name == 'dealII':
        print '--> dealII'
        process = spec06_benchmarks.get_dealII()
    elif benchmark_name == 'soplex':
        print '--> soplex'
        process = spec06_benchmarks.get_soplex()
    elif benchmark_name == 'povray':
        print '--> povray'
        process = spec06_benchmarks.get_povray()
    elif benchmark_name == 'calculix':
        print '--> calculix'
        process = spec06_benchmarks.get_calculix()
    elif benchmark_name == 'hmmer':
        print '--> hmmer'
        process = spec06_benchmarks.get_hmmer()
    elif benchmark_name == 'sjeng':
        print '--> sjeng'
        process = spec06_benchmarks.get_sjeng()
    elif benchmark_name == 'GemsFDTD':
        print '--> GemsFDTD'
        process = spec06_benchmarks.get_GemsFDTD()
    elif benchmark_name == 'libquantum':
        print '--> libquantum'
        process = spec06_benchmarks.get_libquantum()
    elif benchmark_name == 'h264ref':
        print '--> h264ref'
        process = spec06_benchmarks.get_h264ref()
    elif benchmark_name == 'tonto':
        print '--> tonto'
        process = spec06_benchmarks.get_tonto()
    elif benchmark_name == 'lbm':
        print '--> lbm'
        process = spec06_benchmarks.get_lbm()
    elif benchmark_name == 'omnetpp':
        print '--> omnetpp'
        process = spec06_benchmarks.get_omnetpp()
    elif benchmark_name == 'astar':
        print '--> astar'
        process = spec06_benchmarks.get_astar()
    elif benchmark_name == 'wrf':
        print '--> wrf'
        process = spec06_benchmarks.get_wrf()
    elif benchmark_name == 'sphinx3':
        print '--> sphinx3'
        process = spec06_benchmarks.get_sphinx3()
    elif benchmark_name == 'xalancbmk':
        print '--> xalancbmk'
        process = spec06_benchmarks.get_xalancbmk()
    elif benchmark_name == 'specrand_i':
        print '--> specrand_i'
        process = spec06_benchmarks.get_specrand_i()
    elif benchmark_name == 'specrand_f':
        print '--> specrand_f'
        process = spec06_benchmarks.get_specrand_f()
    else:
        print "No recognized SPEC2006 benchmark selected! Exiting."
        sys.exit(1)
    return process

multiprocess = []
benchmarks = options.benchmark.split(';')
assert(len(benchmarks) < 2 or options.smt)
if options.smt:
    assert(len(benchmarks) == 2)
for bm in benchmarks:
    multiprocess.append(get_benchmark_process(bm))

# Set process stdout/stderr
for i in range(0, len(multiprocess)):
    process = multiprocess[i]
    if options.benchmark_stdout:
        outfile = benchmarks[i] + '_' + str(i) + '_out'
        process.output = os.path.join(options.benchmark_stdout, outfile)
        print "stdout file[" + str(i) +"]: " + process.output
    if options.benchmark_stderr:
        errfile = benchmarks[i] + '_' + str(i) + '_err'
        process.errout = os.path.join(options.benchmark_stderr, errfile)
        print "errout file[" + str(i) +"]: " + process.errout

(CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(options)
CPUClass.numThreads = numThreads

# Check -- do not allow SMT with multiple CPUs
if options.smt and options.num_cpus > 1:
    fatal("You cannot use SMT with multiple CPUs!")

np = options.num_cpus
system = System(cpu = [CPUClass(cpu_id=i) for i in xrange(np)],
                mem_mode = test_mem_mode,
                mem_ranges = [AddrRange(options.mem_size)],
                cache_line_size = options.cacheline_size)

''' needed by newest gem5(2016.07)
if numThreads > 1:
    system.multi_thread = True
'''

# Create a top-level voltage domain
system.voltage_domain = VoltageDomain(voltage = options.sys_voltage)

# Create a source clock for the system and set the clock period
system.clk_domain = SrcClockDomain(clock =  options.sys_clock,
                                   voltage_domain = system.voltage_domain)

# Create a CPU voltage domain
system.cpu_voltage_domain = VoltageDomain()

# Create a separate clock domain for the CPUs
system.cpu_clk_domain = SrcClockDomain(clock = options.cpu_clock,
                                       voltage_domain =
                                       system.cpu_voltage_domain)

# All cpus belong to a common cpu_clk_domain, therefore running at a common
# frequency.
for cpu in system.cpu:
    cpu.clk_domain = system.cpu_clk_domain

# Sanity check
if options.fastmem:
    if CPUClass != AtomicSimpleCPU:
        fatal("Fastmem can only be used with atomic CPU!")
    if (options.caches or options.l2cache):
        fatal("You cannot use fastmem in combination with caches!")

if options.simpoint_profile:
    if not options.fastmem:
        # Atomic CPU checked with fastmem option already
        fatal("SimPoint generation should be done with atomic cpu and fastmem")
    if np > 1:
        fatal("SimPoint generation not supported with more than one CPUs")

for i in xrange(np):
    if numThreads == 1:
        system.cpu[i].workload = multiprocess[0]
    else:
        print 'is going to use smt!'
        system.cpu[i].workload = multiprocess
    print system.cpu[i].workload
    system.cpu[i].createThreads()

if options.ruby:
    if not (options.cpu_type == "detailed" or options.cpu_type == "timing"):
        print >> sys.stderr, "Ruby requires TimingSimpleCPU or O3CPU!!"
        sys.exit(1)

    Ruby.create_system(options, False, system)
    assert(options.num_cpus == len(system.ruby._cpu_ports))

    system.ruby.clk_domain = SrcClockDomain(clock = options.ruby_clock,
                                        voltage_domain = system.voltage_domain)
    for i in xrange(np):
        ruby_port = system.ruby._cpu_ports[i]

        # Create the interrupt controller and connect its ports to Ruby
        # Note that the interrupt controller is always present but only
        # in x86 does it have message ports that need to be connected
        system.cpu[i].createInterruptController()

        # Connect the cpu's cache ports to Ruby
        system.cpu[i].icache_port = ruby_port.slave
        system.cpu[i].dcache_port = ruby_port.slave
        if buildEnv['TARGET_ISA'] == 'x86':
            system.cpu[i].interrupts.pio = ruby_port.master
            system.cpu[i].interrupts.int_master = ruby_port.slave
            system.cpu[i].interrupts.int_slave = ruby_port.master
            system.cpu[i].itb.walker.port = ruby_port.slave
            system.cpu[i].dtb.walker.port = ruby_port.slave
else:
    MemClass = Simulation.setMemClass(options)
    system.membus = SystemXBar()
    system.system_port = system.membus.slave
    CacheConfig.config_cache(options, system)
    MemConfig.config_mem(options, system)

for cpu in system.cpu:
    cpu.max_insts_hpt_thread = 200*(10**6)

    cpu.dumpWindowSize = (10**3)*10000
    cpu.policyWindowSize = (10**3)*20

    cpu.expectedSlowdown = 1 * 1024 / 10 # 0~1024, lower -> higher qos
    cpu.fullThreshold = 64 # 0~1024: lower -> more strict -> higher qos

    cpu.LQEntries = 64
    cpu.SQEntries = 64

    # configs for simulate st
    cpu.controlPolicy = 'None'
    cpu.iewProgrammable = False
    cpu.hptInitDispatchWidth = 8
    cpu.smtFetchPolicy = 'RoundRobin'
    cpu.smtROBPolicy = 'Dynamic'
    cpu.smtLSQPolicy = 'Dynamic'

for cpu in system.cpu:
    cpu.icache.tags = LRUPartition()
    cpu.icache.tags.thread_0_assoc = 8
    cpu.dcache.tags = LRUPartition()
    cpu.dcache.tags.thread_0_assoc = 8

system.l2.tags = LRUPartition() # L2 partition
system.l2.tags.thread_0_assoc = 8


# options.take_checkpoints=100000
# options.at_instruction=True
# options.checkpoint_restore=100000

root = Root(full_system = False, system = system)
Simulation.run(options, root, system, FutureClass)
