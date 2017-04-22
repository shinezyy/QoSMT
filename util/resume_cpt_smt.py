#!/usr/bin/env python2.7

import os
import re
import sys
import random
import sh
import operator
import psutil
import time
from os.path import join as pjoin
from os.path import expanduser as uexp
from multiprocessing import Pool

from checkpoint_aggregator_alpha_smt import user_verify


def get_spec():
    x = []
    with open('./simpointed_spec.txt') as f:
        for line in f:
            if not line.startswith('#'):
                x.append(line.strip('\n'))
    return x


def get_checkpointed():
    x = []
    with open('./checkpointed.txt') as f:
        for line in f:
            x.append(line.strip('\n'))
    return x


def num_running_gem5():
    cmds = []
    running_benchmark = []
    num = 0
    for process in psutil.process_iter():
        cmds.append(process.cmdline())
    for cmd in cmds:
        if len(cmd):
            exe = cmd[0]
            if exe.endswith('gem5_exe') or exe.endswith('gem5.fast'):
                num += 1

    return num


def get_running_checkpoint():
    cmds = []
    running_benchmark = []
    for process in psutil.process_iter():
        cmds.append(process.cmdline())
    p = re.compile('simpoint/(.*)/simpoints')
    for cmd in cmds:
        if len(cmd):
            exe = cmd[0]
            if exe.endswith('gem5_exe'):
                running_benchmark.append(p.search(cmd[3]).group(1))
    return running_benchmark


def smt_run(pair):
    memory_size = '4GB'
    gem5_dir = os.environ['gem5_root']
    pair_dir = pair[0] + '_' + pair[1]
    merged_cpt_dir = pjoin(gem5_dir, 'merge_test/' + pair_dir)
    outdir = pjoin(uexp('~/sim_st_0421'), pair_dir)
    exec_dir = pjoin(gem5_dir, 'smt_run')
    os.chdir(exec_dir)

    options = (
        '--outdir=' + outdir,
        pjoin(gem5_dir, 'configs/spec/sim_st.py'),
        '--smt',
        '-r', 1,
        '--checkpoint-dir', merged_cpt_dir,
        '--mem-size=4GB',
        '--benchmark={};{}'.format(pair[0], pair[1]),
        '--benchmark_stdout=' + outdir,
        '--benchmark_stderr=' + outdir,
        '--cpu-type=detailed',
        '--caches',
        '--cacheline_size=64',
        '--l1i_size=64kB',
        '--l1d_size=64kB',
        '--l1i_assoc=16',
        '--l1d_assoc=16',
        '--l2cache',
        '--l2_size=4MB',
        '--l2_assoc=16'
    )

    print options

    user_verify()

    # sys.exit()

    sh.gem5_exe(options)


if __name__ == '__main__':
    global num_thread
    num_thread  = 10
    p = Pool(num_thread)
    #p.map(smt_run, get_spec())
    map(smt_run, [['gcc', 'gcc']])
