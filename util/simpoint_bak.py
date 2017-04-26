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

num_thread = 18


def get_spec():
    x = []
    print 'This script should not be used again, check please'
    assert(0)
    with open('./all_function_spec.txt') as f:
        for line in f:
            if not line.startswith('#'):
                x.append(line.strip('\n'))
    return x


def simpoint_bak(benchmark):
    gem5_dir = os.environ['gem5_root']
    all_checkpoint_dir = pjoin(gem5_dir, 'checkpoint')
    all_simpoint_dir = pjoin(gem5_dir, 'simpoint')

    benchmark_dir = pjoin(all_checkpoint_dir, benchmark)
    simpoint_dir = pjoin(all_simpoint_dir, benchmark)

    backup_dir = pjoin(uexp('~/checkpoint_bak'), benchmark)

    print 'from {} to {}'.format(simpoint_dir, backup_dir)

    sh.cp('-r', benchmark_dir, backup_dir)



if __name__ == '__main__':
    global num_thread
    num_thread = 18
    bms = get_spec()
    print len(bms), 'benchmarks:', bms, 'will be backup'
    user_verify()
    p = Pool(num_thread)
    p.map(simpoint_bak, bms)
