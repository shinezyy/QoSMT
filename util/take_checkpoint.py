#!/usr/bin/env python2.7

import os
import sys
import random
import sh
import operator
from os.path import join as pjoin
from os.path import expanduser as uexp
from multiprocessing import Pool


def get_spec():
    x = []
    with open('./simpointed_spec.txt') as f:
        for line in f:
            if not line.startswith('#'):
                x.append(line.strip('\n'))
    return x



def take_checkpoint(benchmark):
    # print sh.gem5_exe()
    gem5_dir = os.environ['gem5_root']
    all_checkpoing_dir = pjoin(gem5_dir, 'checkpoint')
    all_simpoint_dir = pjoin(gem5_dir, 'simpoint')

    benchmark_dir = pjoin(all_checkpoing_dir, benchmark)
    simpoint_dir = pjoin(all_simpoint_dir, benchmark)

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

    print options
    sh.gem5_exe(options)


if __name__ == '__main__':
    p = Pool(7)
    p.map(take_checkpoint, get_spec())

