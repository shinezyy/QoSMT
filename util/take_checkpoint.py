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


num_thread = 18


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


def take_checkpoint(benchmark):
    global num_thread
    done = get_checkpointed()
    doing = get_running_checkpoint()

    skip = done + doing

    if benchmark in skip:
        print benchmark, 'has been completed or is running, skip'
        return
    else:
        print 'Is to run', benchmark

    num_gem5 = num_running_gem5()
    while True:
        if num_gem5 < num_thread:
            break
        print 'The worker of {} sleeps because {} gem5s are running'.format(
            benchmark, num_gem5)
        time.sleep(180)


    gem5_dir = os.environ['gem5_root']
    all_checkpoing_dir = pjoin(gem5_dir, 'checkpoint')
    all_simpoint_dir = pjoin(gem5_dir, 'simpoint')

    benchmark_dir = pjoin(all_checkpoing_dir, benchmark)
    simpoint_dir = pjoin(all_simpoint_dir, benchmark)

    script_dir = os.getcwd()

    os.chdir(all_checkpoing_dir)

    simpoint_file = pjoin(simpoint_dir, 'simpoints')
    weight_file = pjoin(simpoint_dir, 'weights')
    assert(os.path.isfile(simpoint_file))
    assert(os.path.isfile(weight_file))
    interval = 100000000
    warmup = 0

    options = (
        '--outdir=' + benchmark_dir,
        pjoin(gem5_dir, 'configs/spec/simpoint.py'),
        '--take-simpoint-checkpoint={},{},{},{}'.format(simpoint_file, weight_file,
                                                        interval, warmup),
        '--mem-size=4GB',
        '--benchmark={}'.format(benchmark),
        '--benchmark_stdout=' + benchmark_dir,
        '--benchmark_stderr=' + benchmark_dir,
        '--cpu-type=AtomicSimpleCPU',
    )

    sh.gem5_exe(options)
    os.chdir(script_dir)
    with open('checkpointed.txt', 'a') as of:
        of.write('\n'+benchmark)


if __name__ == '__main__':
    global num_thread
    p = Pool(num_thread)
    p.map(take_checkpoint, get_spec())
    #map(take_checkpoint, ['sjeng'])
    #get_running_checkpoint()

